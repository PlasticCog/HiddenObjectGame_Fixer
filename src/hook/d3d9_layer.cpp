#include "d3d9_layer.h"

#include <d3d9.h>
#include <intrin.h>

#include <algorithm>
#include <cmath>
#include <vector>

using std::max;
using std::min;

#include "cursor.h"
#include "display.h"
#include "framepace.h"
#include "hogfix.h"
#include "hooks.h"
#include "window.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxguid.lib")

namespace hog::d3d9 {
namespace {

// --- real entry points / method pointers -----------------------------------------------------
IDirect3D9*(WINAPI* real_Direct3DCreate9)(UINT) = nullptr;
HRESULT(WINAPI* real_Direct3DCreate9Ex)(UINT, IDirect3D9Ex**) = nullptr;

using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using EndScene_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using SetTexture_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
using DrawPrimUP_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawPrim_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedPrim_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT,
                                                      UINT);

CreateDevice_t real_CreateDevice = nullptr;
Reset_t real_Reset = nullptr;
Present_t real_Present = nullptr;
EndScene_t real_EndScene = nullptr;
SetTexture_t real_SetTexture = nullptr;
DrawPrimUP_t real_DrawPrimitiveUP = nullptr;
DrawPrim_t real_DrawPrimitive = nullptr;
DrawIndexedPrim_t real_DrawIndexedPrimitive = nullptr;

// --- state -----------------------------------------------------------------------------------
IDirect3DDevice9* g_device = nullptr;
bool g_deviceHooked = false;
HWND g_deviceWindow = nullptr;
IDirect3DBaseTexture9* g_stage0 = nullptr;
volatile LONG g_captureActive = 0;
bool g_capturedThisPass = false;

// Vtable indices for IDirect3DDevice9.
enum {
    IDX_Reset = 16,
    IDX_Present = 17,
    IDX_EndScene = 42,
    IDX_SetTexture = 65,
    IDX_DrawPrimitive = 81,
    IDX_DrawIndexedPrimitive = 82,
    IDX_DrawPrimitiveUP = 83,
};

void* Vtbl(void* obj, int i) { return (*reinterpret_cast<void***>(obj))[i]; }

// Rewrites the present parameters so the game renders cheap and windowed, and we own scaling.
void FixupPresentParams(D3DPRESENT_PARAMETERS* pp, const char* where) {
    if (!pp) return;
    HWND focus = window::GameWindow();
    if (!focus) focus = pp->hDeviceWindow;
    SIZE v = display::VirtualSize();

    bool wasFullscreen = !pp->Windowed;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    if (focus) pp->hDeviceWindow = focus;

    // Keep the game's chosen back-buffer size (that's its internal render resolution). Only fill
    // one in when the game left it at 0 (windowed "use client size"), which on our native-size
    // window would mean rendering at full 4K — exactly what we want to avoid.
    if (pp->BackBufferWidth == 0 || pp->BackBufferHeight == 0) {
        pp->BackBufferWidth = v.cx;
        pp->BackBufferHeight = v.cy;
    }
    if (pp->BackBufferFormat == D3DFMT_UNKNOWN) pp->BackBufferFormat = D3DFMT_X8R8G8B8;

    pp->PresentationInterval = g_profile.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    LOGI("d3d9: %s backbuffer %ux%u fmt=%d windowed(forced from %s) interval=%s", where, pp->BackBufferWidth,
         pp->BackBufferHeight, pp->BackBufferFormat, wasFullscreen ? "fullscreen" : "windowed",
         g_profile.vsync ? "vsync" : "immediate");
}

// Source bytes-per-pixel for the formats we can decode (0 = unsupported, e.g. DXT/compressed).
int FormatBytesPerPixel(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
            return 4;
        case D3DFMT_A4R4G4B4:
        case D3DFMT_X4R4G4B4:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_R5G6B5:
            return 2;
        default:
            return 0;
    }
}

// Decodes one row of `count` source pixels into BGRA8888 (top-down, premultiplied-agnostic).
void DecodeRow(const unsigned char* src, unsigned char* dst, int count, D3DFORMAT fmt) {
    auto x4 = [](unsigned v) { return static_cast<unsigned char>(v * 17); };           // 4-bit -> 8-bit
    auto x5 = [](unsigned v) { return static_cast<unsigned char>((v << 3) | (v >> 2)); };
    auto x6 = [](unsigned v) { return static_cast<unsigned char>((v << 2) | (v >> 4)); };
    if (fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8) {
        memcpy(dst, src, size_t(count) * 4);  // already B,G,R,A in memory
        if (fmt == D3DFMT_X8R8G8B8)
            for (int i = 0; i < count; ++i) dst[i * 4 + 3] = 0xFF;
        return;
    }
    const auto* s = reinterpret_cast<const unsigned short*>(src);
    for (int i = 0; i < count; ++i) {
        unsigned p = s[i];
        unsigned char b, g, r, a;
        switch (fmt) {
            case D3DFMT_A4R4G4B4:
                a = x4((p >> 12) & 0xF); r = x4((p >> 8) & 0xF); g = x4((p >> 4) & 0xF); b = x4(p & 0xF);
                break;
            case D3DFMT_X4R4G4B4:
                a = 0xFF; r = x4((p >> 8) & 0xF); g = x4((p >> 4) & 0xF); b = x4(p & 0xF);
                break;
            case D3DFMT_A1R5G5B5:
                a = (p & 0x8000) ? 0xFF : 0x00; r = x5((p >> 10) & 0x1F); g = x5((p >> 5) & 0x1F); b = x5(p & 0x1F);
                break;
            case D3DFMT_X1R5G5B5:
                a = 0xFF; r = x5((p >> 10) & 0x1F); g = x5((p >> 5) & 0x1F); b = x5(p & 0x1F);
                break;
            case D3DFMT_R5G6B5:
                a = 0xFF; r = x5((p >> 11) & 0x1F); g = x6((p >> 5) & 0x3F); b = x5(p & 0x1F);
                break;
            default:
                a = r = g = b = 0;
                break;
        }
        dst[i * 4 + 0] = b;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = r;
        dst[i * 4 + 3] = a;
    }
}

// Builds the OS cursor from the cursor quad the engine just drew: reads the quad's screen size and
// its texture sub-rect (UVs) from the vertex data, then copies that sub-rect out of the bound
// stage-0 texture. Reading the UVs is what lets this work when the cursor lives in a texture atlas.
// Only handles an uncompressed 32-bit texture; for anything else the shim falls back to an arrow.
void CaptureCursorFromQuad(const void* verts, UINT vertexCount, UINT stride) {
    if (!g_stage0 || !verts || vertexCount < 3 || stride < 24) return;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(g_stage0->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&tex))) || !tex) return;

    bool done = false;
    D3DSURFACE_DESC desc{};
    int bpp = FormatBytesPerPixel(SUCCEEDED(tex->GetLevelDesc(0, &desc)) ? desc.Format : D3DFMT_UNKNOWN);
    if (bpp == 0) {
        static D3DFORMAT lastLogged = D3DFMT_UNKNOWN;
        if (desc.Format != lastLogged) {
            lastLogged = desc.Format;
            LOGI("d3d9: cursor texture format 0x%X (%ux%u) unsupported for capture", desc.Format, desc.Width,
                 desc.Height);
        }
    } else {
        // Vertices are XYZRHW first (screen x,y at offset 0/4); the 2D texcoord is the last 8 bytes.
        const auto* base = static_cast<const unsigned char*>(verts);
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float minU = 1e9f, minV = 1e9f, maxU = -1e9f, maxV = -1e9f;
        for (UINT i = 0; i < vertexCount; ++i) {
            const unsigned char* v = base + size_t(i) * stride;
            float x = *reinterpret_cast<const float*>(v + 0);
            float y = *reinterpret_cast<const float*>(v + 4);
            float u = *reinterpret_cast<const float*>(v + stride - 8);
            float vv = *reinterpret_cast<const float*>(v + stride - 4);
            minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y);
            minU = min(minU, u); maxU = max(maxU, u); minV = min(minV, vv); maxV = max(maxV, vv);
        }
        int w = int(maxX - minX + 0.5f), h = int(maxY - minY + 0.5f);
        RECT sub{int(minU * desc.Width + 0.5f), int(minV * desc.Height + 0.5f),
                 int(maxU * desc.Width + 0.5f), int(maxV * desc.Height + 0.5f)};
        int sw = sub.right - sub.left, sh = sub.bottom - sub.top;
        if (w > 0 && h > 0 && w <= 256 && h <= 256 && sw > 0 && sh > 0 && sw <= 256 && sh <= 256 &&
            sub.left >= 0 && sub.top >= 0 && sub.right <= int(desc.Width) && sub.bottom <= int(desc.Height)) {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(tex->LockRect(0, &lr, &sub, D3DLOCK_READONLY))) {
                std::vector<unsigned char> buf(size_t(sw) * sh * 4);
                for (int row = 0; row < sh; ++row) {
                    const unsigned char* src = static_cast<const unsigned char*>(lr.pBits) + size_t(row) * lr.Pitch;
                    unsigned char* dst = buf.data() + size_t(row) * sw * 4;
                    DecodeRow(src, dst, sw, desc.Format);
                }
                tex->UnlockRect(0);
                // Hotspot at the quad centre (these engines centre the cursor on the mouse point).
                cursor::SubmitSprite(buf.data(), sw, sh, sw / 2, sh / 2,
                                     (reinterpret_cast<uintptr_t>(g_stage0) << 8) ^ (sub.left * 73856093u) ^
                                         (sub.top * 19349663u));
                done = true;
            }
        }
    }
    tex->Release();
    if (!done) cursor::NoteCaptureUnavailable();  // let the shim fall back to a system arrow
}

// --- device method detours -------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE H_SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (stage == 0) g_stage0 = tex;
    return real_SetTexture(dev, stage, tex);
}

UINT VertexCount(D3DPRIMITIVETYPE type, UINT prims) {
    switch (type) {
        case D3DPT_TRIANGLELIST: return prims * 3;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN: return prims + 2;
        default: return prims * 3;
    }
}

HRESULT STDMETHODCALLTYPE H_DrawPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT prims,
                                            const void* data, UINT stride) {
    if (g_captureActive) {
        if (!g_capturedThisPass) {
            CaptureCursorFromQuad(data, VertexCount(type, prims), stride);
            g_capturedThisPass = true;
        }
        return D3D_OK;  // hide the software cursor; it is served as a hardware OS cursor instead
    }
    return real_DrawPrimitiveUP(dev, type, prims, data, stride);
}
HRESULT STDMETHODCALLTYPE H_DrawPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT start, UINT prims) {
    if (g_captureActive) return D3D_OK;
    return real_DrawPrimitive(dev, type, start, prims);
}
HRESULT STDMETHODCALLTYPE H_DrawIndexedPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base,
                                                 UINT minIndex, UINT numV, UINT start, UINT prims) {
    if (g_captureActive) return D3D_OK;
    return real_DrawIndexedPrimitive(dev, type, base, minIndex, numV, start, prims);
}

HRESULT STDMETHODCALLTYPE H_EndScene(IDirect3DDevice9* dev) { return real_EndScene(dev); }

HRESULT STDMETHODCALLTYPE H_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                    const RGNDATA* dirty) {
    g_capturedThisPass = false;
    cursor::OnPresent();
    framepace::BeforePresent();

    HRESULT hr;
    HWND target = window::GameWindow();
    if (Virtualizing() && target) {
        RECT vp = window::Viewport();   // letterbox rect in the native client area
        hr = real_Present(dev, nullptr, &vp, target, nullptr);
    } else {
        hr = real_Present(dev, src, dst, wnd, dirty);
    }
    framepace::AfterPresent(dev);
    return hr;
}

HRESULT STDMETHODCALLTYPE H_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (Virtualizing()) FixupPresentParams(pp, "Reset");
    framepace::OnDeviceReset(nullptr);  // drop the query across the reset
    HRESULT hr = real_Reset(dev, pp);
    LOGI("d3d9: Reset -> 0x%08lx", hr);
    if (SUCCEEDED(hr)) framepace::OnDeviceReset(dev);  // recreate it on the reset device
    return hr;
}

void HookDevice(IDirect3DDevice9* dev) {
    if (g_deviceHooked) return;
    using namespace hooks;
    At(Vtbl(dev, IDX_Reset), H_Reset, &real_Reset, "IDirect3DDevice9::Reset");
    At(Vtbl(dev, IDX_Present), H_Present, &real_Present, "IDirect3DDevice9::Present");
    At(Vtbl(dev, IDX_EndScene), H_EndScene, &real_EndScene, "IDirect3DDevice9::EndScene");
    At(Vtbl(dev, IDX_SetTexture), H_SetTexture, &real_SetTexture, "IDirect3DDevice9::SetTexture");
    At(Vtbl(dev, IDX_DrawPrimitiveUP), H_DrawPrimitiveUP, &real_DrawPrimitiveUP, "DrawPrimitiveUP");
    At(Vtbl(dev, IDX_DrawPrimitive), H_DrawPrimitive, &real_DrawPrimitive, "DrawPrimitive");
    At(Vtbl(dev, IDX_DrawIndexedPrimitive), H_DrawIndexedPrimitive, &real_DrawIndexedPrimitive,
       "DrawIndexedPrimitive");
    g_deviceHooked = true;
    LOGI("d3d9: device methods hooked");
}

void AdoptDeviceWindow(HWND focus, HWND device) {
    HWND w = focus ? focus : device;
    g_deviceWindow = w;
    // Make sure the window is black so letterbox margins stay black.
    if (w) {
        HDC dc = GetDC(w);
        RECT rc;
        GetClientRect(w, &rc);
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        ReleaseDC(w, dc);
    }
}

HRESULT STDMETHODCALLTYPE H_CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                         DWORD behavior, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    if (Virtualizing()) {
        AdoptDeviceWindow(window::GameWindow() ? window::GameWindow() : focus, pp ? pp->hDeviceWindow : nullptr);
        FixupPresentParams(pp, "CreateDevice");
        if (g_profile.forceHardwareVP && (behavior & D3DCREATE_SOFTWARE_VERTEXPROCESSING)) {
            D3DCAPS9 caps{};
            if (SUCCEEDED(self->GetDeviceCaps(adapter, type, &caps)) &&
                (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)) {
                behavior &= ~D3DCREATE_SOFTWARE_VERTEXPROCESSING;
                behavior |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
                LOGI("d3d9: forced hardware vertex processing");
            }
        }
        // Reduce the number of frames the driver may queue, cutting input/cursor latency.
        if (g_profile.lowLatency) behavior |= D3DCREATE_DISABLE_PSGP_THREADING;
    }
    HRESULT hr = real_CreateDevice(self, adapter, type, focus, behavior, pp, out);
    LOGI("d3d9: CreateDevice adapter=%u type=%d -> 0x%08lx", adapter, type, hr);
    if (SUCCEEDED(hr) && out && *out) {
        g_device = *out;
        HookDevice(*out);
        framepace::OnDeviceReset(*out);
    }
    return hr;
}

void HookFactory(IDirect3D9* d3d) {
    static bool done = false;
    if (done || !d3d) return;
    done = true;
    hooks::At(Vtbl(d3d, 16), H_CreateDevice, &real_CreateDevice, "IDirect3D9::CreateDevice");
}

IDirect3D9* WINAPI H_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d3d = real_Direct3DCreate9(sdk);
    LOGI("d3d9: Direct3DCreate9(%u) -> %p", sdk, d3d);
    if (d3d) HookFactory(d3d);
    return d3d;
}

HRESULT WINAPI H_Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    HRESULT hr = real_Direct3DCreate9Ex(sdk, out);
    LOGI("d3d9: Direct3DCreate9Ex -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out && *out) HookFactory(*out);  // IDirect3D9Ex shares the base vtable layout
    return hr;
}

}  // namespace

IDirect3DDevice9* Device() { return g_device; }

void BeginCursorCapture() {
    InterlockedExchange(&g_captureActive, 1);
}
void EndCursorCapture() { InterlockedExchange(&g_captureActive, 0); }
bool CursorCaptureActive() { return g_captureActive != 0; }

void InstallHooks() {
    using namespace hooks;
    Api(L"d3d9.dll", "Direct3DCreate9", H_Direct3DCreate9, &real_Direct3DCreate9);
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    if (d3d && GetProcAddress(d3d, "Direct3DCreate9Ex"))
        Api(L"d3d9.dll", "Direct3DCreate9Ex", H_Direct3DCreate9Ex, &real_Direct3DCreate9Ex);
}

}  // namespace hog::d3d9

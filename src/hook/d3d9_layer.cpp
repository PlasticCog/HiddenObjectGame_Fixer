#include "d3d9_layer.h"

#include <d3d9.h>
#include <intrin.h>

#include <cmath>

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

// Copies the currently bound stage-0 texture (assumed to be the cursor sprite) to the OS cursor.
void TryCaptureCursorTexture() {
    if (!g_stage0) return;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(g_stage0->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&tex))) || !tex) return;
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(tex->GetLevelDesc(0, &desc)) && desc.Width <= 256 && desc.Height <= 256 &&
        desc.Format == D3DFMT_A8R8G8B8) {
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) {
            // The sprite texture is usually a tight power-of-two holding one cursor; use it whole.
            // (Precise sub-rect/hotspot comes from the engine shim when it knows them.)
            cursor::SubmitSprite(lr.pBits, int(desc.Width), int(desc.Height), 0, 0,
                                 reinterpret_cast<uintptr_t>(g_stage0));
            tex->UnlockRect(0);
        }
    }
    tex->Release();
}

// --- device method detours -------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE H_SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    if (stage == 0) g_stage0 = tex;
    return real_SetTexture(dev, stage, tex);
}

bool SuppressDrawForCapture() {
    if (!g_captureActive || g_capturedThisPass) return false;
    TryCaptureCursorTexture();
    g_capturedThisPass = true;
    return cursor::UsingConvertedCursor();  // only hide the sprite once we actually have a cursor
}

HRESULT STDMETHODCALLTYPE H_DrawPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT prims,
                                            const void* data, UINT stride) {
    if (SuppressDrawForCapture()) return D3D_OK;
    return real_DrawPrimitiveUP(dev, type, prims, data, stride);
}
HRESULT STDMETHODCALLTYPE H_DrawPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT start, UINT prims) {
    if (SuppressDrawForCapture()) return D3D_OK;
    return real_DrawPrimitive(dev, type, start, prims);
}
HRESULT STDMETHODCALLTYPE H_DrawIndexedPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base,
                                                 UINT minIndex, UINT numV, UINT start, UINT prims) {
    if (SuppressDrawForCapture()) return D3D_OK;
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

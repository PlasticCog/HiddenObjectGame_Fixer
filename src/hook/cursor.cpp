#include "cursor.h"

#include <intrin.h>

#include "display.h"
#include "hogfix.h"
#include "hooks.h"
#include "window.h"

namespace hog::cursor {
namespace {

namespace real {
BOOL(WINAPI* SetCursorPos)(int, int);
HCURSOR(WINAPI* SetCursor)(HCURSOR);
int(WINAPI* ShowCursor)(BOOL);
BOOL(WINAPI* ClipCursor)(const RECT*);
HCURSOR(WINAPI* LoadCursorW)(HINSTANCE, LPCWSTR);
HCURSOR(WINAPI* LoadCursorA)(HINSTANCE, LPCSTR);
}  // namespace real

// --- converted-sprite cursor (camp 2) --------------------------------------------------------
CRITICAL_SECTION g_lock;
HCURSOR g_converted = nullptr;       // current OS cursor built from the game's sprite
unsigned long long g_convertedKey = 0;
bool g_wantConverted = false;        // the game is drawing its own cursor sprite
bool g_gameCursorVisible = true;
volatile LONG g_pendingW = 0, g_pendingH = 0;
unsigned char* g_pendingPixels = nullptr;  // owned, BGRA top-down
int g_pendingHotX = 0, g_pendingHotY = 0;
unsigned long long g_pendingKey = 0;

HCURSOR BuildCursor(const unsigned char* bgra, int w, int h, int hotX, int hotY) {
    HBITMAP color = nullptr, mask = nullptr;
    HDC dc = GetDC(nullptr);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h;  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    memcpy(bits, bgra, size_t(w) * h * 4);
    mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = FALSE;
    ii.xHotspot = hotX;
    ii.yHotspot = hotY;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HCURSOR cur = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return cur;
}

// Promotes any pending sprite (captured on the render thread) into an HCURSOR on this thread.
void ConsumePending() {
    unsigned char* pixels = nullptr;
    int w = 0, h = 0, hotX = 0, hotY = 0;
    unsigned long long key = 0;
    EnterCriticalSection(&g_lock);
    if (g_pendingPixels) {
        pixels = g_pendingPixels;
        g_pendingPixels = nullptr;
        w = g_pendingW;
        h = g_pendingH;
        hotX = g_pendingHotX;
        hotY = g_pendingHotY;
        key = g_pendingKey;
    }
    LeaveCriticalSection(&g_lock);
    if (!pixels) return;
    if (key != g_convertedKey) {
        HCURSOR cur = BuildCursor(pixels, w, h, hotX, hotY);
        if (cur) {
            HCURSOR old = g_converted;
            g_converted = cur;
            g_convertedKey = key;
            if (old) DestroyIcon(old);
        }
    }
    free(pixels);
}

bool ConvertedEnabled() { return g_profile.cursor == CursorMode::System; }

// --- hooks -----------------------------------------------------------------------------------

// Camp 1: the game warps the cursor to clamp it. We swallow those warps (letting the hardware
// cursor move freely) unless the game is genuinely recentering for relative-look (rare here).
BOOL WINAPI H_SetCursorPos(int x, int y) {
    if (Virtualizing() && CalledFromGame(_ReturnAddress())) {
        // Drop redundant per-frame clamps; this is what makes the cursor feel heavy.
        LOGV("cursor: swallowed SetCursorPos(%d,%d)", x, y);
        return TRUE;
    }
    return real::SetCursorPos(x, y);
}

// GetCursorPos returns real screen coordinates. Engines that want client-space coordinates call
// ScreenToClient next, which we hook to produce virtual coordinates, so screen values pass
// through unchanged. (An engine that treats the screen point directly as a fullscreen client
// coordinate is handled per-engine in the engine shims, not here.)

HCURSOR WINAPI H_SetCursor(HCURSOR c) {
    if (Virtualizing() && ConvertedEnabled() && CalledFromGame(_ReturnAddress())) {
        if (g_wantConverted && g_converted) {
            return real::SetCursor(g_gameCursorVisible ? g_converted : nullptr);
        }
    }
    return real::SetCursor(c);
}

int WINAPI H_ShowCursor(BOOL show) {
    if (Virtualizing() && ConvertedEnabled() && g_wantConverted && CalledFromGame(_ReturnAddress())) {
        // When we serve the converted cursor, keep the OS cursor shown regardless.
        g_gameCursorVisible = show != FALSE;
        if (g_gameCursorVisible && g_converted) real::SetCursor(g_converted);
        return show ? 0 : -1;
    }
    return real::ShowCursor(show);
}

BOOL WINAPI H_ClipCursor(const RECT* r) {
    if (!Virtualizing() || !CalledFromGame(_ReturnAddress())) return real::ClipCursor(r);
    if (!r || !g_profile.confineCursor) return real::ClipCursor(nullptr);  // don't trap the pointer
    // Map the game's virtual clip rect onto the real viewport.
    HWND hwnd = window::GameWindow();
    if (!hwnd) return real::ClipCursor(nullptr);
    RECT vp = window::Viewport();
    POINT tl{vp.left, vp.top}, br{vp.right, vp.bottom};
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    RECT real_r{tl.x, tl.y, br.x, br.y};
    return real::ClipCursor(&real_r);
}

}  // namespace

void Init() { InitializeCriticalSection(&g_lock); }

void SubmitSprite(const void* bgra, int w, int h, int hotspotX, int hotspotY, unsigned long long key) {
    if (!ConvertedEnabled() || w <= 0 || h <= 0 || w > 256 || h > 256) return;
    auto* copy = static_cast<unsigned char*>(malloc(size_t(w) * h * 4));
    if (!copy) return;
    memcpy(copy, bgra, size_t(w) * h * 4);
    EnterCriticalSection(&g_lock);
    g_wantConverted = true;
    free(g_pendingPixels);
    g_pendingPixels = copy;
    g_pendingW = w;
    g_pendingH = h;
    g_pendingHotX = hotspotX;
    g_pendingHotY = hotspotY;
    g_pendingKey = key;
    LeaveCriticalSection(&g_lock);
}

void SetGameCursorVisible(bool visible) { g_gameCursorVisible = visible; }

bool UsingConvertedCursor() { return g_wantConverted && g_converted != nullptr; }

void OnPresent() {
    if (!ConvertedEnabled() || !g_wantConverted) return;
    ConsumePending();
    // Re-assert our cursor each frame in case the game set a different one.
    if (g_converted && g_gameCursorVisible) {
        HWND hwnd = window::GameWindow();
        if (hwnd && GetForegroundWindow() == hwnd) real::SetCursor(g_converted);
    }
}

void InstallHooks() {
    using namespace hooks;
    Api(L"user32.dll", "SetCursorPos", H_SetCursorPos, &real::SetCursorPos);
    Api(L"user32.dll", "SetCursor", H_SetCursor, &real::SetCursor);
    Api(L"user32.dll", "ShowCursor", H_ShowCursor, &real::ShowCursor);
    Api(L"user32.dll", "ClipCursor", H_ClipCursor, &real::ClipCursor);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    real::LoadCursorW = reinterpret_cast<decltype(real::LoadCursorW)>(GetProcAddress(u32, "LoadCursorW"));
    real::LoadCursorA = reinterpret_cast<decltype(real::LoadCursorA)>(GetProcAddress(u32, "LoadCursorA"));
}

}  // namespace hog::cursor

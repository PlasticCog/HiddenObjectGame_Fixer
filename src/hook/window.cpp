#include "window.h"

#include <windowsx.h>
#include <intrin.h>

#include <algorithm>

#include "display.h"
#include "hogfix.h"
#include "hooks.h"

namespace hog::window {
namespace {

using std::max;
using std::min;

HWND g_game = nullptr;
RECT g_viewport{};      // where the virtual image lands inside the real client area
SIZE g_virtual{};
CRITICAL_SECTION g_lock;

WNDPROC g_origProc = nullptr;

namespace real {
HWND(WINAPI* CreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
HWND(WINAPI* CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL(WINAPI* SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
BOOL(WINAPI* MoveWindow)(HWND, int, int, int, int, BOOL);
LONG(WINAPI* SetWindowLongW)(HWND, int, LONG);
LONG(WINAPI* SetWindowLongA)(HWND, int, LONG);
BOOL(WINAPI* GetClientRect)(HWND, LPRECT);
BOOL(WINAPI* GetWindowRect)(HWND, LPRECT);
BOOL(WINAPI* ClientToScreen)(HWND, LPPOINT);
BOOL(WINAPI* ScreenToClient)(HWND, LPPOINT);
BOOL(WINAPI* AdjustWindowRect)(LPRECT, DWORD, BOOL);
BOOL(WINAPI* AdjustWindowRectEx)(LPRECT, DWORD, BOOL, DWORD);
BOOL(WINAPI* GetWindowPlacement)(HWND, WINDOWPLACEMENT*);
}  // namespace real

bool LooksLikeGameWindow(DWORD style, int w, int h) {
    // The render window is a normal top-level window with a reasonable size, not a tooltip/child.
    if (style & WS_CHILD) return false;
    return w >= 320 && h >= 240;
}

// Recomputes the letterbox rect: virtual image centered inside the real client area, aspect kept.
void RecomputeViewport() {
    if (!g_game) return;
    RECT rc;
    real::GetClientRect(g_game, &rc);
    int cw = rc.right, ch = rc.bottom;
    SIZE v = g_virtual.cx ? g_virtual : display::VirtualSize();
    if (v.cx <= 0 || v.cy <= 0 || cw <= 0 || ch <= 0) return;

    RECT vp{0, 0, cw, ch};
    if (g_profile.aspect == AspectMode::Keep || g_profile.aspect == AspectMode::Integer) {
        double scale = (g_profile.aspect == AspectMode::Integer)
                           ? double(max<long>(1, min<long>(cw / v.cx, ch / v.cy)))
                           : min(double(cw) / v.cx, double(ch) / v.cy);
        int dw = int(v.cx * scale + 0.5), dh = int(v.cy * scale + 0.5);
        vp.left = (cw - dw) / 2;
        vp.top = (ch - dh) / 2;
        vp.right = vp.left + dw;
        vp.bottom = vp.top + dh;
    }
    EnterCriticalSection(&g_lock);
    g_viewport = vp;
    LeaveCriticalSection(&g_lock);
}

// Mouse messages whose lParam carries client-relative x,y we must scale real->virtual so the
// game (which believes its client is the virtual size) hit-tests where the player actually clicked.
bool IsClientMouseMsg(UINT m) {
    switch (m) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return true;
        default:
            return false;
    }
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (hwnd == g_game && IsClientMouseMsg(msg)) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RealToVirtual(p);
        lp = MAKELPARAM(static_cast<short>(p.x), static_cast<short>(p.y));
    }
    return CallWindowProcW(g_origProc, hwnd, msg, wp, lp);
}

DWORD StyleFor(DWORD requested) {
    if (g_profile.mode == DisplayMode::Borderless) return WS_POPUP | WS_VISIBLE | (requested & WS_MINIMIZEBOX);
    // Windowed: a normal resizable frame.
    return (requested | WS_OVERLAPPEDWINDOW | WS_VISIBLE) & ~WS_MAXIMIZE;
}

// Where and how big the real window should be, given the virtual client size.
void PlacedRect(DWORD style, int& x, int& y, int& w, int& h) {
    const auto& mon = display::Target();
    SIZE v = display::VirtualSize();
    if (g_profile.mode == DisplayMode::Borderless) {
        x = mon.rect.left;
        y = mon.rect.top;
        w = mon.rect.right - mon.rect.left;
        h = mon.rect.bottom - mon.rect.top;
        return;
    }
    double fit = min(double(mon.work.right - mon.work.left) / v.cx,
                     double(mon.work.bottom - mon.work.top) / v.cy);
    double scale = g_profile.windowScale > 0 ? g_profile.windowScale : min(fit, 1.0) * 0.95;
    RECT rc{0, 0, int(v.cx * scale), int(v.cy * scale)};
    real::AdjustWindowRect(&rc, style, FALSE);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    x = mon.work.left + ((mon.work.right - mon.work.left) - w) / 2;
    y = mon.work.top + ((mon.work.bottom - mon.work.top) - h) / 2;
}

void AdoptWindow(HWND hwnd) {
    if (g_game || !Virtualizing()) return;
    g_game = hwnd;
    g_virtual = display::VirtualSize();
    DWORD style = StyleFor(GetWindowLongW(hwnd, GWL_STYLE));
    real::SetWindowLongW(hwnd, GWL_STYLE, style);
    int x, y, w, h;
    PlacedRect(style, x, y, w, h);
    real::SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    RecomputeViewport();
    // Subclass the window so mouse-message coordinates are scaled into the game's virtual space.
    g_origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassProc)));
    LOGI("window: adopted %p -> real (%d,%d) %dx%d, virtual client %ldx%ld", hwnd, x, y, w, h, g_virtual.cx,
         g_virtual.cy);
}

HWND WINAPI H_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                              HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    bool candidate = Virtualizing() && !g_game && LooksLikeGameWindow(style, w, h) && CalledFromGame(_ReturnAddress());
    HWND hwnd = real::CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (hwnd && candidate) AdoptWindow(hwnd);
    return hwnd;
}
HWND WINAPI H_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                              HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    bool candidate = Virtualizing() && !g_game && LooksLikeGameWindow(style, w, h) && CalledFromGame(_ReturnAddress());
    HWND hwnd = real::CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (hwnd && candidate) AdoptWindow(hwnd);
    return hwnd;
}

// Once we own the window, the game's own attempts to move/resize/restyle it are ignored.
bool OwnCall(HWND hwnd, void* ret) {
    return Virtualizing() && hwnd == g_game && CalledFromGame(ret);
}

BOOL WINAPI H_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
    if (OwnCall(hwnd, _ReturnAddress())) {
        // Allow Z-order/activation changes, block geometry changes.
        return real::SetWindowPos(hwnd, after, 0, 0, 0, 0, flags | SWP_NOMOVE | SWP_NOSIZE);
    }
    return real::SetWindowPos(hwnd, after, x, y, w, h, flags);
}
BOOL WINAPI H_MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
    if (OwnCall(hwnd, _ReturnAddress())) return TRUE;
    return real::MoveWindow(hwnd, x, y, w, h, repaint);
}
LONG WINAPI H_SetWindowLongW(HWND hwnd, int idx, LONG v) {
    if (OwnCall(hwnd, _ReturnAddress()) && (idx == GWL_STYLE || idx == GWL_EXSTYLE)) return GetWindowLongW(hwnd, idx);
    return real::SetWindowLongW(hwnd, idx, v);
}
LONG WINAPI H_SetWindowLongA(HWND hwnd, int idx, LONG v) {
    if (OwnCall(hwnd, _ReturnAddress()) && (idx == GWL_STYLE || idx == GWL_EXSTYLE)) return GetWindowLongW(hwnd, idx);
    return real::SetWindowLongA(hwnd, idx, v);
}

// The game must believe its client area is exactly the virtual size.
BOOL WINAPI H_GetClientRect(HWND hwnd, LPRECT r) {
    BOOL ok = real::GetClientRect(hwnd, r);
    if (ok && OwnCall(hwnd, _ReturnAddress())) {
        SIZE v = display::VirtualSize();
        *r = RECT{0, 0, v.cx, v.cy};
    }
    return ok;
}
BOOL WINAPI H_GetWindowRect(HWND hwnd, LPRECT r) {
    BOOL ok = real::GetWindowRect(hwnd, r);
    if (ok && OwnCall(hwnd, _ReturnAddress())) {
        SIZE v = display::VirtualSize();
        *r = RECT{0, 0, v.cx, v.cy};
    }
    return ok;
}

BOOL WINAPI H_ClientToScreen(HWND hwnd, LPPOINT p) {
    if (OwnCall(hwnd, _ReturnAddress()) && p) {
        VirtualToReal(*p);
        return real::ClientToScreen(hwnd, p);
    }
    return real::ClientToScreen(hwnd, p);
}
BOOL WINAPI H_ScreenToClient(HWND hwnd, LPPOINT p) {
    if (OwnCall(hwnd, _ReturnAddress()) && p) {
        BOOL ok = real::ScreenToClient(hwnd, p);
        RealToVirtual(*p);
        return ok;
    }
    return real::ScreenToClient(hwnd, p);
}

BOOL WINAPI H_GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT* wp) {
    BOOL ok = real::GetWindowPlacement(hwnd, wp);
    if (ok && OwnCall(hwnd, _ReturnAddress()) && wp) {
        wp->showCmd = SW_SHOWNORMAL;  // never report minimized/maximized; confuses some engines
        wp->flags = 0;
    }
    return ok;
}

}  // namespace

void Init() { InitializeCriticalSection(&g_lock); }

HWND GameWindow() { return g_game; }

void OnVirtualDisplayChanged(int w, int h, int) {
    g_virtual = SIZE{w, h};
    RecomputeViewport();
}

RECT Viewport() {
    EnterCriticalSection(&g_lock);
    RECT r = g_viewport;
    LeaveCriticalSection(&g_lock);
    if (r.right <= r.left) {  // not computed yet
        SIZE v = display::VirtualSize();
        return RECT{0, 0, v.cx, v.cy};
    }
    return r;
}
void SetViewport(const RECT& r) {
    EnterCriticalSection(&g_lock);
    g_viewport = r;
    LeaveCriticalSection(&g_lock);
}

void RealToVirtual(POINT& p) {
    RECT vp = Viewport();
    SIZE v = display::VirtualSize();
    int vw = vp.right - vp.left, vh = vp.bottom - vp.top;
    if (vw <= 0 || vh <= 0) return;
    p.x = int((p.x - vp.left) * double(v.cx) / vw + 0.5);
    p.y = int((p.y - vp.top) * double(v.cy) / vh + 0.5);
    p.x = max(0L, min<LONG>(v.cx - 1, p.x));
    p.y = max(0L, min<LONG>(v.cy - 1, p.y));
}
void VirtualToReal(POINT& p) {
    RECT vp = Viewport();
    SIZE v = display::VirtualSize();
    if (v.cx <= 0 || v.cy <= 0) return;
    p.x = int(vp.left + p.x * double(vp.right - vp.left) / v.cx + 0.5);
    p.y = int(vp.top + p.y * double(vp.bottom - vp.top) / v.cy + 0.5);
}

void InstallHooks() {
    using namespace hooks;
    Api(L"user32.dll", "CreateWindowExW", H_CreateWindowExW, &real::CreateWindowExW);
    Api(L"user32.dll", "CreateWindowExA", H_CreateWindowExA, &real::CreateWindowExA);
    Api(L"user32.dll", "SetWindowPos", H_SetWindowPos, &real::SetWindowPos);
    Api(L"user32.dll", "MoveWindow", H_MoveWindow, &real::MoveWindow);
    Api(L"user32.dll", "SetWindowLongW", H_SetWindowLongW, &real::SetWindowLongW);
    Api(L"user32.dll", "SetWindowLongA", H_SetWindowLongA, &real::SetWindowLongA);
    Api(L"user32.dll", "GetClientRect", H_GetClientRect, &real::GetClientRect);
    Api(L"user32.dll", "GetWindowRect", H_GetWindowRect, &real::GetWindowRect);
    Api(L"user32.dll", "ClientToScreen", H_ClientToScreen, &real::ClientToScreen);
    Api(L"user32.dll", "ScreenToClient", H_ScreenToClient, &real::ScreenToClient);
    Api(L"user32.dll", "GetWindowPlacement", H_GetWindowPlacement, &real::GetWindowPlacement);
    // AdjustWindowRect is only ever called, never detoured; grab the real pointer directly.
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    real::AdjustWindowRect = reinterpret_cast<decltype(real::AdjustWindowRect)>(
        GetProcAddress(u32, "AdjustWindowRect"));
    real::AdjustWindowRectEx = reinterpret_cast<decltype(real::AdjustWindowRectEx)>(
        GetProcAddress(u32, "AdjustWindowRectEx"));
}

}  // namespace hog::window

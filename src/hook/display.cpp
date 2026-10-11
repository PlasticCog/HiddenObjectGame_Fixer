#include "display.h"

#include <intrin.h>

#include <algorithm>

#include "hogfix.h"
#include "hooks.h"
#include "window.h"

namespace hog::display {
namespace {

Monitor g_mon;
SIZE g_virtual{};
int g_virtualBpp = 32;
std::vector<Mode> g_modes;
CRITICAL_SECTION g_lock;

namespace real {
int(WINAPI* GetSystemMetrics)(int);
BOOL(WINAPI* EnumDisplaySettingsExW)(LPCWSTR, DWORD, DEVMODEW*, DWORD);
BOOL(WINAPI* EnumDisplaySettingsExA)(LPCSTR, DWORD, DEVMODEA*, DWORD);
BOOL(WINAPI* EnumDisplaySettingsW)(LPCWSTR, DWORD, DEVMODEW*);
BOOL(WINAPI* EnumDisplaySettingsA)(LPCSTR, DWORD, DEVMODEA*);
LONG(WINAPI* ChangeDisplaySettingsExW)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);
LONG(WINAPI* ChangeDisplaySettingsExA)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
LONG(WINAPI* ChangeDisplaySettingsW)(DEVMODEW*, DWORD);
LONG(WINAPI* ChangeDisplaySettingsA)(DEVMODEA*, DWORD);
BOOL(WINAPI* SystemParametersInfoW)(UINT, UINT, PVOID, UINT);
BOOL(WINAPI* SystemParametersInfoA)(UINT, UINT, PVOID, UINT);
BOOL(WINAPI* GetMonitorInfoW)(HMONITOR, LPMONITORINFO);
BOOL(WINAPI* GetMonitorInfoA)(HMONITOR, LPMONITORINFO);
int(WINAPI* GetDeviceCaps)(HDC, int);
}  // namespace real

struct MonitorEnum {
    std::vector<HMONITOR> list;
};

BOOL CALLBACK CollectMonitor(HMONITOR m, HDC, LPRECT, LPARAM p) {
    reinterpret_cast<MonitorEnum*>(p)->list.push_back(m);
    return TRUE;
}

void ReadMonitor(HMONITOR m) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    auto getInfo = real::GetMonitorInfoW ? real::GetMonitorInfoW : &::GetMonitorInfoW;
    getInfo(m, &mi);
    g_mon.handle = m;
    g_mon.rect = mi.rcMonitor;
    g_mon.work = mi.rcWork;
    wcsncpy_s(g_mon.device, mi.szDevice, _TRUNCATE);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    auto enumSettings = real::EnumDisplaySettingsExW ? real::EnumDisplaySettingsExW : &::EnumDisplaySettingsExW;
    if (enumSettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm, 0) && dm.dmDisplayFrequency > 1)
        g_mon.refresh = int(dm.dmDisplayFrequency);
}

int Width(const RECT& r) { return r.right - r.left; }
int Height(const RECT& r) { return r.bottom - r.top; }

void BuildModeList() {
    SIZE native = NativeSize();
    SIZE def = DefaultVirtualSize();
    // When we render below native, hide modes above the default virtual size so a game that
    // picks "the largest mode" picks one the iGPU can actually push.
    int capH = (g_profile.renderHeight == -1) ? native.cy : def.cy;
    std::vector<Mode> modes;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    auto enumSettings = real::EnumDisplaySettingsExW ? real::EnumDisplaySettingsExW : &::EnumDisplaySettingsExW;
    for (DWORD i = 0; enumSettings(g_mon.device, i, &dm, 0); ++i) {
        if (dm.dmBitsPerPel < 16 || int(dm.dmPelsHeight) > capH) continue;
        if (dm.dmDisplayOrientation != DMDO_DEFAULT) continue;
        Mode m{int(dm.dmPelsWidth), int(dm.dmPelsHeight), 32, g_mon.refresh};
        bool dup = std::any_of(modes.begin(), modes.end(),
                               [&](const Mode& o) { return o.width == m.width && o.height == m.height; });
        if (!dup) modes.push_back(m);
    }
    // Classic 4:3 modes some games insist on, plus the virtual desktop itself.
    const SIZE extra[] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024},
                          {1366, 768}, {1600, 900}, {1920, 1080}, def};
    for (SIZE s : extra) {
        if (s.cy > capH || s.cx > native.cx) continue;
        bool dup = std::any_of(modes.begin(), modes.end(),
                               [&](const Mode& o) { return o.width == s.cx && o.height == s.cy; });
        if (!dup) modes.push_back({int(s.cx), int(s.cy), 32, g_mon.refresh});
    }
    std::sort(modes.begin(), modes.end(), [](const Mode& a, const Mode& b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    g_modes = std::move(modes);
    LOGI("display: %zu modes offered to the game (cap height %d)", g_modes.size(), capH);
}

bool IsTargetDevice(const wchar_t* dev) { return !dev || !*dev || _wcsicmp(dev, g_mon.device) == 0; }
bool IsTargetDeviceA(const char* dev) {
    if (!dev || !*dev) return true;
    return _wcsicmp(Widen(dev).c_str(), g_mon.device) == 0;
}

// Fills the mode fields shared by DEVMODEA and DEVMODEW (everything after the device name).
template <class DM>
void PatchMode(DM* dm, int w, int h, int bpp, int refresh) {
    dm->dmPelsWidth = w;
    dm->dmPelsHeight = h;
    dm->dmBitsPerPel = bpp;
    dm->dmDisplayFrequency = refresh;
    dm->dmPosition.x = 0;
    dm->dmPosition.y = 0;
    dm->dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_POSITION;
}

template <class DM, class RealFn>
BOOL EnumCommon(DWORD mode, DM* dm, RealFn callReal) {
    if (mode == ENUM_CURRENT_SETTINGS || mode == ENUM_REGISTRY_SETTINGS) {
        if (!callReal(ENUM_CURRENT_SETTINGS)) return FALSE;
        SIZE v = mode == ENUM_CURRENT_SETTINGS ? VirtualSize() : DefaultVirtualSize();
        PatchMode(dm, v.cx, v.cy, mode == ENUM_CURRENT_SETTINGS ? g_virtualBpp : 32, g_mon.refresh);
        return TRUE;
    }
    const auto& modes = ModeList();
    if (mode >= modes.size()) return FALSE;
    if (!callReal(ENUM_CURRENT_SETTINGS)) return FALSE;
    const Mode& m = modes[mode];
    PatchMode(dm, m.width, m.height, m.bpp, m.refresh);
    return TRUE;
}

BOOL WINAPI H_EnumDisplaySettingsExW(LPCWSTR dev, DWORD mode, DEVMODEW* dm, DWORD flags) {
    if (!Virtualizing() || !dm || !IsTargetDevice(dev) || !CalledFromGame(_ReturnAddress()))
        return real::EnumDisplaySettingsExW(dev, mode, dm, flags);
    return EnumCommon(mode, dm, [&](DWORD m) { return real::EnumDisplaySettingsExW(dev, m, dm, flags); });
}
BOOL WINAPI H_EnumDisplaySettingsExA(LPCSTR dev, DWORD mode, DEVMODEA* dm, DWORD flags) {
    if (!Virtualizing() || !dm || !IsTargetDeviceA(dev) || !CalledFromGame(_ReturnAddress()))
        return real::EnumDisplaySettingsExA(dev, mode, dm, flags);
    return EnumCommon(mode, dm, [&](DWORD m) { return real::EnumDisplaySettingsExA(dev, m, dm, flags); });
}
BOOL WINAPI H_EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW* dm) {
    if (!Virtualizing() || !dm || !IsTargetDevice(dev) || !CalledFromGame(_ReturnAddress()))
        return real::EnumDisplaySettingsW(dev, mode, dm);
    return EnumCommon(mode, dm, [&](DWORD m) { return real::EnumDisplaySettingsW(dev, m, dm); });
}
BOOL WINAPI H_EnumDisplaySettingsA(LPCSTR dev, DWORD mode, DEVMODEA* dm) {
    if (!Virtualizing() || !dm || !IsTargetDeviceA(dev) || !CalledFromGame(_ReturnAddress()))
        return real::EnumDisplaySettingsA(dev, mode, dm);
    return EnumCommon(mode, dm, [&](DWORD m) { return real::EnumDisplaySettingsA(dev, m, dm); });
}

template <class DM>
LONG ChangeCommon(DM* dm, DWORD flags) {
    if (!dm) {
        RestoreVirtualSize("ChangeDisplaySettings(NULL)");
        return DISP_CHANGE_SUCCESSFUL;
    }
    SIZE cur = VirtualSize();
    int w = (dm->dmFields & DM_PELSWIDTH) ? int(dm->dmPelsWidth) : cur.cx;
    int h = (dm->dmFields & DM_PELSHEIGHT) ? int(dm->dmPelsHeight) : cur.cy;
    if (w <= 0 || h <= 0) return DISP_CHANGE_BADMODE;
    if (flags & CDS_TEST) return DISP_CHANGE_SUCCESSFUL;
    if (dm->dmFields & DM_BITSPERPEL) g_virtualBpp = int(dm->dmBitsPerPel);
    SetVirtualSize(w, h, "ChangeDisplaySettings");
    return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI H_ChangeDisplaySettingsExW(LPCWSTR dev, DEVMODEW* dm, HWND hwnd, DWORD flags, LPVOID lp) {
    if (!Virtualizing() || !IsTargetDevice(dev)) return real::ChangeDisplaySettingsExW(dev, dm, hwnd, flags, lp);
    return ChangeCommon(dm, flags);
}
LONG WINAPI H_ChangeDisplaySettingsExA(LPCSTR dev, DEVMODEA* dm, HWND hwnd, DWORD flags, LPVOID lp) {
    if (!Virtualizing() || !IsTargetDeviceA(dev)) return real::ChangeDisplaySettingsExA(dev, dm, hwnd, flags, lp);
    return ChangeCommon(dm, flags);
}
LONG WINAPI H_ChangeDisplaySettingsW(DEVMODEW* dm, DWORD flags) {
    if (!Virtualizing()) return real::ChangeDisplaySettingsW(dm, flags);
    return ChangeCommon(dm, flags);
}
LONG WINAPI H_ChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags) {
    if (!Virtualizing()) return real::ChangeDisplaySettingsA(dm, flags);
    return ChangeCommon(dm, flags);
}

int WINAPI H_GetSystemMetrics(int index) {
    if (!Virtualizing() || !CalledFromGame(_ReturnAddress())) return real::GetSystemMetrics(index);
    SIZE v = VirtualSize();
    switch (index) {
        case SM_CXSCREEN:
        case SM_CXFULLSCREEN:
        case SM_CXVIRTUALSCREEN:
        case SM_CXMAXIMIZED:
        case SM_CXMAXTRACK:
            return v.cx;
        case SM_CYSCREEN:
        case SM_CYVIRTUALSCREEN:
        case SM_CYMAXIMIZED:
        case SM_CYMAXTRACK:
            return v.cy;
        case SM_CYFULLSCREEN:
            return v.cy - real::GetSystemMetrics(SM_CYCAPTION);
        case SM_XVIRTUALSCREEN:
        case SM_YVIRTUALSCREEN:
            return 0;
        case SM_CMONITORS:
            return 1;
        default:
            return real::GetSystemMetrics(index);
    }
}

RECT VirtualWorkArea() {
    SIZE v = VirtualSize();
    double sx = double(v.cx) / Width(g_mon.rect), sy = double(v.cy) / Height(g_mon.rect);
    RECT r;
    r.left = LONG((g_mon.work.left - g_mon.rect.left) * sx);
    r.top = LONG((g_mon.work.top - g_mon.rect.top) * sy);
    r.right = LONG((g_mon.work.right - g_mon.rect.left) * sx);
    r.bottom = LONG((g_mon.work.bottom - g_mon.rect.top) * sy);
    return r;
}

BOOL WINAPI H_SystemParametersInfoW(UINT action, UINT ui, PVOID pv, UINT win) {
    if (action == SPI_GETWORKAREA && pv && Virtualizing() && CalledFromGame(_ReturnAddress())) {
        *static_cast<RECT*>(pv) = VirtualWorkArea();
        return TRUE;
    }
    return real::SystemParametersInfoW(action, ui, pv, win);
}
BOOL WINAPI H_SystemParametersInfoA(UINT action, UINT ui, PVOID pv, UINT win) {
    if (action == SPI_GETWORKAREA && pv && Virtualizing() && CalledFromGame(_ReturnAddress())) {
        *static_cast<RECT*>(pv) = VirtualWorkArea();
        return TRUE;
    }
    return real::SystemParametersInfoA(action, ui, pv, win);
}

template <class RealFn>
BOOL MonitorInfoCommon(HMONITOR m, LPMONITORINFO mi, void* ret, RealFn callReal) {
    BOOL ok = callReal();
    if (ok && Virtualizing() && m == g_mon.handle && CalledFromGame(ret)) {
        SIZE v = VirtualSize();
        mi->rcMonitor = RECT{0, 0, v.cx, v.cy};
        mi->rcWork = VirtualWorkArea();
    }
    return ok;
}
BOOL WINAPI H_GetMonitorInfoW(HMONITOR m, LPMONITORINFO mi) {
    return MonitorInfoCommon(m, mi, _ReturnAddress(), [&] { return real::GetMonitorInfoW(m, mi); });
}
BOOL WINAPI H_GetMonitorInfoA(HMONITOR m, LPMONITORINFO mi) {
    return MonitorInfoCommon(m, mi, _ReturnAddress(), [&] { return real::GetMonitorInfoA(m, mi); });
}

int WINAPI H_GetDeviceCaps(HDC hdc, int index) {
    if (Virtualizing() && (index == HORZRES || index == VERTRES || index == DESKTOPHORZRES ||
                           index == DESKTOPVERTRES)) {
        if (real::GetDeviceCaps(hdc, TECHNOLOGY) == DT_RASDISPLAY && CalledFromGame(_ReturnAddress())) {
            SIZE v = VirtualSize();
            return (index == HORZRES || index == DESKTOPHORZRES) ? v.cx : v.cy;
        }
    }
    return real::GetDeviceCaps(hdc, index);
}

}  // namespace

void Init() {
    InitializeCriticalSection(&g_lock);
    MonitorEnum e;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&e));
    HMONITOR chosen = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (g_profile.monitor > 0 && g_profile.monitor <= int(e.list.size())) chosen = e.list[g_profile.monitor - 1];
    ReadMonitor(chosen);
    for (size_t i = 0; i < e.list.size(); ++i) {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(e.list[i], &mi);
        LOGI("display: monitor %zu %ls (%ld,%ld)-(%ld,%ld)%s%s", i + 1, mi.szDevice, mi.rcMonitor.left,
             mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
             (mi.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "", e.list[i] == chosen ? " <- target" : "");
    }
    g_virtual = DefaultVirtualSize();
    LOGI("display: target %ls native %ldx%ld @%dHz, virtual desktop %ldx%ld", g_mon.device, NativeSize().cx,
         NativeSize().cy, g_mon.refresh, g_virtual.cx, g_virtual.cy);
}

void RefreshMonitor() {
    ReadMonitor(g_mon.handle);
}

const Monitor& Target() { return g_mon; }

SIZE NativeSize() { return SIZE{Width(g_mon.rect), Height(g_mon.rect)}; }

SIZE DefaultVirtualSize() {
    SIZE n = NativeSize();
    int h = g_profile.renderHeight;
    if (h == 0) h = n.cy > 1080 ? 1080 : n.cy;  // auto: cap at 1080p, cheap enough for any iGPU
    if (h < 0 || h >= n.cy) return n;
    int w = int((double(h) * n.cx / n.cy) + 0.5);
    w &= ~1;
    return SIZE{w, h};
}

SIZE VirtualSize() {
    EnterCriticalSection(&g_lock);
    SIZE v = g_virtual;
    LeaveCriticalSection(&g_lock);
    return v;
}

void SetVirtualSize(int w, int h, const char* why) {
    EnterCriticalSection(&g_lock);
    bool changed = g_virtual.cx != w || g_virtual.cy != h;
    g_virtual = SIZE{w, h};
    LeaveCriticalSection(&g_lock);
    if (changed) {
        LOGI("display: virtual desktop -> %dx%d (%s)", w, h, why);
        window::OnVirtualDisplayChanged(w, h, g_virtualBpp);
    }
}

void RestoreVirtualSize(const char* why) {
    SIZE d = DefaultVirtualSize();
    g_virtualBpp = 32;
    SetVirtualSize(d.cx, d.cy, why);
}

const std::vector<Mode>& ModeList() {
    if (g_modes.empty()) BuildModeList();
    return g_modes;
}

void InstallHooks() {
    using namespace hooks;
    Api(L"user32.dll", "GetSystemMetrics", H_GetSystemMetrics, &real::GetSystemMetrics);
    Api(L"user32.dll", "EnumDisplaySettingsExW", H_EnumDisplaySettingsExW, &real::EnumDisplaySettingsExW);
    Api(L"user32.dll", "EnumDisplaySettingsExA", H_EnumDisplaySettingsExA, &real::EnumDisplaySettingsExA);
    Api(L"user32.dll", "EnumDisplaySettingsW", H_EnumDisplaySettingsW, &real::EnumDisplaySettingsW);
    Api(L"user32.dll", "EnumDisplaySettingsA", H_EnumDisplaySettingsA, &real::EnumDisplaySettingsA);
    Api(L"user32.dll", "ChangeDisplaySettingsExW", H_ChangeDisplaySettingsExW, &real::ChangeDisplaySettingsExW);
    Api(L"user32.dll", "ChangeDisplaySettingsExA", H_ChangeDisplaySettingsExA, &real::ChangeDisplaySettingsExA);
    Api(L"user32.dll", "ChangeDisplaySettingsW", H_ChangeDisplaySettingsW, &real::ChangeDisplaySettingsW);
    Api(L"user32.dll", "ChangeDisplaySettingsA", H_ChangeDisplaySettingsA, &real::ChangeDisplaySettingsA);
    Api(L"user32.dll", "SystemParametersInfoW", H_SystemParametersInfoW, &real::SystemParametersInfoW);
    Api(L"user32.dll", "SystemParametersInfoA", H_SystemParametersInfoA, &real::SystemParametersInfoA);
    Api(L"user32.dll", "GetMonitorInfoW", H_GetMonitorInfoW, &real::GetMonitorInfoW);
    Api(L"user32.dll", "GetMonitorInfoA", H_GetMonitorInfoA, &real::GetMonitorInfoA);
    Api(L"gdi32.dll", "GetDeviceCaps", H_GetDeviceCaps, &real::GetDeviceCaps);
}

}  // namespace hog::display

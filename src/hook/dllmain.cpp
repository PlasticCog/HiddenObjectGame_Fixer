// hogfix.dll — injected into a hidden-object game before its first instruction.
//
// The loader creates the game suspended, LoadLibrary's this DLL, then calls HogfixInit (below)
// while the game's main thread is still suspended. We read the profile, set DPI awareness, and
// install the generic hooks (display / window / cursor / D3D9 / frame pacing) plus any per-engine
// shim. Then the loader resumes the game, which now sees a virtual display and a window/cursor we
// control, and renders cheap while we upscale.
#include <windows.h>
#include <shlwapi.h>

#include <string>

#include "childinject.h"
#include "cursor.h"
#include "d3d9_layer.h"
#include "display.h"
#include "engine_shim.h"
#include "framepace.h"
#include "hogfix.h"
#include "hooks.h"
#include "inject.h"
#include "log.h"
#include "window.h"

#pragma comment(lib, "shlwapi.lib")

namespace hog {

Profile g_profile;
std::wstring g_dllDir, g_dllPath, g_profilePath;
HMODULE g_module = nullptr;

namespace {
// Address range of the game's main executable, used by CalledFromGame().
uintptr_t g_mainBase = 0, g_mainEnd = 0;

void ComputeMainModuleRange() {
    HMODULE m = GetModuleHandleW(nullptr);
    g_mainBase = reinterpret_cast<uintptr_t>(m);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_mainBase + dos->e_lfanew);
    g_mainEnd = g_mainBase + nt->OptionalHeader.SizeOfImage;
}

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return (n > 0 && n < std::size(buf)) ? std::wstring(buf, n) : std::wstring();
}

// Keep the game from pinning itself to one core after startup.
BOOL(WINAPI* real_SetProcessAffinityMask)(HANDLE, DWORD_PTR) = nullptr;
BOOL WINAPI H_SetProcessAffinityMask(HANDLE proc, DWORD_PTR mask) {
    DWORD_PTR procMask = 0, sysMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask)) {
        LOGV("hogfix: ignored SetProcessAffinityMask(0x%Ix), keeping 0x%Ix", mask, sysMask);
        return real_SetProcessAffinityMask(proc, sysMask);
    }
    return real_SetProcessAffinityMask(proc, mask);
}

void ApplyDpiAwareness(DpiMode mode) {
    // Choose awareness before any window exists so the game's pixels are real pixels (we handle
    // all scaling ourselves). PerMonitorV2 is best on Win10; fall back gracefully.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setCtx = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(
        user32 ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr);
    if (mode == DpiMode::PerMonitor && setCtx) {
        if (setCtx(reinterpret_cast<HANDLE>(-4)) || setCtx(reinterpret_cast<HANDLE>(-3))) return;  // v2, then v1
    }
    if (mode == DpiMode::Unaware) {
        if (setCtx && setCtx(reinterpret_cast<HANDLE>(-1))) return;
    }
    // System-DPI aware (or fallback): the old API, available everywhere.
    auto setAware = reinterpret_cast<BOOL(WINAPI*)()>(user32 ? GetProcAddress(user32, "SetProcessDPIAware") : nullptr);
    if (mode != DpiMode::Unaware && setAware) setAware();
}

}  // namespace

bool CalledFromGame(void* returnAddress) {
    auto a = reinterpret_cast<uintptr_t>(returnAddress);
    return a >= g_mainBase && a < g_mainEnd;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring Widen(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, 0);
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

bool Startup() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_module, path, MAX_PATH);
    g_dllPath = path;
    g_dllDir = g_dllPath.substr(0, g_dllPath.find_last_of(L"\\/"));

    g_profilePath = EnvVar(kProfileEnvVar);
    std::wstring logPath = EnvVar(kLogEnvVar);

    if (!g_profilePath.empty()) g_profile.Load(g_profilePath);
    log::Init(logPath);
    log::SetVerbose(g_profile.verboseLog);

    LOGI("hogfix: starting for \"%ls\" (engine=%ls, mode=%ls)", g_profile.name.c_str(),
         g_profile.engine.c_str(), ToString(g_profile.mode));
    if (g_profile.mode == DisplayMode::Passthrough) {
        LOGI("hogfix: passthrough mode, no hooks installed");
        return true;
    }

    ComputeMainModuleRange();
    ApplyDpiAwareness(g_profile.dpi);

    // Several of these games (and the bundle launcher that normally starts them) pin the process
    // to a single CPU with SetProcessAffinityMask(1). On a modern many-core machine that starves
    // the render/update threads and is a direct cause of low FPS and a laggy cursor. Give the
    // process every core back.
    {
        DWORD_PTR procMask = 0, sysMask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask) && procMask != sysMask) {
            SetProcessAffinityMask(GetCurrentProcess(), sysMask);
            LOGI("hogfix: restored CPU affinity 0x%Ix -> 0x%Ix", procMask, sysMask);
        }
    }

    if (!hooks::Init()) return false;
    display::Init();
    window::Init();
    cursor::Init();
    framepace::Init();

    hooks::Api(L"kernel32.dll", "SetProcessAffinityMask", H_SetProcessAffinityMask, &real_SetProcessAffinityMask);
    childinject::InstallHooks();
    display::InstallHooks();
    window::InstallHooks();
    cursor::InstallHooks();
    d3d9::InstallHooks();
    engineshim::Apply();

    LOGI("hogfix: hooks installed");
    return true;
}

}  // namespace hog

// Called by the loader via CreateRemoteThread while the game is still suspended. Returns 1 on
// success, 0 on failure. Runs outside the loader lock, so COM / LoadLibrary are safe here.
extern "C" __declspec(dllexport) DWORD WINAPI HogfixInit(LPVOID) {
    return hog::Startup() ? 1 : 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hog::g_module = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

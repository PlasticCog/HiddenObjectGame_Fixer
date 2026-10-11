#include "childinject.h"

#include <windows.h>

#include <string>

#include "hogfix.h"
#include "hooks.h"
#include "inject.h"
#include "log.h"

namespace hog::childinject {
namespace {

namespace real {
BOOL(WINAPI* CreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                             LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
BOOL(WINAPI* CreateProcessA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                             LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
}  // namespace real

std::wstring LowerName(const std::wstring& path) {
    std::wstring s = path.substr(path.find_last_of(L"\\/") + 1);
    for (auto& c : s) c = towlower(c);
    return s;
}

// Only follow children that live under the game's directory tree (so we don't inject into
// unrelated system helpers, browsers opened for store links, etc.).
bool ShouldFollow(const std::wstring& childPath) {
    if (childPath.empty()) return false;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(nullptr), self, MAX_PATH);
    std::wstring gameDir = std::wstring(self).substr(0, std::wstring(self).find_last_of(L"\\/"));
    for (auto& c : gameDir) c = towlower(c);
    std::wstring child = childPath;
    for (auto& c : child) c = towlower(c);
    // Same folder (or a subfolder) as the game, but never ourselves looping on a helper.
    return child.rfind(gameDir, 0) == 0;
}

void InjectChild(PROCESS_INFORMATION* pi, const std::wstring& childPath) {
    std::wstring err;
    if (InjectIntoSuspended(pi->hProcess, g_dllPath, err))
        LOGI("childinject: hooked child pid %lu (%ls)", pi->dwProcessId, LowerName(childPath).c_str());
    else
        LOGW("childinject: could not hook child %ls: %ls", LowerName(childPath).c_str(), err.c_str());
}

template <class CharT, class SI, class RealFn, class Widen>
BOOL Common(const CharT* app, CharT* cmd, DWORD flags, SI* si, PROCESS_INFORMATION* pi, RealFn callReal,
            Widen widen) {
    bool follow = g_profile.followChildren;
    std::wstring childPath = app ? widen(app) : std::wstring();
    if (childPath.empty() && cmd) {
        // Pull the program out of the command line (handles a leading quoted path).
        std::wstring c = widen(cmd);
        size_t start = c.find_first_not_of(L" \t");
        if (start != std::wstring::npos) {
            if (c[start] == L'"') {
                size_t end = c.find(L'"', start + 1);
                childPath = c.substr(start + 1, end - start - 1);
            } else {
                size_t end = c.find_first_of(L" \t", start);
                childPath = c.substr(start, end - start);
            }
        }
    }
    follow = follow && ShouldFollow(childPath);

    DWORD useFlags = flags | (follow ? CREATE_SUSPENDED : 0);
    BOOL ok = callReal(useFlags);
    if (!ok || !follow) return ok;

    InjectChild(pi, childPath);
    // Resume only if the caller didn't ask for a suspended child themselves.
    if (!(flags & CREATE_SUSPENDED)) ResumeThread(pi->hThread);
    return ok;
}

BOOL WINAPI H_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                             BOOL inherit, DWORD flags, LPVOID env, LPCWSTR dir, LPSTARTUPINFOW si,
                             LPPROCESS_INFORMATION pi) {
    return Common(
        app, cmd, flags, si, pi,
        [&](DWORD f) { return real::CreateProcessW(app, cmd, pa, ta, inherit, f, env, dir, si, pi); },
        [](const wchar_t* s) { return std::wstring(s); });
}

BOOL WINAPI H_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                             BOOL inherit, DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFOA si,
                             LPPROCESS_INFORMATION pi) {
    return Common(
        app, cmd, flags, si, pi,
        [&](DWORD f) { return real::CreateProcessA(app, cmd, pa, ta, inherit, f, env, dir, si, pi); },
        [](const char* s) { return Widen(s); });
}

}  // namespace

void InstallHooks() {
    if (!g_profile.followChildren) return;
    using namespace hooks;
    Api(L"kernel32.dll", "CreateProcessW", H_CreateProcessW, &real::CreateProcessW);
    Api(L"kernel32.dll", "CreateProcessA", H_CreateProcessA, &real::CreateProcessA);
}

}  // namespace hog::childinject

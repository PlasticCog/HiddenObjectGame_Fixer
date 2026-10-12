// Starting a game with hogfix.dll injected before its first instruction runs.
#pragma once
#include <windows.h>

#include <string>

#include "profile.h"

namespace hog {

// Sets (or clears) Windows' per-app graphics preference for `exe`, so the next launch runs on the
// chosen GPU. Writes HKCU\Software\Microsoft\DirectX\UserGpuPreferences. Default clears it.
void ApplyGpuPreference(const std::wstring& exe, GpuPreference pref);

struct LaunchResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring error;
};

// Name of the export the injector calls after LoadLibrary succeeds (runs outside the loader
// lock, while the game's main thread is still suspended).
inline constexpr char kInitExport[] = "HogfixInit";

// Loads `dllPath` into a process whose main thread is suspended, then calls its init export.
// Does NOT resume the process. Returns false and fills `error` on failure.
bool InjectIntoSuspended(HANDLE process, const std::wstring& dllPath, std::wstring& error);

// Creates the game process suspended with HOGFIX_PROFILE/HOGFIX_LOG set in its environment,
// injects hogfix.dll, then resumes it.
LaunchResult LaunchWithHook(const std::wstring& exe, const std::wstring& args, const std::wstring& workDir,
                            const std::wstring& dllPath, const std::wstring& profilePath,
                            const std::wstring& logPath);

std::wstring LastErrorText(DWORD err = GetLastError());

}  // namespace hog

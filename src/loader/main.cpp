// hogrun.exe — command-line launcher: starts a game with hogfix.dll injected, using a profile.
// Usage:
//   hogrun --profile <file.ini> [--dll <hogfix.dll>] [--log <file>]
//   hogrun --exe <game.exe> [--engine sf|lakehouse|neoaxis|flash|generic] [--passthrough]
// The launcher GUI shells out to this, but it is also handy on its own.
#include <windows.h>

#include <cstdio>
#include <string>

#include "inject.h"
#include "profile.h"

using namespace hog;

namespace {

std::wstring DirOf(const std::wstring& p) { return p.substr(0, p.find_last_of(L"\\/")); }

std::wstring SelfDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return DirOf(buf);
}

std::wstring Arg(int argc, wchar_t** argv, const wchar_t* name, const wchar_t* def = L"") {
    for (int i = 1; i + 1 < argc; ++i)
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool Flag(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], name) == 0) return true;
    return false;
}

int Fail(const std::wstring& msg) {
    fwprintf(stderr, L"hogrun: %s\n", msg.c_str());
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dll = Arg(argc, argv, L"--dll");
    if (dll.empty()) dll = SelfDir() + L"\\hogfix.dll";

    Profile prof;
    std::wstring profilePath = Arg(argc, argv, L"--profile");
    if (!profilePath.empty()) {
        if (!prof.Load(profilePath)) return Fail(L"could not read profile: " + profilePath);
    } else {
        prof.exe = Arg(argc, argv, L"--exe");
        if (prof.exe.empty()) return Fail(L"need --profile <file> or --exe <game.exe>");
        prof.engine = Arg(argc, argv, L"--engine", L"generic");
        prof.name = prof.exe.substr(prof.exe.find_last_of(L"\\/") + 1);
    }
    if (Flag(argc, argv, L"--passthrough")) prof.mode = DisplayMode::Passthrough;

    if (prof.exe.empty() || GetFileAttributesW(prof.exe.c_str()) == INVALID_FILE_ATTRIBUTES)
        return Fail(L"game executable not found: " + prof.exe);
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
        return Fail(L"hogfix.dll not found: " + dll);

    std::wstring logPath = Arg(argc, argv, L"--log");
    if (logPath.empty()) logPath = DirOf(prof.exe) + L"\\hogfix.log";

    ApplyGpuPreference(prof.exe, prof.gpu);
    LaunchResult r = LaunchWithHook(prof.exe, prof.args, prof.workDir, dll, profilePath, logPath);
    if (!r.ok) return Fail(r.error);
    wprintf(L"hogrun: launched pid %lu\n", r.pid);
    return 0;
}

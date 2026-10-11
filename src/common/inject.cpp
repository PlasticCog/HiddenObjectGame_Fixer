#include "inject.h"

#include "profile.h"

#include <vector>

namespace hog {
namespace {

// RVA of an export, read straight from the DLL file on disk (no loading, no DllMain).
DWORD ExportRva(const std::wstring& dllPath, const char* name) {
    HANDLE file = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    HANDLE map = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
    CloseHandle(file);
    if (!map) return 0;
    auto* base = static_cast<BYTE*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
    CloseHandle(map);
    if (!base) return 0;
    DWORD rva = 0;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress) {
        auto* exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        auto* names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
        auto* ords = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals);
        auto* funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
            if (strcmp(reinterpret_cast<char*>(base + names[i]), name) == 0) {
                rva = funcs[ords[i]];
                break;
            }
        }
    }
    UnmapViewOfFile(base);
    return rva;
}

bool RunRemote(HANDLE process, LPTHREAD_START_ROUTINE fn, void* param, DWORD& exitCode, std::wstring& error,
               const wchar_t* what) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, fn, param, 0, nullptr);
    if (!thread) {
        error = std::wstring(L"CreateRemoteThread(") + what + L") failed: " + LastErrorText();
        return false;
    }
    DWORD wait = WaitForSingleObject(thread, 60000);
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0) {
        error = std::wstring(what) + L" timed out in the game process";
        return false;
    }
    return true;
}

// Builds a copy of our environment block with extra/overridden variables.
std::vector<wchar_t> MakeEnvironment(const std::vector<std::pair<std::wstring, std::wstring>>& extra) {
    std::vector<wchar_t> env;
    wchar_t* block = GetEnvironmentStringsW();
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        std::wstring entry = p;
        bool overridden = false;
        for (const auto& [k, v] : extra) {
            if (_wcsnicmp(entry.c_str(), (k + L"=").c_str(), k.size() + 1) == 0) overridden = true;
        }
        if (!overridden) env.insert(env.end(), entry.c_str(), entry.c_str() + entry.size() + 1);
    }
    FreeEnvironmentStringsW(block);
    for (const auto& [k, v] : extra) {
        std::wstring entry = k + L"=" + v;
        env.insert(env.end(), entry.c_str(), entry.c_str() + entry.size() + 1);
    }
    env.push_back(L'\0');
    return env;
}

}  // namespace

std::wstring LastErrorText(DWORD err) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring s = L"error " + std::to_wstring(err);
    if (msg) {
        s += L": ";
        s += msg;
        LocalFree(msg);
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
    }
    return s;
}

bool InjectIntoSuspended(HANDLE process, const std::wstring& dllPath, std::wstring& error) {
    DWORD initRva = ExportRva(dllPath, kInitExport);
    if (!initRva) {
        error = L"Could not find " + std::wstring(L"HogfixInit") + L" in " + dllPath;
        return false;
    }
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        error = L"VirtualAllocEx failed: " + LastErrorText();
        return false;
    }
    bool ok = false;
    if (WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        // kernel32 is mapped at the same address in every 32-bit process of this boot session.
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        DWORD module = 0;
        if (RunRemote(process, loadLibrary, remote, module, error, L"LoadLibraryW")) {
            if (!module) {
                error = L"hogfix.dll failed to load inside the game process";
            } else {
                DWORD initResult = 0;
                auto init = reinterpret_cast<LPTHREAD_START_ROUTINE>(static_cast<uintptr_t>(module) + initRva);
                if (RunRemote(process, init, nullptr, initResult, error, L"HogfixInit")) {
                    ok = initResult == 1;
                    if (!ok) error = L"HogfixInit reported failure (see the game's log)";
                }
            }
        }
    } else {
        error = L"WriteProcessMemory failed: " + LastErrorText();
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return ok;
}

LaunchResult LaunchWithHook(const std::wstring& exe, const std::wstring& args, const std::wstring& workDir,
                            const std::wstring& dllPath, const std::wstring& profilePath,
                            const std::wstring& logPath) {
    LaunchResult r;
    std::wstring cwd = workDir;
    if (cwd.empty()) {
        cwd = exe.substr(0, exe.find_last_of(L"\\/"));
    }
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) cmd += L" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    std::vector<std::pair<std::wstring, std::wstring>> vars;
    if (!profilePath.empty()) vars.emplace_back(kProfileEnvVar, profilePath);
    if (!logPath.empty()) vars.emplace_back(kLogEnvVar, logPath);
    std::vector<wchar_t> env = MakeEnvironment(vars);

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(), cwd.c_str(), &si, &pi)) {
        r.error = L"Could not start " + exe + L": " + LastErrorText();
        return r;
    }
    r.pid = pi.dwProcessId;
    if (!dllPath.empty() && !InjectIntoSuspended(pi.hProcess, dllPath, r.error)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return r;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    r.ok = true;
    return r;
}

}  // namespace hog

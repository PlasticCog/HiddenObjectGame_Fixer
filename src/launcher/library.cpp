#include "library.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

namespace hog::launcher {
namespace {

std::wstring Combine(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t last = a.back();
    return (last == L'\\' || last == L'/') ? a + b : a + L"\\" + b;
}
std::wstring DirOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s);
}
std::wstring FileName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}
std::wstring Stem(const std::wstring& p) {
    std::wstring f = FileName(p);
    size_t d = f.find_last_of(L'.');
    return d == std::wstring::npos ? f : f.substr(0, d);
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Sanitize(const std::wstring& name) {
    std::wstring out;
    for (wchar_t c : name) {
        if (iswalnum(c) || c == L'-' || c == L'_')
            out += c;
        else if (c == L' ')
            out += L'_';
    }
    if (out.empty()) out = L"game";
    return out;
}

}  // namespace

std::wstring SelfDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return DirOf(buf);
}

std::wstring DllPath() { return Combine(SelfDir(), L"hogfix.dll"); }

std::wstring ProfilesDir() {
    wchar_t* appdata = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        base = appdata;
        CoTaskMemFree(appdata);
    }
    std::wstring dir = Combine(Combine(base, L"HOGFixer"), L"profiles");
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::vector<Entry> LoadLibrary() {
    std::vector<Entry> out;
    std::wstring dir = ProfilesDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Combine(dir, L"*.ini").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Entry e;
            e.file = Combine(dir, fd.cFileName);
            if (e.profile.Load(e.file)) {
                e.missing = !FileExists(e.profile.exe);
                out.push_back(std::move(e));
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return _wcsicmp(a.profile.name.c_str(), b.profile.name.c_str()) < 0; });
    return out;
}

bool SaveEntry(Entry& e) {
    if (e.file.empty()) {
        std::wstring base = Sanitize(e.profile.name.empty() ? Stem(e.profile.exe) : e.profile.name);
        std::wstring candidate = Combine(ProfilesDir(), base + L".ini");
        for (int i = 2; Exists(candidate); ++i)
            candidate = Combine(ProfilesDir(), base + L"_" + std::to_wstring(i) + L".ini");
        e.file = candidate;
    }
    return e.profile.Save(e.file);
}

bool DeleteEntry(const Entry& e) { return !e.file.empty() && DeleteFileW(e.file.c_str()); }

std::wstring DetectEngine(const std::wstring& exePath) {
    std::wstring dir = DirOf(exePath);
    std::wstring stem = Stem(exePath);
    auto sib = [&](const wchar_t* n) { return FileExists(Combine(dir, n)); };

    if (sib(L"magic.dll") || _wcsnicmp(stem.c_str(), L"LakeHouse", 9) == 0) return L"lakehouse";
    if (sib(L"Game.bin") && sib(L"EngineApp.dll")) return L"neoaxis";
    // Flash host: a CONFIG\application.ini mentioning Flash, or sibling SWF data files.
    if (sib(L"CONFIG\\application.ini") || sib(L"engine") && sib(L"data") && sib(L"LIB\\interface.swf"))
        return L"flash";
    // sf / Alawar D3D9 engine: game.pack + game.xml, or a properties folder.
    if (sib(L"game.pack") && sib(L"game.xml")) return L"sf";
    return L"generic";
}

hog::Profile MakeDefaultProfile(const std::wstring& exePath) {
    hog::Profile p;
    p.exe = exePath;
    p.name = Stem(exePath);
    p.engine = DetectEngine(exePath);
    p.workDir = DirOf(exePath);
    // Flash host can't be forced windowed by the D3D path; let it manage its own window for now.
    if (p.engine == L"flash") p.mode = hog::DisplayMode::Passthrough;
    return p;
}

std::vector<std::wstring> ScanBundle(const std::wstring& bundleRoot) {
    std::vector<std::wstring> games;
    std::wstring myGames = Combine(bundleRoot, L"My Games");
    for (int n = 1; n <= 32; ++n) {
        std::wstring folder = Combine(myGames, std::to_wstring(n));
        std::wstring manifest = Combine(folder, L"manifest.xml");
        if (!FileExists(manifest)) continue;
        // manifest.xml: <GameExecutable>Name.exe</GameExecutable>
        HANDLE h = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        char buf[2048];
        DWORD read = 0;
        ReadFile(h, buf, sizeof(buf) - 1, &read, nullptr);
        CloseHandle(h);
        buf[read] = 0;
        std::string s(buf);
        size_t a = s.find("<GameExecutable>");
        size_t b = s.find("</GameExecutable>");
        if (a == std::string::npos || b == std::string::npos) continue;
        a += strlen("<GameExecutable>");
        std::string exe = s.substr(a, b - a);
        int wn = MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, nullptr, 0);
        std::wstring wexe(wn ? wn - 1 : 0, 0);
        MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, wexe.data(), wn);
        std::wstring full = Combine(folder, wexe);
        if (FileExists(full)) games.push_back(full);
    }
    return games;
}

}  // namespace hog::launcher

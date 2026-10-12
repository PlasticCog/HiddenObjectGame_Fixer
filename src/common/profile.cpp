#include "profile.h"

#include <windows.h>

#include <cwchar>
#include <cwctype>

namespace hog {
namespace {

struct Ini {
    const std::wstring& path;

    std::wstring Get(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"") const {
        wchar_t buf[2048];
        GetPrivateProfileStringW(sec, key, def, buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        return buf;
    }
    int GetInt(const wchar_t* sec, const wchar_t* key, int def) const {
        std::wstring s = Get(sec, key);
        return s.empty() ? def : _wtoi(s.c_str());
    }
    float GetFloat(const wchar_t* sec, const wchar_t* key, float def) const {
        std::wstring s = Get(sec, key);
        return s.empty() ? def : static_cast<float>(_wtof(s.c_str()));
    }
    bool GetBool(const wchar_t* sec, const wchar_t* key, bool def) const {
        std::wstring s = Get(sec, key);
        if (s.empty()) return def;
        wchar_t c = static_cast<wchar_t>(towlower(s[0]));
        return c == L'1' || c == L't' || c == L'y' || c == L'o' && s.size() > 1 && towlower(s[1]) == L'n';
    }
    bool Put(const wchar_t* sec, const wchar_t* key, const std::wstring& v) const {
        return WritePrivateProfileStringW(sec, key, v.c_str(), path.c_str()) != FALSE;
    }
};

bool IEquals(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

template <class E, size_t N>
E ParseEnum(const std::wstring& s, const E (&values)[N], E def) {
    for (E v : values)
        if (IEquals(s, ToString(v))) return v;
    return def;
}

std::wstring Num(float f) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%g", f);
    return buf;
}

}  // namespace

const wchar_t* ToString(DisplayMode v) {
    switch (v) {
        case DisplayMode::Borderless: return L"borderless";
        case DisplayMode::Windowed: return L"windowed";
        case DisplayMode::Passthrough: return L"passthrough";
    }
    return L"";
}
const wchar_t* ToString(ScaleFilter v) {
    switch (v) {
        case ScaleFilter::Bilinear: return L"bilinear";
        case ScaleFilter::Bicubic: return L"bicubic";
        case ScaleFilter::Sharp: return L"sharp";
        case ScaleFilter::Nearest: return L"nearest";
    }
    return L"";
}
const wchar_t* ToString(AspectMode v) {
    switch (v) {
        case AspectMode::Keep: return L"keep";
        case AspectMode::Stretch: return L"stretch";
        case AspectMode::Integer: return L"integer";
    }
    return L"";
}
const wchar_t* ToString(CursorMode v) {
    switch (v) {
        case CursorMode::System: return L"system";
        case CursorMode::Game: return L"game";
    }
    return L"";
}
const wchar_t* ToString(DpiMode v) {
    switch (v) {
        case DpiMode::PerMonitor: return L"permonitor";
        case DpiMode::System: return L"system";
        case DpiMode::Unaware: return L"unaware";
    }
    return L"";
}
const wchar_t* ToString(GpuPreference v) {
    switch (v) {
        case GpuPreference::Default: return L"default";
        case GpuPreference::PowerSaving: return L"integrated";
        case GpuPreference::HighPerformance: return L"discrete";
    }
    return L"";
}

bool Profile::Load(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    Ini ini{path};
    name = ini.Get(L"Game", L"Name");
    exe = ini.Get(L"Game", L"Exe");
    args = ini.Get(L"Game", L"Args");
    workDir = ini.Get(L"Game", L"WorkDir");
    engine = ini.Get(L"Game", L"Engine", L"generic");

    mode = ParseEnum(ini.Get(L"Display", L"Mode"),
                     {DisplayMode::Borderless, DisplayMode::Windowed, DisplayMode::Passthrough}, mode);
    monitor = ini.GetInt(L"Display", L"Monitor", monitor);
    std::wstring rh = ini.Get(L"Display", L"RenderHeight", L"auto");
    renderHeight = IEquals(rh, L"auto") ? 0 : IEquals(rh, L"native") ? -1 : _wtoi(rh.c_str());
    filter = ParseEnum(ini.Get(L"Display", L"Filter"),
                       {ScaleFilter::Bilinear, ScaleFilter::Bicubic, ScaleFilter::Sharp, ScaleFilter::Nearest},
                       filter);
    aspect = ParseEnum(ini.Get(L"Display", L"Aspect"),
                       {AspectMode::Keep, AspectMode::Stretch, AspectMode::Integer}, aspect);
    windowScale = ini.GetFloat(L"Display", L"WindowScale", windowScale);
    vsync = ini.GetBool(L"Display", L"VSync", vsync);
    fpsLimit = ini.GetInt(L"Display", L"FpsLimit", fpsLimit);
    lowLatency = ini.GetBool(L"Display", L"LowLatency", lowLatency);

    cursor = ParseEnum(ini.Get(L"Cursor", L"Mode"), {CursorMode::System, CursorMode::Game}, cursor);
    cursorScale = ini.GetFloat(L"Cursor", L"Scale", cursorScale);
    confineCursor = ini.GetBool(L"Cursor", L"Confine", confineCursor);

    dpi = ParseEnum(ini.Get(L"Compat", L"Dpi"), {DpiMode::PerMonitor, DpiMode::System, DpiMode::Unaware}, dpi);
    gpu = ParseEnum(ini.Get(L"Compat", L"Gpu"),
                    {GpuPreference::Default, GpuPreference::PowerSaving, GpuPreference::HighPerformance}, gpu);
    followChildren = ini.GetBool(L"Compat", L"FollowChildren", followChildren);
    forceHardwareVP = ini.GetBool(L"Compat", L"ForceHardwareVP", forceHardwareVP);
    flashOcx = ini.Get(L"Compat", L"FlashOcx");
    verboseLog = ini.GetBool(L"Compat", L"VerboseLog", verboseLog);
    return true;
}

bool Profile::Save(const std::wstring& path) const {
    // Create the file with a UTF-16LE BOM first so WritePrivateProfileStringW stores Unicode.
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const unsigned char bom[2] = {0xFF, 0xFE};
        DWORD written = 0;
        WriteFile(h, bom, 2, &written, nullptr);
        CloseHandle(h);
    }
    Ini ini{path};
    bool ok = true;
    ok &= ini.Put(L"Game", L"Name", name);
    ok &= ini.Put(L"Game", L"Exe", exe);
    ok &= ini.Put(L"Game", L"Args", args);
    ok &= ini.Put(L"Game", L"WorkDir", workDir);
    ok &= ini.Put(L"Game", L"Engine", engine);

    ok &= ini.Put(L"Display", L"Mode", ToString(mode));
    ok &= ini.Put(L"Display", L"Monitor", std::to_wstring(monitor));
    ok &= ini.Put(L"Display", L"RenderHeight",
                  renderHeight == 0 ? L"auto" : renderHeight < 0 ? L"native" : std::to_wstring(renderHeight));
    ok &= ini.Put(L"Display", L"Filter", ToString(filter));
    ok &= ini.Put(L"Display", L"Aspect", ToString(aspect));
    ok &= ini.Put(L"Display", L"WindowScale", Num(windowScale));
    ok &= ini.Put(L"Display", L"VSync", vsync ? L"1" : L"0");
    ok &= ini.Put(L"Display", L"FpsLimit", std::to_wstring(fpsLimit));
    ok &= ini.Put(L"Display", L"LowLatency", lowLatency ? L"1" : L"0");

    ok &= ini.Put(L"Cursor", L"Mode", ToString(cursor));
    ok &= ini.Put(L"Cursor", L"Scale", Num(cursorScale));
    ok &= ini.Put(L"Cursor", L"Confine", confineCursor ? L"1" : L"0");

    ok &= ini.Put(L"Compat", L"Dpi", ToString(dpi));
    ok &= ini.Put(L"Compat", L"Gpu", ToString(gpu));
    ok &= ini.Put(L"Compat", L"FollowChildren", followChildren ? L"1" : L"0");
    ok &= ini.Put(L"Compat", L"ForceHardwareVP", forceHardwareVP ? L"1" : L"0");
    ok &= ini.Put(L"Compat", L"FlashOcx", flashOcx);
    ok &= ini.Put(L"Compat", L"VerboseLog", verboseLog ? L"1" : L"0");
    return ok;
}

}  // namespace hog

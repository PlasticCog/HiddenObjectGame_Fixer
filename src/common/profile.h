// Per-game settings shared by the launcher (writes them) and hogfix.dll (reads them).
// Stored as a UTF-16 INI file so game names/paths with any characters survive.
#pragma once
#include <string>

namespace hog {

enum class DisplayMode { Borderless, Windowed, Passthrough };
enum class ScaleFilter { Bilinear, Bicubic, Sharp, Nearest };
enum class AspectMode { Keep, Stretch, Integer };
enum class CursorMode { System, Game };   // System = smooth OS cursor (converted from the game's)
enum class DpiMode { PerMonitor, System, Unaware };
// Which GPU Windows should hand the game, via the per-app graphics preference.
enum class GpuPreference { Default, PowerSaving, HighPerformance };

struct Profile {
    // [Game]
    std::wstring name;
    std::wstring exe;          // absolute path
    std::wstring args;
    std::wstring workDir;      // empty = exe folder
    std::wstring engine;       // "sf", "lakehouse", "neoaxis", "flash", "generic"

    // [Display]
    DisplayMode mode = DisplayMode::Borderless;
    int monitor = 0;           // 0 = primary, N = Nth monitor (1-based in EnumDisplayMonitors order)
    int renderHeight = 0;      // virtual desktop height shown to the game; 0 = auto, -1 = native
    ScaleFilter filter = ScaleFilter::Bicubic;
    AspectMode aspect = AspectMode::Keep;
    float windowScale = 0.0f;  // windowed mode: 0 = largest that fits the work area
    bool vsync = true;
    int fpsLimit = 0;          // 0 = off
    bool lowLatency = true;    // keep at most one frame queued on the GPU

    // [Cursor]
    CursorMode cursor = CursorMode::System;
    float cursorScale = 0.0f;  // 0 = follow the game's upscale factor
    bool confineCursor = false;

    // [Compat]
    DpiMode dpi = DpiMode::PerMonitor;
    GpuPreference gpu = GpuPreference::Default;
    bool followChildren = true;
    bool forceHardwareVP = false;
    std::wstring flashOcx;     // path to a Flash ActiveX OCX for Flash-hosting games
    bool verboseLog = false;

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
};

const wchar_t* ToString(DisplayMode v);
const wchar_t* ToString(ScaleFilter v);
const wchar_t* ToString(AspectMode v);
const wchar_t* ToString(CursorMode v);
const wchar_t* ToString(DpiMode v);
const wchar_t* ToString(GpuPreference v);

// Environment variable through which the loader hands the profile path to hogfix.dll.
inline constexpr wchar_t kProfileEnvVar[] = L"HOGFIX_PROFILE";
inline constexpr wchar_t kLogEnvVar[] = L"HOGFIX_LOG";

}  // namespace hog

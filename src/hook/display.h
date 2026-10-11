// Virtual display: the resolution/monitor layout the game believes it is running on.
// Games that ask for "the desktop resolution" or switch display modes get a virtual mode,
// which we later upscale onto the real monitor. The real display mode is never changed.
#pragma once
#include <windows.h>

#include <vector>

namespace hog::display {

struct Monitor {
    HMONITOR handle = nullptr;
    RECT rect{};   // physical pixels
    RECT work{};
    int refresh = 60;
    wchar_t device[32] = L"";
};

struct Mode {
    int width, height, bpp, refresh;
};

void Init();
void InstallHooks();

const Monitor& Target();
SIZE NativeSize();
SIZE VirtualSize();
// The default virtual size from the profile (what "the desktop" looks like before any mode change).
SIZE DefaultVirtualSize();
void SetVirtualSize(int w, int h, const char* why);
void RestoreVirtualSize(const char* why);
// Mode list shown to the game (deduped, capped so that "pick the biggest" picks something cheap).
const std::vector<Mode>& ModeList();
// Re-reads monitor geometry (e.g. after WM_DISPLAYCHANGE).
void RefreshMonitor();

}  // namespace hog::display

// Window management: the game's top-level window is placed and sized by us, borderless or
// windowed on the chosen monitor, with its client area kept at the virtual resolution.
#pragma once
#include <windows.h>

namespace hog::window {

void Init();
void InstallHooks();

// Called by display.cpp when the virtual desktop size changes.
void OnVirtualDisplayChanged(int w, int h, int bpp);

// The game's main render window, once identified.
HWND GameWindow();
// Maps a point in real client pixels to virtual client pixels (for cursor/coordinate fixups).
void RealToVirtual(POINT& p);
void VirtualToReal(POINT& p);
// The rectangle (in the window's real client space) that the virtual image is drawn into.
RECT Viewport();
void SetViewport(const RECT& r);

}  // namespace hog::window

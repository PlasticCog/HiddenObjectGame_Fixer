// Cursor smoothing.
//
// These games fall into two camps:
//  (1) They let the OS draw the arrow (SetCursor/LoadCursor) but call SetCursorPos every frame
//      to clamp/warp it. At 4K on a weak iGPU that fights the real pointer and feels laggy.
//  (2) They hide the OS cursor and draw a sprite in D3D, so the visible pointer only moves as
//      fast as the game renders (stutter, trails).
//
// For (1) we drop the game's redundant SetCursorPos calls and let Windows move the hardware
// cursor at full rate. For (2) the D3D layer captures the cursor sprite texture, we build an
// HCURSOR from it and show it via the OS, and we suppress the in-engine sprite draw. Either way
// the visible pointer becomes a true hardware cursor decoupled from the game's frame rate.
#pragma once
#include <windows.h>

namespace hog::cursor {

void Init();
void InstallHooks();

// Called by the D3D layer when it has captured the game's cursor sprite (BGRA, top-down).
// hotspot is in texture pixels. Thread-safe; the actual SetCursor happens on the window thread.
void SubmitSprite(const void* bgra, int w, int h, int hotspotX, int hotspotY, unsigned long long key);

// The game asked to show/hide its software cursor; mirror that to the OS cursor.
void SetGameCursorVisible(bool visible);

// True while we are serving a system cursor converted from the game's sprite (camp 2).
bool UsingConvertedCursor();

// Pump: re-assert our cursor if something stole it. Call once per presented frame.
void OnPresent();

}  // namespace hog::cursor

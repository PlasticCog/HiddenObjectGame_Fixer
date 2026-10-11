// Direct3D9 interception: the perf and scaling core.
//
// The game is steered (via the display virtualization) into creating a small back buffer, which
// it renders its scene into cheaply. We force windowed/borderless presentation and stretch that
// small buffer up to the real monitor with aspect-correct letterboxing, so a weak iGPU fills far
// fewer pixels per frame than native 4K. We also control vsync, frame latency and an optional FPS
// cap here, and expose a hook the engine shims use to lift the software cursor out of the frame.
#pragma once
#include <windows.h>

struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;

namespace hog::d3d9 {

void InstallHooks();   // hooks Direct3DCreate9 / Direct3DCreate9Ex

IDirect3DDevice9* Device();   // the active device, or null

// --- cursor sprite capture (used by engine shims) --------------------------------------------
// While capture is active, the next textured quad the game draws is treated as its mouse cursor:
// the bound texture sub-rect is copied out (for cursor::SubmitSprite) and the draw is suppressed,
// so the sprite never gets baked into the frame and we can show a crisp hardware cursor instead.
void BeginCursorCapture();
void EndCursorCapture();
bool CursorCaptureActive();

}  // namespace hog::d3d9

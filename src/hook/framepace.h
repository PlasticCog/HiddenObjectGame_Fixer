// Frame pacing: optional FPS cap and a one-frame GPU latency limiter, driven from Present.
#pragma once
struct IDirect3DDevice9;

namespace hog::framepace {

void Init();
void BeforePresent();                 // enforce the FPS cap (sleep until the next frame is due)
void AfterPresent(IDirect3DDevice9*);  // flush so the GPU keeps at most one frame queued
void OnDeviceReset(IDirect3DDevice9*);  // (re)create the latency query for the given device

}  // namespace hog::framepace

#include "framepace.h"

#include <windows.h>
#include <mmsystem.h>
#include <d3d9.h>

#include "hogfix.h"
#include "log.h"

#pragma comment(lib, "winmm.lib")

namespace hog::framepace {
namespace {

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_nextFrame{};
bool g_haveNext = false;
IDirect3DQuery9* g_flushQuery = nullptr;
bool g_timerRaised = false;

double Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / g_freq.QuadPart;
}

}  // namespace

void Init() {
    QueryPerformanceFrequency(&g_freq);
    if (g_profile.fpsLimit > 0 || g_profile.lowLatency) {
        timeBeginPeriod(1);
        g_timerRaised = true;
    }
}

void BeforePresent() {
    if (g_profile.fpsLimit <= 0) return;
    double period = 1.0 / g_profile.fpsLimit;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_haveNext) {
        g_nextFrame = now;
        g_haveNext = true;
    }
    double target = double(g_nextFrame.QuadPart) / g_freq.QuadPart + period;
    double cur = double(now.QuadPart) / g_freq.QuadPart;
    double wait = target - cur;
    if (wait > 0) {
        // Sleep most of it, then spin the last ~1.5 ms for an accurate cap.
        if (wait > 0.002) Sleep(DWORD((wait - 0.0015) * 1000.0));
        while (Now() < target) YieldProcessor();
    }
    QueryPerformanceCounter(&g_nextFrame);
    // If we fell badly behind (e.g. after a stall), resync to avoid a burst of catch-up frames.
    if (cur > target + period) g_nextFrame = now;
}

void AfterPresent(IDirect3DDevice9* dev) {
    if (!g_profile.lowLatency || !dev) return;
    if (!g_flushQuery) return;
    // Force the CPU to wait until the GPU has finished this frame, so no more than one frame is
    // ever queued ahead. On a slow iGPU this is what keeps the cursor and clicks feeling attached.
    if (SUCCEEDED(g_flushQuery->Issue(D3DISSUE_END))) {
        while (g_flushQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) YieldProcessor();
    }
}

void OnDeviceReset(IDirect3DDevice9* dev) {
    if (g_flushQuery) {
        g_flushQuery->Release();
        g_flushQuery = nullptr;
    }
    if (dev && g_profile.lowLatency) {
        if (FAILED(dev->CreateQuery(D3DQUERYTYPE_EVENT, &g_flushQuery))) g_flushQuery = nullptr;
    }
}

}  // namespace hog::framepace

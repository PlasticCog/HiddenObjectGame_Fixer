#include "engine_shim.h"

#include <windows.h>

#include "cursor.h"
#include "hogfix.h"
#include "hooks.h"
#include "log.h"

// Per-engine fixes. The generic layers (display/window/cursor/d3d9/framepace) already fix scaling,
// windowing, coordinate mapping, vsync, frame latency and CPU affinity for every game. What is left
// engine-specific is the software cursor: these engines hide the OS cursor and draw their own sprite
// into the frame, so the visible pointer only moves at the game's frame rate (choppy). Each shim
// locates that engine's cursor-draw routine and suppresses it, then cursor::ForceSystemCursor makes
// the OS draw a true hardware cursor that moves at mouse rate regardless of FPS.

namespace hog::engineshim {
namespace {

DWORD MainTimeDateStamp() {
    HMODULE m = GetModuleHandleW(nullptr);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(m) + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

// Converts a preferred-base (0x400000) VA to a runtime pointer (these exes have no ASLR, but be safe).
void* Va(uintptr_t preferredVa) {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return reinterpret_cast<void*>(base + (preferredVa - 0x400000));
}

bool BytesMatch(void* at, const unsigned char* sig, size_t n) {
    __try {
        return memcmp(at, sig, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// --- sf / Alawar "Quest" engine (Kronville / Panopticon / Mexicana / Cruel Games) ------------
// The cursor sprite is drawn by DrawCursor(this, CRenderer*) (thiscall, ret 4), the last thing
// before Present. Suppressing it removes the choppy software cursor; we then force a smooth OS one.
struct SfBuild {
    DWORD timeDateStamp;
    const char* name;
    uintptr_t drawCursor;
    unsigned char guard[8];
};

const SfBuild kSfBuilds[] = {
    {0x55A78FBA, "Kronville", 0x4EA660, {0x83, 0xEC, 0x38, 0x53, 0x8B, 0xD9, 0x56, 0x80}},
    {0x5370DFC5, "Panopticon", 0x4E7610, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00}},
    {0x5292D99B, "Mexicana", 0x4FE470, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70, 0x89, 0x4D}},
    {0x5063FC9F, "CruelGames", 0x4FFBC0, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70, 0x89, 0x4D}},
};

// Trampoline for the suppressed DrawCursor (kept by MinHook; never called — we drop the draw).
void(__fastcall* g_origDrawCursor)(void*, void*, void*) = nullptr;
void __fastcall H_DrawCursor(void* /*this*/, void* /*edx*/, void* /*renderer*/) {
    // Draw nothing: the cursor is served as a hardware OS cursor instead.
}

bool ApplySfEngine() {
    DWORD ts = MainTimeDateStamp();
    const SfBuild* build = nullptr;
    for (const auto& b : kSfBuilds)
        if (b.timeDateStamp == ts) {
            build = &b;
            break;
        }
    if (!build) {
        LOGW("engineshim(sf): unrecognized build (TimeDateStamp 0x%08lX); leaving software cursor as-is", ts);
        return false;
    }
    void* draw = Va(build->drawCursor);
    if (!BytesMatch(draw, build->guard, sizeof(build->guard))) {
        LOGW("engineshim(sf): %s DrawCursor signature mismatch @%p; not suppressing", build->name, draw);
        return false;
    }
    if (!hooks::At(draw, H_DrawCursor, &g_origDrawCursor, "sf::DrawCursor")) return false;
    cursor::ForceSystemCursor(true);
    LOGI("engineshim(sf): %s cursor sprite suppressed, smooth hardware cursor active", build->name);
    return true;
}

}  // namespace

void Apply() {
    const std::wstring& e = g_profile.engine;
    bool wantSystemCursor = g_profile.cursor == CursorMode::System && Virtualizing();

    if (e == L"sf") {
        if (wantSystemCursor)
            ApplySfEngine();
        else
            LOGI("engineshim(sf): cursor=game, leaving the engine's own cursor");
    } else if (e == L"lakehouse") {
        LOGI("engineshim: lakehouse (cursor handled via config pre-seed in the loader)");
    } else if (e == L"neoaxis") {
        LOGI("engineshim: neoaxis uses an OS cursor already; generic latency fix applies");
    } else if (e == L"flash") {
        LOGI("engineshim: flash host (passthrough / separate compat module)");
    } else {
        LOGI("engineshim: generic engine, no engine-specific shim");
    }
}

}  // namespace hog::engineshim

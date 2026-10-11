#include "engine_shim.h"

#include <windows.h>

#include "d3d9_layer.h"
#include "hogfix.h"
#include "hooks.h"
#include "log.h"
#include "sigscan.h"

// Per-engine cursor handling. The generic layers (display/window/cursor/d3d9) already fix scaling,
// windowing, coordinate mapping and frame pacing for every game. What remains engine-specific is
// how each engine draws its *software* cursor, so we can lift it out of the frame and show a crisp
// hardware cursor instead. Each shim finds that engine's cursor-draw routine by byte pattern and
// brackets it with d3d9::BeginCursorCapture()/EndCursorCapture().
//
// Signatures are filled in from the reverse-engineering notes under research/notes/. Until a
// signature is confirmed for a game, that game still gets the full generic treatment; only the
// sprite->hardware cursor conversion waits on the shim.

namespace hog::engineshim {
namespace {

// Wraps a cursor-draw function: capture active for its whole body.
using VoidThunk = void(__fastcall*)(void*, void*);  // most sf:: methods are thiscall/fastcall-ish
VoidThunk g_sfCursorReal = nullptr;

void ApplySfEngine() {
    // The "sf::" Alawar engine (Kronville/Panopticon/Mexicana/CruelGames). The cursor-draw hook
    // point differs per build; signatures live in research/notes/sf_engine.md. Placeholder until
    // confirmed — the generic cursor path (swallowing per-frame SetCursorPos) already helps here.
    LOGI("engineshim: sf engine selected (generic cursor path active; sprite capture pending sig)");
}

void ApplyLakeHouse() {
    LOGI("engineshim: lakehouse selected (generic path active; sprite capture pending sig)");
}

void ApplyNeoAxis() {
    // NeoAxis/EMDM uses the OS cursor already; nothing engine-specific needed beyond the generic
    // display/window virtualization.
    LOGI("engineshim: neoaxis selected (OS cursor; generic path)");
}

void ApplyFlash() {
    LOGI("engineshim: flash host selected (handled by the flash compat module)");
}

}  // namespace

void Apply() {
    const std::wstring& e = g_profile.engine;
    if (e == L"sf")
        ApplySfEngine();
    else if (e == L"lakehouse")
        ApplyLakeHouse();
    else if (e == L"neoaxis")
        ApplyNeoAxis();
    else if (e == L"flash")
        ApplyFlash();
    else
        LOGI("engineshim: generic engine, no engine-specific shim");
}

}  // namespace hog::engineshim

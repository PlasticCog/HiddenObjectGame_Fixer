// Per-engine fixes: byte-pattern scans that find the engine's cursor-draw / mode-select code in
// the main module and hook it, so the generic D3D/window/cursor layers behave correctly for that
// specific game. Selected by Profile::engine. Safe no-op when nothing matches.
#pragma once

namespace hog::engineshim {

void Apply();   // called once, after the generic hooks are installed and the main module is mapped

}  // namespace hog::engineshim

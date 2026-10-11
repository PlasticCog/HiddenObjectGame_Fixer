// Follows the fix into child processes: some games (e.g. the NeoAxis "EMDM" title) are a small
// launcher that spawns the real game as a child, and the bundle menu launches every game as a
// child. When Profile.followChildren is set we hook CreateProcess* so each child is created
// suspended, injected with hogfix.dll (inheriting the same profile via the environment), then
// resumed. Only children launched from the game's own directory tree are followed, to avoid
// injecting into unrelated helpers.
#pragma once

namespace hog::childinject {

void InstallHooks();

}  // namespace hog::childinject

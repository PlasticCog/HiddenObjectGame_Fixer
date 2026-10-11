// Process-wide state of the injected fix layer.
#pragma once
#include <windows.h>

#include <string>

#include "profile.h"

namespace hog {

extern Profile g_profile;
extern std::wstring g_dllDir;     // folder containing hogfix.dll
extern std::wstring g_dllPath;
extern std::wstring g_profilePath;
extern HMODULE g_module;

inline bool Virtualizing() { return g_profile.mode != DisplayMode::Passthrough; }

// True when the return address belongs to the game (not Windows or a driver), which is how we
// tell the game's own window/API calls apart from the D3D runtime's internal ones.
bool CalledFromGame(void* returnAddress);

std::string Narrow(const std::wstring& w);
std::wstring Widen(const char* s);

}  // namespace hog

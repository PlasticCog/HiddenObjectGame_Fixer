// Thin typed wrappers over MinHook.
#pragma once
#include <windows.h>

#include "log.h"

namespace hog::hooks {

bool Init();
// Hooks `target`, storing the trampoline in `*original`. Enabled immediately.
bool Install(void* target, void* detour, void** original, const char* name);
// Hooks an export by module + name. Fails quietly (logged) when the export is missing.
bool InstallApi(const wchar_t* module, const char* proc, void* detour, void** original);

template <class F>
bool Api(const wchar_t* module, const char* proc, F detour, F* original) {
    return InstallApi(module, proc, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}
template <class F>
bool At(void* target, F detour, F* original, const char* name) {
    return Install(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), name);
}

// Returns the function at vtable slot `index` of COM object `obj`.
inline void* VtableEntry(void* obj, int index) { return (*reinterpret_cast<void***>(obj))[index]; }

}  // namespace hog::hooks

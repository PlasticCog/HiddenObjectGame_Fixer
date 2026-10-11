#include "hooks.h"

#include <MinHook.h>

namespace hog::hooks {

bool Init() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        LOGE("MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    return true;
}

bool Install(void* target, void* detour, void** original, const char* name) {
    if (!target) {
        LOGW("hook %s: no target", name);
        return false;
    }
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_ERROR_ALREADY_CREATED) return true;
    if (s != MH_OK) {
        LOGE("hook %s @%p: create failed: %s", name, target, MH_StatusToString(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        LOGE("hook %s @%p: enable failed: %s", name, target, MH_StatusToString(s));
        return false;
    }
    LOGV("hooked %s @%p", name, target);
    return true;
}

bool InstallApi(const wchar_t* module, const char* proc, void* detour, void** original) {
    HMODULE m = GetModuleHandleW(module);
    if (!m) m = LoadLibraryW(module);
    void* target = m ? reinterpret_cast<void*>(GetProcAddress(m, proc)) : nullptr;
    if (!target) {
        LOGW("hook %s: export not found", proc);
        return false;
    }
    return Install(target, detour, original, proc);
}

}  // namespace hog::hooks

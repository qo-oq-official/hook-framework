#include "com/com_hook_engine.h"
#include "com/com_registry.h"
#include "logging/logger.h"

namespace hookfw {

ComHookEngine& ComHookEngine::instance() {
    static ComHookEngine inst;
    return inst;
}

void ComHookEngine::registerThunk(const std::string& iid, int vtableIndex, void* thunk) {
    std::lock_guard lock(mutex_);
    thunks_[iid][vtableIndex] = thunk;
}

bool ComHookEngine::patchSlot(void** vtable, int index, void* thunk, void** outOriginal) {
    if (!vtable || !thunk) return false;

    void* original = vtable[index];
    if (!original) return false;

    DWORD oldProtect;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        LOG_ERROR("ComHookEngine: VirtualProtect failed for vtable[{:d}]", index);
        return false;
    }

    *outOriginal = original;
    vtable[index] = thunk;

    DWORD ignored;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &ignored);
    return true;
}

void ComHookEngine::unpatchSlot(void** vtable, int index, void* original) {
    if (!vtable || !original) return;

    DWORD oldProtect;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect))
        return;

    vtable[index] = original;
    DWORD ignored;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &ignored);
}

int ComHookEngine::patchInterface(void* pInterface, const std::string& iid) {
    if (!pInterface) return 0;
    void** vtable = *reinterpret_cast<void***>(pInterface);
    if (!vtable) return 0;

    auto it = thunks_.find(iid);
    if (it == thunks_.end()) return 0;

    std::lock_guard lock(mutex_);
    int patched = 0;
    for (const auto& [index, thunk] : it->second) {
        void* original = nullptr;
        if (patchSlot(vtable, index, thunk, &original)) {
            patched_.push_back({pInterface, index, original, true});
            patched++;
            LOG_INFO("ComHookEngine: patched {} vtable[{}]", iid, index);
        }
    }
    return patched;
}

void ComHookEngine::unpatchInterface(void* pInterface) {
    if (!pInterface) return;
    void** vtable = *reinterpret_cast<void***>(pInterface);
    if (!vtable) return;

    std::lock_guard lock(mutex_);
    auto it = patched_.begin();
    while (it != patched_.end()) {
        if (it->interfacePtr == pInterface) {
            unpatchSlot(vtable, it->vtableIndex, it->originalFunc);
            LOG_INFO("ComHookEngine: unpatched vtable[{}]", it->vtableIndex);
            it = patched_.erase(it);
        } else {
            ++it;
        }
    }
}

void ComHookEngine::unpatchAll() {
    std::lock_guard lock(mutex_);
    for (auto& slot : patched_) {
        if (!slot.interfacePtr || !slot.patched) continue;
        void** vtable = *reinterpret_cast<void***>(slot.interfacePtr);
        unpatchSlot(vtable, slot.vtableIndex, slot.originalFunc);
        slot.patched = false;
    }
    patched_.clear();
}

}  // namespace hookfw

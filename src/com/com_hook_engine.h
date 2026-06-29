#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace hookfw {

struct PatchedVtableSlot {
    void* interfacePtr;
    int vtableIndex;
    void* originalFunc;
    bool patched;
};

class ComHookEngine {
public:
    static ComHookEngine& instance();

    // Patch all configured methods on an interface pointer
    int patchInterface(void* pInterface, const std::string& iid);
    
    // Unpatch a specific interface instance
    void unpatchInterface(void* pInterface);
    
    // Unpatch all
    void unpatchAll();

    // Register a client-provided thunk function for a given IID + vtable index
    void registerThunk(const std::string& iid, int vtableIndex, void* thunk);

private:
    ComHookEngine() = default;
    
    bool patchSlot(void** vtable, int index, void* thunk, void** outOriginal);
    void unpatchSlot(void** vtable, int index, void* original);

    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_map<int, void*>> thunks_;  // IID → index → thunk
    std::vector<PatchedVtableSlot> patched_;
};

}  // namespace hookfw

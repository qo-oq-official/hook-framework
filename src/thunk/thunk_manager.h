#pragma once
#include "thunk/hook_context.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>

namespace hookfw {

class ThunkManager {
public:
    static ThunkManager& instance();

    struct ThunkSlot {
        ThunkData data;
        void* executableStub = nullptr;
        uint32_t id = 0;
    };

    ThunkSlot* allocateThunk(uint32_t hookId,
                             void* originalFunc,
                             void* replacementFunc,
                             bool callOriginal);

    ThunkSlot* allocateLightThunk(uint32_t hookId, void* originalFunc);

    void freeThunk(ThunkSlot* slot);
    void freeAll();

private:
    ThunkManager() = default;
    ~ThunkManager();

    void* generateStub(ThunkData* data);

    std::mutex mutex_;
    std::vector<std::unique_ptr<ThunkSlot>> slots_;
};

}

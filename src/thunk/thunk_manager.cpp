#include "thunk/thunk_manager.h"
#include "logging/logger.h"
#include <Windows.h>

extern "C" void hookfw_thunk_entry();

namespace hookfw {

ThunkManager& ThunkManager::instance() {
    static ThunkManager inst;
    return inst;
}

ThunkManager::~ThunkManager() {
    freeAll();
}

ThunkManager::ThunkSlot* ThunkManager::allocateThunk(
    uint32_t hookId, void* originalFunc, void* replacementFunc, bool callOriginal) {

    std::lock_guard lock(mutex_);

    auto slot = std::make_unique<ThunkSlot>();
    slot->id = hookId;
    slot->data.hookId = hookId;
    slot->data.padding0 = 0;
    slot->data.originalFunction = originalFunc;
    slot->data.trampolineFunction = originalFunc;
    slot->data.replacementFunction = replacementFunc;
    slot->data.callOriginal = callOriginal ? 1 : 0;
    slot->data.preCallback = nullptr;
    slot->data.postCallback = nullptr;
    std::memset(slot->data.padding1, 0, sizeof(slot->data.padding1));

    slot->executableStub = generateStub(&slot->data);
    if (!slot->executableStub) {
        LOG_ERROR("Failed to generate executable stub for hook {}", hookId);
        return nullptr;
    }

    auto* ptr = slot.get();
    slots_.push_back(std::move(slot));

    LOG_INFO("Allocated thunk slot id={} originalFunc={:#x}", hookId,
             reinterpret_cast<uintptr_t>(originalFunc));
    return ptr;
}

void* ThunkManager::generateStub(ThunkData* data) {
    // Generate:
    //   mov rax, <ThunkData*>         ; 48 B8 <8 bytes>     = 10 bytes
    //   jmp [rip+0]                   ; FF 25 00 00 00 00   = 6 bytes
    //   <address of thunk_entry>      ; 8 bytes
    // Total: 24 bytes

    constexpr size_t stubSize = 64;
    void* mem = VirtualAlloc(nullptr, stubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) {
        LOG_ERROR("VirtualAlloc failed for stub, error={}", GetLastError());
        return nullptr;
    }

    uint8_t* code = static_cast<uint8_t*>(mem);

    // mov rax, imm64
    code[0] = 0x48;
    code[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(code + 2) = reinterpret_cast<uint64_t>(data);

    // jmp [rip+0]
    code[10] = 0xFF;
    code[11] = 0x25;
    *reinterpret_cast<uint32_t*>(code + 12) = 0;
    *reinterpret_cast<uint64_t*>(code + 16) = reinterpret_cast<uint64_t>(&hookfw_thunk_entry);

    DWORD oldProtect;
    VirtualProtect(mem, stubSize, PAGE_EXECUTE_READ, &oldProtect);

    return mem;
}

void ThunkManager::freeThunk(ThunkSlot* slot) {
    std::lock_guard lock(mutex_);
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->get() == slot) {
            if (slot->executableStub) {
                VirtualFree(slot->executableStub, 0, MEM_RELEASE);
            }
            slots_.erase(it);
            LOG_INFO("Freed thunk slot id={}", slot->id);
            return;
        }
    }
    LOG_WARN("Attempted to free unknown thunk slot");
}

void ThunkManager::freeAll() {
    std::lock_guard lock(mutex_);
    for (auto& slot : slots_) {
        if (slot->executableStub) {
            VirtualFree(slot->executableStub, 0, MEM_RELEASE);
        }
    }
    slots_.clear();
}

}

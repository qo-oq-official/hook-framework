#include "thunk/thunk_manager.h"
#include "logging/logger.h"
#include <Windows.h>
#include <format>

extern "C" void hookfw_universal_entry();

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

    slot->executableStub = generateStub(&slot->data, &hookfw_universal_entry);
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

/**
 * @brief 生成一段动态可执行的机器码跳板(Thunk Stub)，用于跳转至目标入口函数，并携带自定义Thunk上下文数据
 * @param data 指向Thunk上下文数据结构体 ThunkData 的指针，会硬编码写入生成的机器码中
 * @param entryFunc 跳板最终要跳转执行的目标函数地址
 * @return void*  返回新分配的可执行内存起始地址；分配失败返回 nullptr
 *
 * @note 运行环境：Windows x64 平台，仅适配64位程序，依赖Windows内存API
 * @note 生成的x64汇编指令对应机器码：
 *  0: 48 B8 [8字节data指针]      mov rax, data        // 将ThunkData*载入rax寄存器
 *  A: FF 25 00 00 00 00          jmp qword ptr [rip+0]
 * 10: [8字节entryFunc地址]       // jmp间接寻址的目标函数地址
 *
 * 完整汇编示意：
 * mov rax, ThunkData指针
 * jmp qword ptr [rip]
 * .dq entryFunc
 *
 * @attention 内存分配大小固定64字节，预留充足冗余空间；
 *            初始权限 PAGE_EXECUTE_READWRITE 用于写入机器码，写入完成后收紧为 PAGE_EXECUTE_READ，移除写权限，符合内存保护最佳实践
 * @attention 调用方职责：
 *            1. 不再使用该stub时，必须调用 VirtualFree 释放内存，否则内存泄漏
 *            2. 保证 ThunkData 对象生命周期长于本stub，机器码内保存裸指针，无自动生命周期管理
 * @warning 禁止在非x64 Windows平台调用；禁止直接修改返回内存内代码；不要跨进程传递此stub地址
 */
void* ThunkManager::generateStub(ThunkData* data, void* entryFunc) {
    constexpr size_t stubSize = 64;
    void* mem = VirtualAlloc(nullptr, stubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) {
        LOG_ERROR("VirtualAlloc failed for stub, error={}", GetLastError());
        return nullptr;
    }

    uint8_t* code = static_cast<uint8_t*>(mem);

    // mov rax, data    48 B8 <64位立即数>
    code[0] = 0x48;
    code[1] = 0xB8;
    // 将 data 指针作为 64 位立即数写入 code[2] ~ code[9]
    *reinterpret_cast<uint64_t*>(code + 2) = reinterpret_cast<uint64_t>(data);

    // jmp qword ptr [rip+0]    FF 25 00 00 00 00
    code[10] = 0xFF;
    code[11] = 0x25;
    *reinterpret_cast<uint32_t*>(code + 12) = 0;
    // rip相对间接跳转指向此处，保存目标入口函数地址
    *reinterpret_cast<uint64_t*>(code + 16) = reinterpret_cast<uint64_t>(entryFunc);

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

ThunkManager::ThunkSlot* ThunkManager::allocateLightThunk(uint32_t hookId, void* originalFunc) {
    std::lock_guard lock(mutex_);

    auto slot = std::make_unique<ThunkSlot>();
    slot->id = hookId;
    slot->data.hookId = hookId;
    slot->data.padding0 = 0;
    slot->data.originalFunction = originalFunc;
    slot->data.trampolineFunction = originalFunc;
    slot->data.replacementFunction = nullptr;
    slot->data.callOriginal = 1;
    slot->data.preCallback = nullptr;
    slot->data.postCallback = nullptr;
    std::memset(slot->data.padding1, 0, sizeof(slot->data.padding1));

    slot->executableStub = generateStub(&slot->data, &hookfw_universal_entry);
    if (!slot->executableStub) {
        LOG_ERROR("Failed to generate light stub for hook {}", hookId);
        return nullptr;
    }

    auto* ptr = slot.get();
    slots_.push_back(std::move(slot));
    return ptr;
}

void ThunkManager::freeAll() {
    std::lock_guard lock(mutex_);
    for (auto& slot : slots_) {
        if (slot->executableStub) {
            VirtualFree(slot->executableStub, 0, MEM_RELEASE);
        }
    }
    slots_.clear();
    trampolineMap_.clear();
}

void ThunkManager::registerTrampoline(uintptr_t trampolineBase, uintptr_t originalFunction) {
    std::lock_guard lock(mutex_);
    trampolineMap_.emplace_back(trampolineBase, originalFunction);
    LOG_INFO("Registered trampoline {:#x} -> original {:#x}", trampolineBase, originalFunction);
}

bool ThunkManager::resolveTrampoline(uintptr_t addr, uintptr_t* outOriginal) const {
    if (!addr || !outOriginal) return false;
    std::lock_guard lock(mutex_);
    // The Detours trampoline code is at most rbCode[30] + jmp (~14 bytes); a
    // 64-byte window from the code base safely contains any return address.
    constexpr uintptr_t kTrampolineWindow = 64;
    for (const auto& [base, original] : trampolineMap_) {
        if (addr >= base && addr < base + kTrampolineWindow) {
            *outOriginal = original + (addr - base);
            return true;
        }
    }
    return false;
}

}

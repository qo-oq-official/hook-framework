#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <format>
#include <Windows.h>
#include <unordered_map>
#include <mutex>

namespace hookfw {

#pragma pack(push, 1)
struct HookContext {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
    uint64_t returnAddress;
    alignas(16) uint8_t xmm0[16];
    alignas(16) uint8_t xmm1[16];
    alignas(16) uint8_t xmm2[16];
    alignas(16) uint8_t xmm3[16];
    alignas(16) uint8_t xmm4[16];
    alignas(16) uint8_t xmm5[16];
    uint8_t stackSnapshot[512];
    uint32_t stackSnapshotSize;
    uint32_t hookId;

    uint32_t stackFrameCount = 0;
    uint64_t stackFrames[64] = {};
    uint64_t archiveBufPosBefore = 0;

    void captureCallStack(uint32_t maxFrames = 64) {
        stackFrameCount = 0;

        // Frame 0: direct return address (captured by dispatch)
        if (returnAddress > 0x10000)
            stackFrames[stackFrameCount++] = returnAddress;

        // Scan remaining stack slots (from trampoline RSP upward)
        uintptr_t rspTop = rsp;
        for (int slot = 0; slot < 256 && stackFrameCount < maxFrames; slot++) {
            uintptr_t* pSlot = reinterpret_cast<uintptr_t*>(rspTop + slot * 8);

            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(pSlot, &mbi, sizeof(mbi))) break;
            if (mbi.State != MEM_COMMIT) break;
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) break;

            uintptr_t candidate = *pSlot;
            if (candidate < 0x10000) continue;

            MEMORY_BASIC_INFORMATION cmbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(candidate), &cmbi, sizeof(cmbi))) continue;
            if (cmbi.Type != MEM_IMAGE) continue;

            bool dup = false;
            for (uint32_t j = 0; j < stackFrameCount; j++) {
                if (stackFrames[j] == candidate) { dup = true; break; }
            }
            if (dup) continue;

            stackFrames[stackFrameCount++] = candidate;
        }
    }

    static std::string resolveFrame(uint64_t addr) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
            return std::format("{:#x}", addr);

        static std::mutex s_cacheMutex;
        static std::unordered_map<void*, std::string> s_nameCache;

        if (mbi.Type == MEM_IMAGE && mbi.AllocationBase) {
            std::string name;
            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                auto it = s_nameCache.find(mbi.AllocationBase);
                if (it != s_nameCache.end()) {
                    name = it->second;
                }
            }
            if (name.empty()) {
                wchar_t path[MAX_PATH];
                if (GetModuleFileNameW(reinterpret_cast<HMODULE>(mbi.AllocationBase), path, MAX_PATH)) {
                    std::wstring_view wp(path);
                    auto pos = wp.rfind(L'\\');
                    std::wstring_view fname = (pos != std::wstring_view::npos)
                        ? std::wstring_view(wp.begin() + pos + 1, wp.end())
                        : wp;
                    int len = WideCharToMultiByte(CP_UTF8, 0, fname.data(), (int)fname.size(),
                                                  nullptr, 0, nullptr, nullptr);
                    name.resize(len);
                    if (len > 0)
                        WideCharToMultiByte(CP_UTF8, 0, fname.data(), (int)fname.size(),
                                            &name[0], len, nullptr, nullptr);
                    std::lock_guard<std::mutex> lock(s_cacheMutex);
                    s_nameCache[mbi.AllocationBase] = name;
                }
            }
            uint64_t rva = addr - reinterpret_cast<uint64_t>(mbi.AllocationBase);
            return std::format("{}+0x{:08x}", name, rva);
        }
        return std::format("{:#x}", addr);
    }

    template<typename T>
    T arg(int index) const {
        static_assert(sizeof(T) <= 8, "arg<T> only supports types <= 8 bytes");
        uint64_t val = 0;
        switch (index) {
            case 0: val = rcx; break;
            case 1: val = rdx; break;
            case 2: val = r8;  break;
            case 3: val = r9;  break;
            default: {
                int stackOffset = (index - 4) * 8 + 32;
                if (stackOffset + 8 <= static_cast<int>(stackSnapshotSize)) {
                    std::memcpy(&val, stackSnapshot + stackOffset, 8);
                }
                break;
            }
        }
        T result{};
        std::memcpy(&result, &val, sizeof(T));
        return result;
    }

    template<typename T>
    T returnValue() const {
        T result{};
        std::memcpy(&result, &rax, sizeof(T));
        return result;
    }

    template<typename T>
    void setArg(int index, T value) {
        static_assert(sizeof(T) <= 8, "setArg<T> only supports types <= 8 bytes");
        uint64_t val = 0;
        std::memcpy(&val, &value, sizeof(T));
        switch (index) {
            case 0: rcx = val; break;
            case 1: rdx = val; break;
            case 2: r8  = val; break;
            case 3: r9  = val; break;
            default: {
                int stackOffset = (index - 4) * 8 + 32;
                if (stackOffset + 8 <= static_cast<int>(stackSnapshotSize)) {
                    std::memcpy(stackSnapshot + stackOffset, &val, 8);
                }
                break;
            }
        }
    }

    template<typename T>
    void setReturnValue(T value) {
        std::memcpy(&rax, &value, sizeof(T));
    }

    float argFloat(int index) const {
        float val = 0.0f;
        const uint8_t* xmmSlot = nullptr;
        switch (index) {
            case 0: xmmSlot = xmm0; break;
            case 1: xmmSlot = xmm1; break;
            case 2: xmmSlot = xmm2; break;
            case 3: xmmSlot = xmm3; break;
            default: return 0.0f;
        }
        std::memcpy(&val, xmmSlot, sizeof(float));
        return val;
    }

    double argDouble(int index) const {
        double val = 0.0;
        const uint8_t* xmmSlot = nullptr;
        switch (index) {
            case 0: xmmSlot = xmm0; break;
            case 1: xmmSlot = xmm1; break;
            case 2: xmmSlot = xmm2; break;
            case 3: xmmSlot = xmm3; break;
            default: return 0.0;
        }
        std::memcpy(&val, xmmSlot, sizeof(double));
        return val;
    }
};
#pragma pack(pop)

struct ThunkData {
    void* originalFunction;
    void* trampolineFunction;
    uint32_t hookId;
    uint32_t padding0;
    void* preCallback;
    void* postCallback;
    void* replacementFunction;
    uint8_t callOriginal;
    uint8_t padding1[7];
};

}

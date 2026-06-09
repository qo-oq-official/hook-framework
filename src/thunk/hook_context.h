#pragma once
#include <cstdint>
#include <cstring>

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

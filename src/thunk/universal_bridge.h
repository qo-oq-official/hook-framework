#pragma once
#include "thunk/hook_context.h"
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)
struct RegisterContext {
    uint64_t rax;
    uint64_t rcx, rdx, r8, r9;
    uint64_t rsp;
    double   xmm0, xmm1, xmm2, xmm3;
};
#pragma pack(pop)

extern "C" {

void UniversalCxxBridge(hookfw::ThunkData* data, RegisterContext* ctx);

}

namespace hookfw {

uint64_t CallOriginal(ThunkData* data, RegisterContext* ctx, size_t stackArgBytes);

}

#include "output/stack_walker.h"
#include "thunk/universal_bridge.h"
#include "thunk/thunk_manager.h"
#include <windows.h>
#include <iostream>
#include <vector>


#include "logging/logger.h"
namespace hookfw {
void StackInfo::WriteIntoTracedValue(perfetto::TracedValue ctx) const
{
    auto dict = std::move(ctx).WriteDictionary();
    dict.Add("RVA", rva);
    dict.Add("MOD", mod);
}


StackInfo CaptureCallStackAddresses(const RegisterContext* ctx) {
    uint64_t returnAddress = *(uint64_t *)(ctx->rsp - 8);
    StackInfo stackInfo;

    // 先查 trampoline 注册表：嵌套调用时返回地址落在 Detours trampoline 中，
    // 需翻译回原函数地址后再解析模块。
    uint64_t resolved = returnAddress;
    bool mapped = ThunkManager::instance().resolveTrampoline(returnAddress, &resolved);
    if (mapped) {
        LOG_DEBUG("mapped trampoline {:#x} -> original {:#x}", returnAddress, resolved);
    }

    HMODULE hModule = NULL;
    // 通过地址查询其所在的模块句柄
    if (GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)resolved, 
        &hModule)) 
    {
        DWORD_PTR va = (DWORD_PTR)resolved;
        DWORD_PTR base = (DWORD_PTR)hModule;
        DWORD_PTR rva = va - base; // 计算 RVA
        CHAR moduleName[MAX_PATH] = { 0 };
        GetModuleFileNameA(hModule, moduleName, MAX_PATH);
        stackInfo.rva = rva;
        stackInfo.mod = std::string(moduleName);
        LOG_DEBUG("moduleName {} rva {}", moduleName, rva);
    } else {
        LOG_WARN("GetModuleHandleExW failed returnAddress {}", resolved);
    }
    
    return stackInfo;
}
}
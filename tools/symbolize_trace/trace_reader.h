#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hookfw {

// 从 trace 中提取到的单个调用点
struct CallSite {
    uint64_t ts;       // 时间戳（纳秒）
    std::string name;  // slice 名称（被 hook 的函数显示名）
    std::string mod;   // 调用者模块完整路径
    uint64_t rva;      // 调用者代码的镜像相对 RVA
};

// 通过 trace_processor_shell 执行 SQL，提取携带 debug.stack.RVA/MOD 注解的调用点。
// 依赖：trace_processor_shell.exe 路径需有效。
std::vector<CallSite> ExtractCallSites(const std::string& tracePath, const std::string& tpShell);

}  // namespace hookfw

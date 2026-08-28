#pragma once

#include "pdb_resolver.h"

#include <cstddef>
#include <string>

namespace hookfw {

// 对 trace 的文本 proto 做符号注入：在携带 RVA/MOD 的 debug_annotations 块中
// 追加 FUNC（函数名）与 SRC（file:line）两个 dict_entries，返回注入成功的块数量。
size_t AugmentText(const std::string& text, PdbResolver& resolver, std::string& outText);

// 完整增强流程：traceconv text -> AugmentText -> trace_processor convert binary。
// 返回是否成功。
bool AugmentTrace(const std::string& tracePath,
                  const std::string& outputPath,
                  PdbResolver& resolver,
                  const std::string& traceconv,
                  const std::string& tpShell);

}  // namespace hookfw

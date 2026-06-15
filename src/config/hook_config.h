#pragma once
#include "hook_framework/types.h"
#include <string>
#include <vector>

namespace hookfw {

enum class OutputFormat : uint8_t {
    File,
    Perfetto,
};

struct FrameworkConfig {
    std::string outputDir = "dump_data";
    std::string logFile = "hook_framework.log";
    std::string logLevel = "trace";
    OutputFormat outputFormat = OutputFormat::File;
    std::vector<HookDescriptor> hooks;
};

}

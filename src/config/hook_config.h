#pragma once
#include "hook_framework/types.h"
#include "config/perfetto_config.h"
#include <string>
#include <vector>

namespace hookfw {

struct FrameworkConfig {
    std::string outputDir = "dump_data";
    std::string logFile = "hook_framework.log";
    std::string logLevel = "trace";
    PerfettoConfig perfetto;
    std::vector<HookDescriptor> hooks;
};

}

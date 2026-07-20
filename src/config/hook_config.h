#pragma once
#include "hook_framework/types.h"
#include <string>
#include <vector>

namespace hookfw {

struct FrameworkConfig {
    std::string outputDir = "dump_data";
    std::string logFile = "hook_framework.log";
    std::string logLevel = "trace";
    std::vector<HookDescriptor> hooks;
    size_t ringBufferCapacity = 100000;
};

}

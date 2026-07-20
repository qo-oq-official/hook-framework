#pragma once
#include <cstdint>

namespace hookfw {

struct PerfettoConfig {
    uint32_t bufferSizeKb      = 32 * 1024;
    uint32_t flushPeriodMs     = 30000;
    uint32_t maxFileSizeBytes  = 0;
};

}

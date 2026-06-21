#pragma once
#include <format>
#include <cstdint>
#include <string>

namespace hookfw {

inline std::string formatRvaName(uint64_t rva) {
    return std::format("RVA_0x{:08x}", rva);
}

inline std::string formatRvaSuffix(uint64_t rva) {
    return std::format(" @RVA_0x{:08x}", rva);
}

}

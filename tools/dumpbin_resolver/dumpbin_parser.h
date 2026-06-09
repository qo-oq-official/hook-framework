#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace hookfw {

struct ExportEntry {
    uint32_t ordinal = 0;
    uint32_t hint = 0;
    uint64_t rva = 0;
    std::string name;
};

class DumpbinParser {
public:
    bool parse(const std::string& dumpbinOutput);
    const std::vector<ExportEntry>& entries() const { return entries_; }
    const std::string& moduleName() const { return moduleName_; }

private:
    std::vector<ExportEntry> entries_;
    std::string moduleName_;
};

}

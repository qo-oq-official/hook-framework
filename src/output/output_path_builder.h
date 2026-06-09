#pragma once
#include <string>
#include <cstdint>
#include <filesystem>

namespace hookfw {

class OutputPathBuilder {
public:
    explicit OutputPathBuilder(const std::string& baseDir);

    std::filesystem::path buildRawPath(const std::string& moduleName,
                                       const std::string& functionName,
                                       uint64_t seq,
                                       const std::string& phase) const;

    std::filesystem::path buildJsonPath(const std::string& moduleName,
                                        const std::string& functionName,
                                        uint64_t seq,
                                        const std::string& phase) const;

    bool ensureDirectory(const std::filesystem::path& dirPath) const;

private:
    std::string sanitizeName(const std::string& name) const;
    std::string formatSeq(uint64_t seq) const;

    std::filesystem::path baseDir_;
};

}

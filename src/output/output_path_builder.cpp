#include "output/output_path_builder.h"
#include "logging/logger.h"
#include <format>
#include <algorithm>

namespace hookfw {

OutputPathBuilder::OutputPathBuilder(const std::string& baseDir)
    : baseDir_(baseDir) {
    LOG_INFO("Output path builder initialized, baseDir={}", baseDir);
}

std::filesystem::path OutputPathBuilder::buildRawPath(
    const std::string& moduleName, const std::string& functionName,
    uint64_t seq, const std::string& phase) const {

    return baseDir_ / sanitizeName(moduleName) / sanitizeName(functionName)
           / (formatSeq(seq) + "_" + phase + ".raw");
}

std::filesystem::path OutputPathBuilder::buildJsonPath(
    const std::string& moduleName, const std::string& functionName,
    uint64_t seq, const std::string& phase) const {

    return baseDir_ / sanitizeName(moduleName) / sanitizeName(functionName)
           / (formatSeq(seq) + "_" + phase + ".json");
}

bool OutputPathBuilder::ensureDirectory(const std::filesystem::path& dirPath) const {
    try {
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
            LOG_TRACE("Created directory: {}", dirPath.string());
        }
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to create directory {}: {}", dirPath.string(), ex.what());
        return false;
    }
}

std::string OutputPathBuilder::sanitizeName(const std::string& name) const {
    std::string result = name;
    std::replace_if(result.begin(), result.end(),
                    [](char c) {
                        return c == '\\' || c == '/' || c == ':' || c == '*'
                            || c == '?' || c == '"' || c == '<' || c == '>'
                            || c == '|';
                    },
                    '_');
    return result;
}

std::string OutputPathBuilder::formatSeq(uint64_t seq) const {
    return std::format("{:06d}", seq);
}

}

#pragma once
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace hookfw {

class OutputWriter {
public:
    virtual ~OutputWriter() = default;

    virtual bool writeRaw(const std::string& moduleName,
                          const std::string& functionName,
                          uint64_t seq,
                          const std::string& phase,
                          const uint8_t* data,
                          size_t size) = 0;

    virtual bool writeJson(const std::string& moduleName,
                           const std::string& functionName,
                           uint64_t seq,
                           const std::string& phase,
                           const nlohmann::json& data) = 0;
};

}

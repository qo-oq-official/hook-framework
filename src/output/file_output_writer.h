#pragma once
#include "output/output_writer.h"
#include "output/output_path_builder.h"
#include <atomic>
#include <memory>

namespace hookfw {

class FileOutputWriter : public OutputWriter {
public:
    explicit FileOutputWriter(const std::string& baseDir);
    ~FileOutputWriter() override = default;

    bool writeRaw(const std::string& moduleName,
                  const std::string& functionName,
                  uint64_t seq,
                  const std::string& phase,
                  const uint8_t* data,
                  size_t size) override;

    bool writeJson(const std::string& moduleName,
                   const std::string& functionName,
                   uint64_t seq,
                   const std::string& phase,
                   const nlohmann::json& data) override;

    uint64_t nextSeq();

private:
    OutputPathBuilder pathBuilder_;
    std::atomic<uint64_t> seqCounter_{0};
};

}

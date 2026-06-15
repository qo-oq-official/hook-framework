#pragma once
#include "output/output_writer.h"
#include <vector>
#include <mutex>
#include <string>
#include <atomic>
#include <cstdint>

namespace hookfw {

struct TraceEvent {
    std::string name;
    std::string category;
    char phase;
    int64_t timestampUs;
    uint32_t pid;
    uint32_t tid;
    nlohmann::json args;
};

class TraceOutputWriter : public OutputWriter {
public:
    explicit TraceOutputWriter(const std::string& outputDir);
    ~TraceOutputWriter() override;

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

    void addTraceEvent(const std::string& name,
                       const std::string& category,
                       char phase,
                       int64_t timestampUs,
                       uint32_t pid,
                       uint32_t tid,
                       const nlohmann::json& args);

    void flush();
    uint64_t nextSeq();

    static int64_t nowMicroseconds();
    static std::string undecorateName(const std::string& mangledName);

private:
    void doFlush();

    std::string outputDir_;
    std::string traceFilePath_;
    std::vector<TraceEvent> events_;
    std::mutex mutex_;
    std::atomic<uint64_t> seqCounter_{0};
    size_t flushThreshold_ = 10000;
    bool headerWritten_ = false;
    uint64_t eventCount_ = 0;
    std::streampos nextWritePos_ = 0;
};

}

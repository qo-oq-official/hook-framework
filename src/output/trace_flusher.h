#pragma once
#include "output/ring_buffer.h"
#include "output/trace_output_writer.h"
#include <mutex>
#include <string>
#include <unordered_map>

namespace hookfw {

class TraceFlusher {
public:
    static TraceFlusher& instance();

    void start(std::shared_ptr<TraceOutputWriter> writer);
    void stop();
    void syncFlush();

    RingBuffer& ringBuffer() { return ringBuffer_; }

    void registerHookName(uint32_t hookId, const std::string& moduleName,
                          const std::string& functionName, uint64_t rva = 0);

private:
    TraceFlusher() = default;
    ~TraceFlusher();

    void drainToWriter();

    RingBuffer ringBuffer_;
    std::shared_ptr<TraceOutputWriter> writer_;
    std::mutex flushMutex_;
    bool active_ = false;

    struct HookInfo {
        std::string displayName;
        std::string moduleName;
        uint64_t rva = 0;
    };
    std::unordered_map<uint32_t, HookInfo> hookNames_;
    std::mutex namesMutex_;
};

}

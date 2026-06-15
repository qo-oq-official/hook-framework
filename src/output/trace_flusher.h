#pragma once
#include "output/ring_buffer.h"
#include "output/trace_output_writer.h"
#include <thread>
#include <atomic>
#include <string>
#include <unordered_map>

namespace hookfw {

class TraceFlusher {
public:
    static TraceFlusher& instance();

    void start(std::shared_ptr<TraceOutputWriter> writer);
    void stop();

    RingBuffer& ringBuffer() { return ringBuffer_; }

    void registerHookName(uint32_t hookId, const std::string& moduleName,
                          const std::string& functionName);

private:
    TraceFlusher() = default;
    ~TraceFlusher();

    void flushLoop();

    RingBuffer ringBuffer_;
    std::shared_ptr<TraceOutputWriter> writer_;
    std::thread flushThread_;
    std::atomic<bool> running_{false};

    struct HookInfo {
        std::string displayName;
        std::string moduleName;
    };
    std::unordered_map<uint32_t, HookInfo> hookNames_;
    std::mutex namesMutex_;
};

}

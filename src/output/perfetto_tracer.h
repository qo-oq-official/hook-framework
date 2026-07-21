#pragma once
#include "config/perfetto_config.h"
#include <memory>
#include <string>
#include <cstdint>

namespace perfetto {
class TracingSession;
}

namespace hookfw {

class PerfettoTracer {
public:
    static PerfettoTracer& instance();

    bool initialize(const std::string& outputDir);
    void shutdown();
    bool isActive() const { return active_; }

    void setOutputDir(const std::string& dir) { outputDir_ = dir; }
    const std::string& outputDir() const { return outputDir_; }
    void setConfig(const PerfettoConfig& cfg) { config_ = cfg; }

private:
    PerfettoTracer() = default;
    ~PerfettoTracer();

    std::unique_ptr<perfetto::TracingSession> session_;
    PerfettoConfig config_;
    std::string outputDir_;
    std::string tracePath_;
    void* traceFileHandle_ = nullptr;
    bool active_ = false;
};

}

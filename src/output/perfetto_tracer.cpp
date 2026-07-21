#include "output/perfetto_tracer.h"
#include "output/hook_data_source.h"
#include "perfetto.h"
#include "logging/logger.h"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace hookfw {

PerfettoTracer& PerfettoTracer::instance() {
    static PerfettoTracer inst;
    return inst;
}

PerfettoTracer::~PerfettoTracer() {
    shutdown();
}

static void perfettoLogCallback(perfetto::base::LogMessageCallbackArgs args) {
    const char* file = args.filename ? args.filename : "perfetto";
    switch (args.level) {
    case perfetto::base::kLogDebug:
        LOG_DEBUG("[perfetto] {}:{} {}", file, args.line, args.message);
        break;
    case perfetto::base::kLogInfo:
        LOG_INFO("[perfetto] {}:{} {}", file, args.line, args.message);
        break;
    case perfetto::base::kLogImportant:
        LOG_WARN("[perfetto] {}:{} {}", file, args.line, args.message);
        break;
    case perfetto::base::kLogError:
        LOG_ERROR("[perfetto] {}:{} {}", file, args.line, args.message);
        break;
    }
}

bool PerfettoTracer::initialize(const std::string& outputDir) {
    if (active_) return true;

    outputDir_ = outputDir;
    std::filesystem::create_directories(outputDir_);

    LOG_INFO("PerfettoTracer initializing (buffer={}KB flush={}ms)...",
             config_.bufferSizeKb, config_.flushPeriodMs);

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = config_.bufferSizeKb;
    args.log_message_callback = &perfettoLogCallback;
    perfetto::Tracing::Initialize(args);

    HookDataSource::Register();

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
    tracePath_ = (std::filesystem::path(outputDir_) / oss.str()).string();

    perfetto::TraceConfig traceCfg;
    auto* buf = traceCfg.add_buffers();
    buf->set_size_kb(config_.bufferSizeKb);

    auto* ds = traceCfg.add_data_sources()->mutable_config();
    ds->set_name("hookfw.HookDataSource");

    traceCfg.set_duration_ms(0);
    traceCfg.set_flush_period_ms(config_.flushPeriodMs);

    if (config_.maxFileSizeBytes > 0) {
        traceCfg.set_max_file_size_bytes(config_.maxFileSizeBytes);
    }

    session_ = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    session_->Setup(traceCfg);
    session_->Start();

    active_ = true;
    LOG_INFO("PerfettoTracer started, output={}", tracePath_);
    return true;
}

void PerfettoTracer::shutdown() {
    if (!active_) return;

    LOG_INFO("PerfettoTracer shutting down...");

    try {
        HookDataSource::flush();

        session_->FlushBlocking(3000);

        session_->Stop();

        auto data = session_->ReadTraceBlocking();
        if (!data.empty()) {
            std::ofstream file(tracePath_, std::ios::binary);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            file.close();

            LOG_INFO("Perfetto trace saved: {} ({} bytes)", tracePath_, data.size());
        } else {
            LOG_WARN("Perfetto trace is empty");
        }
        LOG_INFO("PerfettoTracer stopped");
    } catch (const std::exception& ex) {
        LOG_ERROR("PerfettoTracer::shutdown exception: {}", ex.what());
    }

    session_.reset();
    perfetto::Tracing::Shutdown();
    active_ = false;
}

}

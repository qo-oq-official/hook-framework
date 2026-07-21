#include "output/perfetto_tracer.h"
#include "output/hook_data_source.h"
#include "perfetto.h"
#include "logging/logger.h"
#include <Windows.h>
#include <io.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace hookfw {

static void tlog(const char* msg) {
    OutputDebugStringA(msg);
    HANDLE h = CreateFileA("perfetto_shutdown.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[256];
        int len = wsprintfA(buf, "%02d:%02d:%02d.%03d %s\r\n",
                            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        WriteFile(h, buf, len, &w, nullptr);
        CloseHandle(h);
    }
}

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

    tlog("PerfettoTracer::initialize enter");

    outputDir_ = outputDir;
    std::filesystem::create_directories(outputDir_);

    LOG_INFO("PerfettoTracer initializing (buffer={}KB flush={}ms)...",
             config_.bufferSizeKb, config_.flushPeriodMs);

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    args.shmem_size_hint_kb = config_.bufferSizeKb;
    args.log_message_callback = &perfettoLogCallback;

    tlog("PerfettoTracer: calling Tracing::Initialize");
    perfetto::Tracing::Initialize(args);
    tlog("PerfettoTracer: Tracing::Initialize done");

    HookDataSource::Register();

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream oss;
    oss << "trace_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".perfetto";
    tracePath_ = (std::filesystem::path(outputDir_) / oss.str()).string();

    traceFileHandle_ = CreateFileA(tracePath_.c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (traceFileHandle_ == INVALID_HANDLE_VALUE) {
        tlog("PerfettoTracer: failed to create trace file");
        LOG_ERROR("Failed to create trace file: {}", tracePath_);
        return false;
    }

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

    tlog("PerfettoTracer: creating session");
    session_ = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(traceFileHandle_), 0);

    tlog("PerfettoTracer: calling Setup with fd");
    session_->Setup(traceCfg, fd);

    tlog("PerfettoTracer: calling Start");
    session_->Start();

    active_ = true;
    LOG_INFO("PerfettoTracer started, output={}", tracePath_);
    tlog("PerfettoTracer::initialize done");
    return true;
}

void PerfettoTracer::shutdown() {
    tlog("PerfettoTracer::shutdown enter");

    if (!active_) {
        tlog("PerfettoTracer::shutdown SKIP (not active)");
        return;
    }

    LOG_INFO("PerfettoTracer shutting down...");

    tlog("PerfettoTracer: calling Stop");
    try {
        session_->Stop();
        tlog("PerfettoTracer: Stop done");
    } catch (const std::exception& ex) {
        tlog("PerfettoTracer: Stop EXCEPTION");
        LOG_ERROR("PerfettoTracer::Stop exception: {}", ex.what());
    }

    tlog("PerfettoTracer: resetting session");
    session_.reset();

    tlog("PerfettoTracer: calling Tracing::Shutdown");
    perfetto::Tracing::Shutdown();
    tlog("PerfettoTracer: Tracing::Shutdown done");

    if (traceFileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(traceFileHandle_);
        traceFileHandle_ = INVALID_HANDLE_VALUE;
        LOG_INFO("Perfetto trace saved: {}", tracePath_);
        tlog("PerfettoTracer: file closed");
    }

    active_ = false;
    tlog("PerfettoTracer::shutdown exit");
}

}

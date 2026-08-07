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

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

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
    default:
        LOG_DEBUG("[perfetto] {}:{} {}", file, args.line, args.message);
        break;
    }
}

bool PerfettoTracer::initialize() {
    if (active_) return true;
    active_ = true;
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    args.shmem_size_hint_kb = 32768;
    args.shmem_page_size_hint_kb = 32;  
    args.track_event_buffer_exhausted_policy = perfetto::BufferExhaustedPolicy::kStall;  
    args.log_message_callback = &perfettoLogCallback;
    try {
        LOG_INFO("PerfettoTracer: calling Tracing::Initialize");
        perfetto::Tracing::Initialize(args);
        LOG_INFO("PerfettoTracer: Tracing::Initialize done");
        bool res = perfetto::TrackEvent::AddSessionObserver(&track_event_observer_);
        LOG_INFO("PerfettoTracer: TrackEvent::AddSessionObserver done res {}", res);
        perfetto::TrackEvent::Register();
        LOG_INFO("PerfettoTracer: TrackEvent::Register done");
        bool ready = track_event_observer_.WaitReady(std::chrono::milliseconds(500));
        LOG_INFO("PerfettoTracer: {}", ready);
    } catch(...) {
        LOG_ERROR("PerfettoTracer: Failed to initialize");
        throw;
    }
    return true;
}

void PerfettoTracer::shutdown() {
    
}

}

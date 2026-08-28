#include "output/hook_data_source.h"
#include "output/perfetto_tracer.h"
#include "thunk/universal_bridge.h"
#include "output/stack_walker.h"
#include "perfetto.h"
#include "logging/logger.h"

namespace hookfw {

std::unordered_map<uint32_t, std::string> HookDataSource::hookNames_;
std::mutex HookDataSource::namesMutex_;

void HookDataSource::Register() {

}

void HookDataSource::TraceSlice(const std::string& name,
                                const std::string& category,
                                char phase,
                                const nlohmann::json& args) {
    if (!PerfettoTracer::instance().isActive()) {
        auto& tracer = PerfettoTracer::instance();
        tracer.initialize();
    }
    if (phase == 'B') {
        LOG_TRACE("Entry function {}", name);
        TRACE_EVENT_BEGIN("SW", perfetto::DynamicString{name});
    } else if (phase == 'E') {
        LOG_TRACE("Exit function {}", name);
        TRACE_EVENT_END("SW");
    } else {
        LOG_ERROR("unknown phase {}", phase);
    }
}

void HookDataSource::TraceSlice(const std::string& name,
                                const std::string& category,
                                char phase,
                                const RegisterContext* ctx) {
    if (!PerfettoTracer::instance().isActive()) {
        auto& tracer = PerfettoTracer::instance();
        tracer.initialize();
    }
    if (phase == 'B') {
        LOG_TRACE("Entry function {}", name);
        auto stack = CaptureCallStackAddresses(ctx);
        TRACE_EVENT_BEGIN("SW", perfetto::DynamicString{name}, "stack", stack);
    } else if (phase == 'E') {
        LOG_TRACE("Exit function {}", name);
        TRACE_EVENT_END("SW");
    } else {
        LOG_ERROR("unknown phase {}", phase);
    }
}

void HookDataSource::registerHookName(uint32_t hookId, const std::string& name) {
    std::lock_guard lock(namesMutex_);
    hookNames_[hookId] = name;
}

std::string HookDataSource::getHookName(uint32_t hookId) {
    std::lock_guard lock(namesMutex_);
    auto it = hookNames_.find(hookId);
    return (it != hookNames_.end()) ? it->second : std::to_string(hookId);
}

}

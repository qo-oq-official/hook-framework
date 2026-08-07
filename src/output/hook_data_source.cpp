#include "output/hook_data_source.h"
#include "output/perfetto_tracer.h"
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
        // TRACE_EVENT_BEGIN("SW", perfetto::DynamicString{name}, 
        //     [&](perfetto::EventContext ctx) {
        //         for (const auto& [key, val] : args.items()) {
        //             auto* da = ctx.event()->add_debug_annotations();
        //             da->set_name(key);
        //             if (val.is_string()) {
        //                 da->set_string_value(val.get<std::string>());
        //             } else if (val.is_number_integer()) {
        //                 da->set_int_value(val.get<int64_t>());
        //             } else if (val.is_number_float()) {
        //                 da->set_double_value(val.get<double>());
        //             } else if (val.is_boolean()) {
        //                 da->set_bool_value(val.get<bool>());
        //             } else {
        //                 da->set_legacy_json_value(val.dump());
        //             }
        //         }
        // });
        LOG_TRACE("Entry function {}", name);
        TRACE_EVENT_BEGIN("SW", perfetto::DynamicString{name});
    } else if (phase == 'E') {
        // TRACE_EVENT_END("SW", 
        //     [&](perfetto::EventContext ctx) {
        //         for (const auto& [key, val] : args.items()) {
        //             auto* da = ctx.event()->add_debug_annotations();
        //             da->set_name(key);
        //             if (val.is_string()) {
        //                 da->set_string_value(val.get<std::string>());
        //             } else if (val.is_number_integer()) {
        //                 da->set_int_value(val.get<int64_t>());
        //             } else if (val.is_number_float()) {
        //                 da->set_double_value(val.get<double>());
        //             } else if (val.is_boolean()) {
        //                 da->set_bool_value(val.get<bool>());
        //             } else {
        //                 da->set_legacy_json_value(val.dump());
        //             }
        //         }
        // });
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

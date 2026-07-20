#include "output/hook_data_source.h"
#include "output/perfetto_tracer.h"
#include "perfetto.h"
#include "logging/logger.h"

namespace hookfw {

class HookDataSourceInternal : public perfetto::DataSource<HookDataSourceInternal> {
public:
    void OnSetup(const SetupArgs&) override {}
    void OnStart(const StartArgs&) override {}
    void OnStop(const StopArgs&) override {}
};

}  // namespace hookfw

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(hookfw::HookDataSourceInternal);

namespace hookfw {

std::unordered_map<uint32_t, std::string> HookDataSource::hookNames_;
std::mutex HookDataSource::namesMutex_;

void HookDataSource::Register() {
    perfetto::DataSourceDescriptor desc;
    desc.set_name("hookfw.HookDataSource");
    HookDataSourceInternal::Register(desc);
    LOG_INFO("HookDataSource registered");
}

void HookDataSource::TraceSlice(const std::string& name,
                                const std::string& category,
                                char phase,
                                uint32_t threadId,
                                const nlohmann::json& args) {
    if (!PerfettoTracer::instance().isActive()) {
        PerfettoTracer::instance().initialize(
            PerfettoTracer::instance().outputDir().empty()
                ? "dump_data"
                : PerfettoTracer::instance().outputDir());
    }

    HookDataSourceInternal::Trace(
        [&](HookDataSourceInternal::TraceContext ctx) {
            auto pkt = ctx.NewTracePacket();
            pkt->set_timestamp(
                static_cast<uint64_t>(perfetto::base::GetWallTimeNs().count()));
            auto* te = pkt->set_track_event();
            te->set_name(name);
            te->add_categories(category);
            te->set_type(phase == 'B'
                ? perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN
                : perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END);

            for (const auto& [key, val] : args.items()) {
                auto* da = te->add_debug_annotations();
                da->set_name(key);
                if (val.is_string()) {
                    da->set_string_value(val.get<std::string>());
                } else if (val.is_number_integer()) {
                    da->set_int_value(val.get<int64_t>());
                } else if (val.is_number_float()) {
                    da->set_double_value(val.get<double>());
                } else if (val.is_boolean()) {
                    da->set_bool_value(val.get<bool>());
                } else {
                    da->set_string_value(val.dump());
                }
            }
        });
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

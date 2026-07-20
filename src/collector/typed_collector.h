#pragma once
#include "collector/collector_base.h"
#include "hook_framework/types.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace hookfw {

class TypedCollector : public CollectorBase {
public:
    TypedCollector() = default;
    ~TypedCollector() override = default;

    void setParamDescriptors(const std::vector<ParamDescriptor>& params);
    void setReturnType(ParamType type);

    void onEntry(const HookContext& ctx) final;
    void onExit(const HookContext& ctx) final;

protected:
    virtual void onEntryCustom(const HookContext& ctx, nlohmann::json& customData) {}
    virtual void onExitCustom(const HookContext& ctx, nlohmann::json& customData) {}

    nlohmann::json extractParam(const HookContext& ctx, int index,
                                const ParamDescriptor& desc,
                                ExtractionPhase phase) const;
    nlohmann::json extractReturnValue(const HookContext& ctx) const;

    // Build flat Perfetto args from extracted params (collector owns formatting)
    void buildPerfettoArgs(const nlohmann::json& params,
                           const nlohmann::json& customData,
                           nlohmann::json& args) const;

    // Flatten a single param into args with a field prefix
    static void flattenParam(const nlohmann::json& p, const std::string& key,
                             nlohmann::json& args);

    std::vector<ParamDescriptor> params_;
    ParamType returnType_ = ParamType::Int64;
};

}

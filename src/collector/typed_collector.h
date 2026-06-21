#pragma once
#include "collector/collector_base.h"
#include "output/file_output_writer.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace hookfw {

class TypedCollector : public CollectorBase {
public:
    TypedCollector() = default;
    ~TypedCollector() override = default;

    void setParamDescriptors(const std::vector<ParamDescriptor>& params);
    void setReturnType(ParamType type);

    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;

protected:
    nlohmann::json extractParam(const HookContext& ctx, int index,
                                const ParamDescriptor& desc) const;
    nlohmann::json extractReturnValue(const HookContext& ctx) const;

    std::vector<ParamDescriptor> params_;
    ParamType returnType_ = ParamType::Int64;
};

}

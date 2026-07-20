#pragma once
#include "collector/collector_base.h"

namespace hookfw {

class RawCollector : public CollectorBase {
public:
    RawCollector() = default;
    ~RawCollector() override = default;

    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

}

#pragma once
#include "collector/collector_base.h"

namespace hookfw {

class ClassCacheCollector : public CollectorBase {
public:
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

}

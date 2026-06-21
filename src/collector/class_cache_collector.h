#pragma once
#include "collector/typed_collector.h"

namespace hookfw {

class ClassCacheCollector : public TypedCollector {
public:
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

}

#pragma once
#include "collector/collector_base.h"
#include "collector/raw_collector.h"
#include "collector/typed_collector.h"
#include "hook_framework/types.h"
#include <memory>

namespace hookfw {

class CollectorFactory {
public:
    static std::shared_ptr<CollectorBase> create(const HookDescriptor& desc);
};

}

#include "collector/collector_factory.h"
#include "collector/typed_collector.h"
#include "logging/logger.h"
#include "output/rva_format.h"
#include <format>

namespace hookfw {

CollectorRegistry& CollectorRegistry::instance() {
    static CollectorRegistry inst;
    return inst;
}

void CollectorRegistry::registerCreator(const std::string& name, CollectorCreator creator) {
    creators_[name] = std::move(creator);
}

std::shared_ptr<CollectorBase> CollectorRegistry::create(const std::string& name) {
    auto it = creators_.find(name);
    if (it == creators_.end()) {
        LOG_ERROR("Custom collector not found: {}", name);
        return nullptr;
    }
    return it->second();
}

std::shared_ptr<CollectorBase> CollectorFactory::create(const HookDescriptor& desc) {
    std::string targetName = desc.target.useRva
        ? formatRvaName(desc.target.rva)
        : desc.target.symbolName;

    switch (desc.collectorType) {
        case CollectorType::Raw: {
            LOG_INFO("Creating RawCollector for {}!{}", desc.target.moduleName, targetName);
            return std::make_shared<RawCollector>();
        }
        case CollectorType::Typed: {
            LOG_INFO("Creating TypedCollector for {}!{}", desc.target.moduleName, targetName);
            auto collector = std::make_shared<TypedCollector>();
            collector->setParamDescriptors(desc.params);
            collector->setReturnType(desc.returnType);
            return collector;
        }
        case CollectorType::Custom: {
            auto collector = CollectorRegistry::instance().create(desc.customCollectorName);
            if (!collector) {
                LOG_ERROR("Falling back to RawCollector for custom: {}", desc.customCollectorName);
                return std::make_shared<RawCollector>();
            }
            if (auto* typed = dynamic_cast<TypedCollector*>(collector.get())) {
                typed->setParamDescriptors(desc.params);
                typed->setReturnType(desc.returnType);
            }
            return collector;
        }
    }
    LOG_ERROR("Unknown collector type: {}", static_cast<int>(desc.collectorType));
    return std::make_shared<RawCollector>();
}

}

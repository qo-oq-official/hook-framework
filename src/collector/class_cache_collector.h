#pragma once
#include "collector/typed_collector.h"

namespace hookfw {

struct ReadClassCollector : public TypedCollector {
    void onExitCustom(const HookContext& ctx, nlohmann::json& customData) override;
};

struct ReadObjectCollector : public TypedCollector {
    void onExitCustom(const HookContext& ctx, nlohmann::json& customData) override;
};

struct CopyLoadArrayStoreMapFromCollector : public TypedCollector {
    void onEntryCustom(const HookContext& ctx, nlohmann::json& customData) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& customData) override;
};

struct UseCacheArchiveCollector : public TypedCollector {
    void onEntryCustom(const HookContext& ctx, nlohmann::json& customData) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& customData) override;
};

}

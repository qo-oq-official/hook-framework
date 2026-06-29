#pragma once
#include "collector/typed_collector.h"

namespace hookfw {

// Hook: su_CArchive::ReadClass / WriteClass
// Captures: class name + index per archive
struct ReadClassCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& args) override;
};

struct ReadObjectCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& args) override;
};

struct CopyLoadArrayStoreMapFromCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& args) override;
};

struct UseCacheArchiveCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExitCustom(const HookContext& ctx, nlohmann::json& args) override;
};

}

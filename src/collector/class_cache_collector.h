#pragma once
#include "collector/typed_collector.h"

namespace hookfw {

// Hook: su_CArchive::ReadClass / WriteClass
// Captures: class name + index per archive
struct ReadClassCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

// Hook: su_CArchive::ReadObject
// Captures: vtable[2] Serialize RVA from returned CObject*
struct ReadObjectCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

// Hook: su_CArchive::copyLoadArrayStoreMapFrom
// Captures: src/dst archive full class → index mapping
struct CopyLoadArrayStoreMapFromCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

// Hook: useCacheArchive_c ctor/dtor
// Captures: archive + mode on scope enter/leave
struct UseCacheArchiveCollector : public TypedCollector {
    void onEntry(const HookContext& ctx) override;
    void onExit(const HookContext& ctx) override;
};

}

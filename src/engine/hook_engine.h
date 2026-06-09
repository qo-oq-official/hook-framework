#pragma once
#include "hook_framework/types.h"
#include "thunk/thunk_manager.h"
#include "collector/collector_base.h"
#include "replacer/replacer_base.h"
#include "output/output_writer.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace hookfw {

struct InstalledHook {
    uint32_t id;
    HookDescriptor descriptor;
    ThunkManager::ThunkSlot* thunkSlot = nullptr;
    std::shared_ptr<CollectorBase> collector;
    std::shared_ptr<ReplacerBase> replacer;
    void* resolvedAddress = nullptr;
};

class HookEngineCore {
public:
    static HookEngineCore& instance();

    bool addHook(const HookDescriptor& desc,
                 std::shared_ptr<CollectorBase> collector,
                 std::shared_ptr<ReplacerBase> replacer);

    bool installAll();
    void uninstallAll();

    InstalledHook* getHookById(uint32_t id);
    void setOutputWriter(std::shared_ptr<OutputWriter> writer);
    std::shared_ptr<OutputWriter> getOutputWriter();

private:
    HookEngineCore() = default;
    ~HookEngineCore();

    void* resolveAddress(const HookTarget& target);

    std::mutex mutex_;
    std::vector<std::unique_ptr<InstalledHook>> hooks_;
    std::shared_ptr<OutputWriter> outputWriter_;
    uint32_t nextId_ = 1;
    bool installed_ = false;
};

}

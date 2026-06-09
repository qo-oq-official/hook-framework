#include "collector/raw_collector.h"
#include "logging/logger.h"

namespace hookfw {

void RawCollector::onEntry(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("RawCollector::onEntry: no output writer set");
        return;
    }

    try {
        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(&ctx);
        size_t size = sizeof(HookContext);

        if (!outputWriter_->writeRaw(moduleName_, functionName_, seq, "entry", data, size)) {
            LOG_ERROR("RawCollector: failed to write entry data for {}!{}",
                      moduleName_, functionName_);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("RawCollector::onEntry exception: {}", ex.what());
    }
}

void RawCollector::onExit(const HookContext& ctx) {
    if (!outputWriter_) {
        LOG_ERROR("RawCollector::onExit: no output writer set");
        return;
    }

    try {
        auto* fileWriter = dynamic_cast<FileOutputWriter*>(outputWriter_.get());
        uint64_t seq = fileWriter ? fileWriter->nextSeq() : 0;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(&ctx);
        size_t size = sizeof(HookContext);

        if (!outputWriter_->writeRaw(moduleName_, functionName_, seq, "exit", data, size)) {
            LOG_ERROR("RawCollector: failed to write exit data for {}!{}",
                      moduleName_, functionName_);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("RawCollector::onExit exception: {}", ex.what());
    }
}

}

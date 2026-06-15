#include "output/trace_flusher.h"
#include "logging/logger.h"
#include <Windows.h>

namespace hookfw {

TraceFlusher& TraceFlusher::instance() {
    static TraceFlusher inst;
    return inst;
}

TraceFlusher::~TraceFlusher() {
    stop();
}

void TraceFlusher::start(std::shared_ptr<TraceOutputWriter> writer) {
    if (running_) return;
    writer_ = writer;
    running_ = true;
    flushThread_ = std::thread(&TraceFlusher::flushLoop, this);
    LOG_INFO("TraceFlusher started");
}

void TraceFlusher::stop() {
    if (!running_) return;
    running_ = false;
    if (flushThread_.joinable()) {
        flushThread_.join();
    }
    flushLoop();
    if (writer_) writer_->flush();
    LOG_INFO("TraceFlusher stopped");
}

void TraceFlusher::registerHookName(uint32_t hookId, const std::string& moduleName,
                                     const std::string& functionName) {
    std::lock_guard lock(namesMutex_);
    HookInfo info;
    info.moduleName = moduleName;
    info.displayName = TraceOutputWriter::undecorateName(functionName);
    hookNames_[hookId] = std::move(info);
}

void TraceFlusher::flushLoop() {
    constexpr size_t BATCH = 4096;
    auto batch = std::make_unique<TraceRecord[]>(BATCH);

    while (true) {
        size_t count = ringBuffer_.drain(batch.get(), BATCH);
        if (count == 0) {
            if (!running_) break;
            Sleep(50);
            continue;
        }

        std::lock_guard lock(namesMutex_);
        for (size_t i = 0; i < count; i++) {
            const auto& rec = batch[i];
            auto it = hookNames_.find(rec.hookId);
            std::string name = (it != hookNames_.end()) ? it->second.displayName
                                                         : std::to_string(rec.hookId);
            std::string cat = (it != hookNames_.end()) ? it->second.moduleName : "unknown";

            nlohmann::json args;
            args["arg0"] = std::format("{:#x}", rec.arg0);
            args["arg1"] = std::format("{:#x}", rec.arg1);
            args["arg2"] = std::format("{:#x}", rec.arg2);
            args["arg3"] = std::format("{:#x}", rec.arg3);

            if (writer_) {
                writer_->addTraceEvent(name, cat, 'X', rec.timestampUs,
                                       GetCurrentProcessId(), rec.threadId, args);
            }
        }
    }
}

}

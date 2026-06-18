#include "output/trace_flusher.h"
#include "logging/logger.h"
#include <Windows.h>
#include <format>

namespace hookfw {

TraceFlusher& TraceFlusher::instance() {
    static TraceFlusher inst;
    return inst;
}

TraceFlusher::~TraceFlusher() {
    stop();
}

void TraceFlusher::start(std::shared_ptr<TraceOutputWriter> writer) {
    writer_ = writer;
    active_ = true;
    LOG_INFO("TraceFlusher started (synchronous mode)");
}

void TraceFlusher::stop() {
    if (!active_) return;
    active_ = false;
    syncFlush();
    LOG_INFO("TraceFlusher stopped ({} events remaining in buffer)", ringBuffer_.pending());
}

void TraceFlusher::registerHookName(uint32_t hookId, const std::string& moduleName,
                                     const std::string& functionName, uint64_t rva) {
    std::lock_guard lock(namesMutex_);
    HookInfo info;
    info.moduleName = moduleName;
    info.displayName = TraceOutputWriter::undecorateName(functionName);
    info.rva = rva;
    hookNames_[hookId] = std::move(info);
}

static uint64_t g_totalFlushed = 0;
static bool g_firstFlushLogged = false;

void TraceFlusher::syncFlush() {
    std::unique_lock lock(flushMutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;

    try {
        drainToWriter();
        if (writer_) writer_->flush();
    } catch (const std::exception& ex) {
        LOG_ERROR("syncFlush exception: {}", ex.what());
    } catch (...) {
        LOG_ERROR("syncFlush unknown exception");
    }
}

void TraceFlusher::drainToWriter() {
    constexpr size_t BATCH = 4096;
    TraceRecord batch[BATCH];

    size_t count;
    while ((count = ringBuffer_.drain(batch, BATCH)) > 0) {
        g_totalFlushed += count;
        uint64_t pending = ringBuffer_.pending();
        if (pending > RingBuffer::CAPACITY * 2 / 3) {
            static uint64_t lastWarnPending = 0;
            if (pending > lastWarnPending + 100000) {
                lastWarnPending = pending;
                LOG_WARN("Ring buffer approaching capacity: {} pending / {} total ({} total flushed)",
                         pending, RingBuffer::CAPACITY, g_totalFlushed);
            }
        }

        std::lock_guard lock(namesMutex_);
        for (size_t i = 0; i < count; i++) {
            try {
                const auto& rec = batch[i];
                auto it = hookNames_.find(rec.hookId);
                if (it == hookNames_.end()) {
                    LOG_WARN("Unknown hook_id {} in ring buffer (not registered with flusher)",
                             rec.hookId);
                }
                std::string name = (it != hookNames_.end())
                    ? (it->second.rva > 0
                        ? it->second.displayName + std::format(" @RVA_{:#x}", it->second.rva)
                        : it->second.displayName)
                    : std::to_string(rec.hookId);
                std::string cat = (it != hookNames_.end()) ? it->second.moduleName : "unknown";

                nlohmann::json args;
                if (rec.phase == 0) {
                    args["arg0"] = std::format("{:#x}", rec.arg0);
                    args["arg1"] = std::format("{:#x}", rec.arg1);
                    args["arg2"] = std::format("{:#x}", rec.arg2);
                    args["arg3"] = std::format("{:#x}", rec.arg3);
                } else {
                    if (rec.arg0 != 0) {
                        args["return"] = std::format("{:#x}", rec.arg0);
                    }
                }

                char phase = (rec.phase == 0) ? 'B' : 'E';
                if (writer_) {
                    writer_->addTraceEvent(name, cat, phase, rec.timestampUs,
                                           GetCurrentProcessId(), rec.threadId, args);
                }
            } catch (const std::exception& ex) {
                LOG_WARN("Failed to serialize event for hook_id={} in drainToWriter: {}",
                         batch[i].hookId, ex.what());
            } catch (...) {
                LOG_WARN("Unknown exception serializing hook_id={} in drainToWriter",
                         batch[i].hookId);
            }
        }
    }
}

}

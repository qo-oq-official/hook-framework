#pragma once

#include "etw/etw_loader_config.h"
#include "etw/etw_monitor.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hookfw {

// Watches Image Load events and injects the configured action DLLs into any
// process that loads a trigger DLL.
class DllLoaderEngine {
public:
    DllLoaderEngine() = default;
    ~DllLoaderEngine();

    DllLoaderEngine(const DllLoaderEngine&) = delete;
    DllLoaderEngine& operator=(const DllLoaderEngine&) = delete;

    // Starts monitoring and the injection worker. Returns false on failure.
    bool start(const EtwLoaderConfig& config);
    void stop();

private:
    struct CompiledRule {
        std::wstring onLoad;  // lowercased basenames
        std::wstring inject;  // absolute paths
    };

    void onImageLoad(ULONG processId, ULONG_PTR imageBase, const std::wstring& filePath);
    void injectionLoop();
    bool injectDll(ULONG pid, const std::wstring& dllPath);

    EtwLoaderConfig config_;
    std::vector<CompiledRule> compiled_;
    EtwImageLoadMonitor monitor_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::pair<ULONG, std::wstring>> queue_;

    std::mutex injectedMutex_;
    std::unordered_set<std::wstring> injected_;  // "<pid>|<lowercased dll>"

    std::thread injectThread_;
    std::atomic<bool> running_{false};
};

}  // namespace hookfw

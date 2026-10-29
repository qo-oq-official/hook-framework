#include "etw/etw_loader.h"

#include "logging/logger.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {

hookfw::DllLoaderEngine g_engine;
std::atomic<bool> g_stop{false};

BOOL WINAPI ctrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--config" || a == "-c") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::printf("Usage: etw_loader --config <path.json>\n");
            return 0;
        }
    }

    if (configPath.empty()) {
        std::printf("Usage: etw_loader --config <path.json>\n");
        return 1;
    }
    LOG_INFO("=== ETW DLL Loader starting ===");

    hookfw::EtwLoaderConfigLoader loader;
    auto config = loader.load(configPath);
    if (!config) {
        LOG_ERROR("Failed to load config: {}", configPath);
        spdlog::shutdown();
        return 1;
    }

    if (!g_engine.start(*config)) {
        LOG_ERROR("Failed to start engine");
        spdlog::shutdown();
        return 1;
    }

    SetConsoleCtrlHandler(ctrlHandler, TRUE);
    LOG_INFO("Monitoring... press Ctrl+C to stop");

    while (!g_stop) {
        Sleep(100);
    }

    LOG_INFO("Stopping...");
    g_engine.stop();
    LOG_INFO("Stopped.");
    spdlog::shutdown();
    return 0;
}

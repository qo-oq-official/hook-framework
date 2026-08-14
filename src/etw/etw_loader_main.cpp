#include "etw/etw_loader.h"

#include "logging.h"

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

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("etw_loader.log", true);
    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
    auto logger = std::make_shared<spdlog::logger>("etw_loader", sinks.begin(), sinks.end());
    logger->set_pattern("[%H:%M:%S.%e] [%@] [%l] [tid:%t] %v");
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info);

    spdlog::info("=== ETW DLL Loader starting ===");

    hookfw::EtwLoaderConfigLoader loader;
    auto config = loader.load(configPath);
    if (!config) {
        spdlog::error("Failed to load config: {}", configPath);
        spdlog::shutdown();
        return 1;
    }

    if (!g_engine.start(*config)) {
        spdlog::error("Failed to start engine");
        spdlog::shutdown();
        return 1;
    }

    SetConsoleCtrlHandler(ctrlHandler, TRUE);
    spdlog::info("Monitoring... press Ctrl+C to stop");

    while (!g_stop) {
        Sleep(100);
    }

    spdlog::info("Stopping...");
    g_engine.stop();
    spdlog::info("Stopped.");
    spdlog::shutdown();
    return 0;
}

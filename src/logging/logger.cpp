#include "logging/logger.h"
#include <filesystem>
#include <format>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/cfg/env.h>
#if defined(_WIN32)
#include <Windows.h>
#include <shlobj.h>
#endif


namespace hookfw {

static void logDiag(const char* msg) {
    OutputDebugStringA(msg);
}

std::string GetCurrentProcessName() {
    wchar_t buffer[MAX_PATH];
    // 獲取目前執行檔的完整寬字元路徑
    if (GetModuleFileNameW(NULL, buffer, MAX_PATH)) {
        std::filesystem::path exePath(buffer);
        return exePath.stem().string();
    }
    return "Unknown";
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::initialize(const std::string& logFile)
{
    try {

        std::filesystem::path absolutePath = std::filesystem::absolute(logFile);
        logDiag(std::format("Logger: initializing file= {}\n", absolutePath.string()).c_str());
        std::filesystem::file_status status = std::filesystem::status(absolutePath);
        std::filesystem::perms permissions = status.permissions();
        if ((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none) {
            logDiag("Log file without write permissions\n");
            return;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        logDiag(std::format("Failed to get log file absolute path {}\n", e.what()).c_str());
        return;
    }
    try {
        spdlog::cfg::load_env_levels(); 
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{fileSink, msvcSink};
        auto logger = std::make_shared<spdlog::logger>(LOGGER_NAME, sinks.begin(), sinks.end());
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
        logger->flush_on(spdlog::level::trace);

        spdlog::register_logger(logger);
        spdlog::apply_logger_env_levels(logger);
        logger->info("Hook framework logger initialized, file={}", logFile);
        logDiag("Logger: file sink created OK\n");
        LOG_INFO("current log level {}", spdlog::level::to_string_view(logger->level()));
    } catch (const std::exception& ex) {
        std::string msg = "Logger init EXCEPTION: " + std::string(ex.what()) + " err=" + std::to_string(GetLastError()) + "\n";
        logDiag(msg.c_str());
    } catch (...) {
        logDiag("Logger init UNKNOWN EXCEPTION\n");
    }
}

void Logger::initialize() {
    if (spdlog::get(LOGGER_NAME)) {
        return;
    }
    std::string processName = GetCurrentProcessName();
    std::string logFileName = std::format("{}_hook.log", processName); 
    std::filesystem::path logFile = logFileName;
    initialize(logFile.string());
}

void Logger::shutdown() {
    auto logger = spdlog::get(LOGGER_NAME);
    if (logger) {
        logger->info("Hook framework logger shutting down");
        logger->flush();
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    auto logger = spdlog::get(LOGGER_NAME);
    if (!logger) {
        initialize();
    }
    return logger;
}

}

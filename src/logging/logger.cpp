#include "logging/logger.h"
#include "logging/crash_log.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/cfg/env.h>
#include <Windows.h>

namespace hookfw {

static void logDiag(const char* msg) {
    OutputDebugStringA(msg);
    crash_log::write(msg);
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::initialize(const std::string& logFile, spdlog::level::level_enum level) {
    if (logger_) {
        return;
    }

    logDiag(("Logger: initializing file=" + logFile + "\n").c_str());

    // Check if directory is writable
    auto dirSep = logFile.rfind('\\');
    if (dirSep != std::string::npos) {
        std::string dirPath = logFile.substr(0, dirSep);
        DWORD attrs = GetFileAttributesA(dirPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            std::string err = "Logger: directory not found: " + dirPath + " err=" + std::to_string(GetLastError()) + "\n";
            logDiag(err.c_str());
        }
    }

    try {
        spdlog::cfg::load_env_levels("HOOK_LOG_LEVEL"); 
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{fileSink, msvcSink};
        logger_ = std::make_shared<spdlog::logger>("hookfw", sinks.begin(), sinks.end());
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
        logger_->flush_on(spdlog::level::trace);

        spdlog::register_logger(logger_);
        spdlog::apply_logger_env_levels(logger_);
        logger_->info("Hook framework logger initialized, file={}", logFile);
        logDiag("Logger: file sink created OK\n");
        LOG_INFO("current log level {}", spdlog::level::to_string_view(logger_->level()));
    } catch (const std::exception& ex) {
        std::string msg = "Logger init EXCEPTION: " + std::string(ex.what()) + " err=" + std::to_string(GetLastError()) + "\n";
        logDiag(msg.c_str());
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("hookfw", msvcSink);
        logger_->set_level(level);
        logger_->error("Logger fell back to MSVC debug sink (file creation failed: {})", ex.what());
    } catch (...) {
        logDiag("Logger init UNKNOWN EXCEPTION\n");
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("hookfw", msvcSink);
        logger_->set_level(level);
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->info("Hook framework logger shutting down");
        logger_->flush();
        spdlog::drop("hookfw");
        logger_.reset();
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (!logger_) {
        initialize();
    }
    return logger_;
}

}

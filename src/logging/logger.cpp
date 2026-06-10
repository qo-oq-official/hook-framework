#include "logging/logger.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <Windows.h>

namespace hookfw {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::initialize(const std::string& logFile, spdlog::level::level_enum level) {
    if (logger_) {
        return;
    }

    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile, 10 * 1024 * 1024, 5);
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{fileSink, msvcSink};
        logger_ = std::make_shared<spdlog::logger>("hookfw", sinks.begin(), sinks.end());
        logger_->set_level(level);
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
        logger_->flush_on(spdlog::level::info);

        spdlog::register_logger(logger_);
        logger_->info("Hook framework logger initialized, file={}", logFile);
    } catch (const std::exception& ex) {
        OutputDebugStringA(("Logger init failed: " + std::string(ex.what()) + "\n").c_str());
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("hookfw", msvcSink);
        logger_->set_level(level);
        logger_->error("Failed to create file logger: {}", ex.what());
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

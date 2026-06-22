#include "logging/logger.h"
#include "logging/crash_log.h"
#include <spdlog/sinks/basic_file_sink.h>
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

    crash_log::write(("Logger: initializing file=" + logFile + "\n").c_str());

    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{fileSink, msvcSink};
        logger_ = std::make_shared<spdlog::logger>("hookfw", sinks.begin(), sinks.end());
        logger_->set_level(level);
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
        logger_->flush_on(spdlog::level::trace);

        spdlog::register_logger(logger_);
        logger_->info("Hook framework logger initialized, file={}", logFile);
        crash_log::write("Logger: file sink created OK\n");
    } catch (const std::exception& ex) {
        std::string msg = "Logger init FAILED: " + std::string(ex.what()) + "\n";
        OutputDebugStringA(msg.c_str());
        crash_log::write(msg.c_str());
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

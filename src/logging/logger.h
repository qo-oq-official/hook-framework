#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <memory>
#include <string>

namespace hookfw {

class Logger {
public:
    static Logger& instance();

    void initialize(const std::string& logFile = "hook_framework.log",
                    spdlog::level::level_enum level = spdlog::level::trace);
    void shutdown();

    std::shared_ptr<spdlog::logger> get();

private:
    Logger() = default;
    ~Logger() = default;
    std::shared_ptr<spdlog::logger> logger_ = nullptr;
};

}

#define LOG_TRACE(...) ::hookfw::Logger::instance().get()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) ::hookfw::Logger::instance().get()->debug(__VA_ARGS__)
#define LOG_INFO(...)  ::hookfw::Logger::instance().get()->info(__VA_ARGS__)
#define LOG_WARN(...)  ::hookfw::Logger::instance().get()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::hookfw::Logger::instance().get()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::hookfw::Logger::instance().get()->critical(__VA_ARGS__)

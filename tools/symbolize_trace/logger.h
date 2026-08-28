#pragma once

#include <spdlog/spdlog.h>

namespace hookfw {

// 返回本工具的全局 spdlog logger（由 main 注册，名称为 "symbolize_trace"）
inline spdlog::logger* log() {
    std::shared_ptr<spdlog::logger> l = spdlog::get("symbolize_trace");
    if (l) {
        return l.get();
    }
    return spdlog::default_logger_raw();
}

}  // namespace hookfw

#define LOG_TRACE(...) do { auto* _l = ::hookfw::log(); if (_l) _l->trace(__VA_ARGS__); } while (0)
#define LOG_DEBUG(...) do { auto* _l = ::hookfw::log(); if (_l) _l->debug(__VA_ARGS__); } while (0)
#define LOG_INFO(...)  do { auto* _l = ::hookfw::log(); if (_l) _l->info(__VA_ARGS__);  } while (0)
#define LOG_WARN(...)  do { auto* _l = ::hookfw::log(); if (_l) _l->warn(__VA_ARGS__);  } while (0)
#define LOG_ERROR(...) do { auto* _l = ::hookfw::log(); if (_l) _l->error(__VA_ARGS__); } while (0)

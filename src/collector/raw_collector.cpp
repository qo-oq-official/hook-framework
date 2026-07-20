#include "collector/raw_collector.h"
#include "output/hook_data_source.h"
#include "logging/logger.h"
#include <Windows.h>

namespace hookfw {

static nlohmann::json ctxToJson(const HookContext& ctx) {
    nlohmann::json j;
    j["hook_id"] = ctx.hookId;
    j["rax"] = std::format("{:#x}", ctx.rax);
    j["rbx"] = std::format("{:#x}", ctx.rbx);
    j["rcx"] = std::format("{:#x}", ctx.rcx);
    j["rdx"] = std::format("{:#x}", ctx.rdx);
    j["rsi"] = std::format("{:#x}", ctx.rsi);
    j["rdi"] = std::format("{:#x}", ctx.rdi);
    j["rbp"] = std::format("{:#x}", ctx.rbp);
    j["rsp"] = std::format("{:#x}", ctx.rsp);
    j["r8"] = std::format("{:#x}", ctx.r8);
    j["r9"] = std::format("{:#x}", ctx.r9);
    j["r10"] = std::format("{:#x}", ctx.r10);
    j["r11"] = std::format("{:#x}", ctx.r11);
    j["r12"] = std::format("{:#x}", ctx.r12);
    j["r13"] = std::format("{:#x}", ctx.r13);
    j["r14"] = std::format("{:#x}", ctx.r14);
    j["r15"] = std::format("{:#x}", ctx.r15);
    j["rflags"] = std::format("{:#x}", ctx.rflags);
    j["return_address"] = std::format("{:#x}", ctx.returnAddress);

    if (ctx.stackFrameCount > 0) {
        nlohmann::json frames = nlohmann::json::array();
        for (uint32_t i = 0; i < ctx.stackFrameCount && i < 64; ++i) {
            frames.push_back(ctx.resolveFrame(ctx.stackFrames[i]));
        }
        j["callstack"] = frames;
    }
    return j;
}

void RawCollector::onEntry(const HookContext& ctx) {
    try {
        HookDataSource::TraceSlice(functionName_, moduleName_, 'B',
                                    GetCurrentThreadId(), ctxToJson(ctx));
    } catch (const std::exception& ex) {
        LOG_ERROR("RawCollector::onEntry exception: {}", ex.what());
    }
}

void RawCollector::onExit(const HookContext& ctx) {
    try {
        nlohmann::json j = ctxToJson(ctx);
        j["return_value"] = std::format("{:#x}", ctx.rax);
        HookDataSource::TraceSlice(functionName_, moduleName_, 'E',
                                    GetCurrentThreadId(), j);
    } catch (const std::exception& ex) {
        LOG_ERROR("RawCollector::onExit exception: {}", ex.what());
    }
}

}

#include "api/hook_api.h"
#include "logging/logger.h"
#include "logging/crash_log.h"
#include "git_hash.h"
#include <cstdlib>
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <format>
#include <chrono>

namespace {

std::filesystem::path getDllDirectory() {
    char dllPath[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&getDllDirectory), &hSelf);
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
    return std::filesystem::path(dllPath).parent_path();
}

std::filesystem::path getDirectoryByEnv(const std::string &envName ) {
    const char* path_val = std::getenv(envName.c_str());
    if (path_val != nullptr) {
        hookfw::crash_log::write((envName + ": " + std::string{path_val} + "\n").c_str());
        return std::filesystem::path(path_val);
    }
    hookfw::crash_log::write((envName + ": null\n").c_str());
    return getDllDirectory();
}

std::wstring GetProcessNameViaCmd() {
    int argc;
    // 将整个命令行拆分为标准的参数数组
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != NULL && argc > 0) {
        std::wstring fullPath(argv[0]);
        LocalFree(argv); // 必须释放内存

        size_t lastSlash = fullPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            return fullPath.substr(lastSlash + 1);
        }
        return fullPath;
    }
    return L"";
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            auto dllDir = getDllDirectory();
            auto earlyLogPath = (dllDir / "hook_early.log").string();
            hookfw::crash_log::init(earlyLogPath.c_str(), false);
            hookfw::crash_log::write("[1] DllMain ATTACH entered\n");

            try {
                hookfw::crash_log::write("[2] getDllDirectory OK\n");
                uint64_t pid = GetCurrentProcessId();
                std::wstring processName = GetProcessNameViaCmd();
                std::wstring processCmd {GetCommandLineW()};
                auto configDir = getDirectoryByEnv("HOOK_CONFIG_DIR");
                auto outputDir = getDirectoryByEnv("HOOK_OUTPUT_DIR");
                auto now = std::chrono::system_clock::now();
                auto zoned_now = std::chrono::zoned_time{std::chrono::current_zone(), now};
                std::string dt_str = std::format("{:%Y%m%d%H%M%S}", zoned_now);
                auto logPath = (outputDir / ("hook_framework-" + std::filesystem::path(processName).stem().generic_string()  + "-" + dt_str + "-" + std::to_string(pid) + ".log")).string();
                auto configPath = (configDir / (std::filesystem::path(processName).stem().generic_string() + "_hooks_config.json")).string();

                hookfw::crash_log::write(("dllDir: " + dllDir.string() + "\n").c_str());
                hookfw::crash_log::write(("Git hash: " + std::string(HOOKFW_GIT_HASH) + "\n").c_str());
                hookfw::crash_log::write("Log paths:\n");
                hookfw::crash_log::write(("  early: " + earlyLogPath + "\n").c_str());
                hookfw::crash_log::write(("  main:  " + logPath + "\n").c_str());
                hookfw::crash_log::write(("  crash: " + (dllDir / "hook_crash.log").string() + "\n").c_str());
                hookfw::crash_log::write(("  attach: " + (dllDir / "hook_attach_err.log").string() + "\n").c_str());
                hookfw::Logger::instance().initialize(logPath);
                hookfw::crash_log::write("[3] Logger initialized\n");
                LOG_INFO("=== Hook Framework DLL loaded ===");
                LOG_INFO("Git hash: {}", HOOKFW_GIT_HASH);
                LOG_INFO("DLL directory: {}", dllDir.string());
                LOG_INFO("Config path: {}", configPath);
                LOG_INFO("Process ID: {}", pid);
                LOG_INFO(L"Process CMD: {}", processCmd);

                if (std::filesystem::exists(configPath)) {
                    hookfw::HookEngine::instance().setDllDirectory(dllDir.string());
                    hookfw::crash_log::write("[4] Loading config...\n");
                    hookfw::initializeFramework(configPath);
                    hookfw::crash_log::write("[5] Framework init complete\n");
                    LOG_INFO("=== Framework initialization complete ===");
                } else {
                    hookfw::crash_log::write("[4] Config not found, API mode\n");
                    hookfw::HookEngine::instance().initialize();
                }
            } catch (const std::exception& ex) {
                std::string msg = "[CRASH] Exception: " + std::string(ex.what()) + "\n";
                hookfw::crash_log::write(msg.c_str());
                try { LOG_CRITICAL("DllMain ATTACH exception: {}", ex.what()); } catch (...) {}
            } catch (...) {
                hookfw::crash_log::write("[CRASH] Unknown exception\n");
            }
            try { hookfw::Logger::instance().get()->flush(); } catch (...) {}
            hookfw::crash_log::write("[6] DllMain ATTACH done\n");
            break;
        }
        case DLL_PROCESS_DETACH: {
            try {
                hookfw::crash_log::write("[DETACH] DLL unloading, starting shutdown...\n");
                LOG_INFO("=== Hook Framework DLL unloading ===");
                hookfw::shutdownFramework();
                hookfw::crash_log::write("[DETACH] shutdownFramework returned\n");
            } catch (const std::exception& ex) {
                OutputDebugStringA(("DllMain DETACH exception: " + std::string(ex.what()) + "\n").c_str());
            } catch (...) {
                OutputDebugStringA("DllMain DETACH: unknown exception\n");
            }
            break;
        }
    }
    return TRUE;
}

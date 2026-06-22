#include "api/hook_api.h"
#include "logging/logger.h"
#include "logging/crash_log.h"
#include "git_hash.h"
#include <Windows.h>
#include <filesystem>

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

                auto configPath = (dllDir / "hooks.json").string();
                auto logPath = (dllDir / "hook_framework.log").string();

            hookfw::crash_log::write(("dllDir: " + dllDir.string() + "\n").c_str());
            hookfw::crash_log::write(("Git hash: " + std::string(HOOKFW_GIT_HASH) + "\n").c_str());
            hookfw::Logger::instance().initialize(logPath);
            hookfw::crash_log::write("[3] Logger initialized\n");
            LOG_INFO("=== Hook Framework DLL loaded ===");
            LOG_INFO("Git hash: {}", HOOKFW_GIT_HASH);
            LOG_INFO("DLL directory: {}", dllDir.string());
                LOG_INFO("Config path: {}", configPath);
                LOG_INFO("Process ID: {}", GetCurrentProcessId());

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
                LOG_INFO("=== Hook Framework DLL unloading ===");
                hookfw::shutdownFramework();
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

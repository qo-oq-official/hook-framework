#include "api/hook_api.h"
#include "logging/logger.h"
#include "git_hash.h"
#include <Windows.h>
#include <filesystem>

namespace {

static void earlyLog(const char* msg) {
    static HANDLE hFile = INVALID_HANDLE_VALUE;
    if (hFile == INVALID_HANDLE_VALUE) {
        char dllPath[MAX_PATH] = {};
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&earlyLog), &hSelf);
        GetModuleFileNameA(hSelf, dllPath, MAX_PATH);
        std::string logPath(dllPath);
        auto pos = logPath.rfind('\\');
        if (pos != std::string::npos) logPath = logPath.substr(0, pos + 1);
        logPath += "hook_early.log";
        hFile = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
        FlushFileBuffers(hFile);
    }
}

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
            earlyLog("[1] DllMain ATTACH entered\n");
            try {
                auto dllDir = getDllDirectory();
                earlyLog("[2] getDllDirectory OK\n");

                auto configPath = (dllDir / "hooks.json").string();
                auto logPath = (dllDir / "hook_framework.log").string();

            hookfw::Logger::instance().initialize(logPath);
            earlyLog("[3] Logger initialized\n");
            LOG_INFO("=== Hook Framework DLL loaded ===");
            LOG_INFO("Git hash: {}", HOOKFW_GIT_HASH);
            LOG_INFO("DLL directory: {}", dllDir.string());
                LOG_INFO("Config path: {}", configPath);
                LOG_INFO("Process ID: {}", GetCurrentProcessId());

                if (std::filesystem::exists(configPath)) {
                    hookfw::HookEngine::instance().setDllDirectory(dllDir.string());
                    earlyLog("[4] Loading config...\n");
                    hookfw::initializeFramework(configPath);
                    earlyLog("[5] Framework init complete\n");
                    LOG_INFO("=== Framework initialization complete ===");
                } else {
                    earlyLog("[4] Config not found, API mode\n");
                    hookfw::HookEngine::instance().initialize();
                }
            } catch (const std::exception& ex) {
                std::string msg = "[CRASH] Exception: " + std::string(ex.what()) + "\n";
                earlyLog(msg.c_str());
                try { LOG_CRITICAL("DllMain ATTACH exception: {}", ex.what()); } catch (...) {}
            } catch (...) {
                earlyLog("[CRASH] Unknown exception\n");
            }
            try { hookfw::Logger::instance().get()->flush(); } catch (...) {}
            earlyLog("[6] DllMain ATTACH done\n");
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

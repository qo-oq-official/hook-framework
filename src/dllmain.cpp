#include "api/hook_api.h"
#include "logging/logger.h"
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
            try {
                auto dllDir = getDllDirectory();
                SetCurrentDirectoryW(dllDir.wstring().c_str());

                auto configPath = (dllDir / "hooks.json").string();
                auto logPath = (dllDir / "hook_framework.log").string();

                hookfw::Logger::instance().initialize(logPath);
                LOG_INFO("=== Hook Framework DLL loaded ===");
                LOG_INFO("DLL directory: {}", dllDir.string());
                LOG_INFO("Config path: {}", configPath);

                if (std::filesystem::exists(configPath)) {
                    hookfw::initializeFramework(configPath);
                } else {
                    LOG_WARN("Config file not found: {}, waiting for API calls", configPath);
                    hookfw::HookEngine::instance().initialize();
                }
            } catch (const std::exception& ex) {
                LOG_CRITICAL("DllMain ATTACH exception: {}", ex.what());
            } catch (...) {
                OutputDebugStringA("DllMain ATTACH: unknown exception\n");
            }
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

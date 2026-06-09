#include "api/hook_api.h"
#include "logging/logger.h"
#include <Windows.h>
#include <filesystem>

namespace {

std::string getConfigPath() {
    char dllPath[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&getConfigPath), &hSelf);
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);

    std::filesystem::path p(dllPath);
    auto configPath = p.parent_path() / "hooks.json";
    return configPath.string();
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            try {
                std::string configPath = getConfigPath();
                hookfw::Logger::instance().initialize();
                LOG_INFO("=== Hook Framework DLL loaded ===");
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

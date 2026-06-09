#include "internal_funcs.h"
#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// ============================================================
// 辅助: 计算函数RVA
// ============================================================

static uintptr_t getModuleBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
}

template<typename T>
static uint64_t getRva(T funcPtr) {
    return reinterpret_cast<uintptr_t>(funcPtr) - getModuleBase();
}

// 获取成员函数地址 (MSVC简单继承)
template<typename T>
static uint64_t getMemberRva(T memberPtr) {
    union { T ptr; uintptr_t addr; } u;
    u.ptr = memberPtr;
    return u.addr - getModuleBase();
}

// ============================================================
// 替换函数示例: 替换 secret_compute
// ============================================================

static int hooked_secret_compute(int a, int b, int mode) {
    std::cout << "    [HOOK] secret_compute intercepted: a=" << a
              << " b=" << b << " mode=" << mode << "\n";
    return 0xDEAD;
}

// ============================================================
// main
// ============================================================

static const char* DUMP_DIR = "dump_data_rva_demo";

int main() {
    std::cout << "=== RVA Hook Demo ===\n";
    std::cout << "Demonstrates hooking non-exported / C++ / namespace functions via RVA\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    HMODULE hSelf = GetModuleHandleA(nullptr);
    auto base = reinterpret_cast<uintptr_t>(hSelf);

    // --- 打印所有目标函数的RVA ---
    std::cout << "[RVA Table] Module base = 0x" << std::hex << base << std::dec << "\n";
    uint64_t rvaSecretCompute = getRva(&secret_compute);
    uint64_t rvaPhysics = getRva(&physics_gravity);
    uint64_t rvaAuth = getRva(&auth_check);
    uint64_t rvaSend = getMemberRva(&NetworkSession::sendPacket);
    uint64_t rvaRecv = getMemberRva(&NetworkSession::recvPacket);
    uint64_t rvaAuthenticate = getMemberRva(&NetworkSession::authenticate);
    uint64_t rvaChecksum = getRva(&NetworkSession::checksum);
    uint64_t rvaDrawRect = getRva(&engine::render::drawRect);
    uint64_t rvaBlend = getRva(&engine::render::blendColors);

    std::cout << "  secret_compute:               0x" << std::hex << rvaSecretCompute << "\n";
    std::cout << "  physics_gravity:              0x" << rvaPhysics << "\n";
    std::cout << "  auth_check:                   0x" << rvaAuth << "\n";
    std::cout << "  NetworkSession::sendPacket:   0x" << rvaSend << "\n";
    std::cout << "  NetworkSession::recvPacket:   0x" << rvaRecv << "\n";
    std::cout << "  NetworkSession::authenticate: 0x" << rvaAuthenticate << "\n";
    std::cout << "  NetworkSession::checksum:     0x" << rvaChecksum << "\n";
    std::cout << "  engine::render::drawRect:     0x" << rvaDrawRect << "\n";
    std::cout << "  engine::render::blendColors:  0x" << rvaBlend << std::dec << "\n\n";

    // --- 初始化 hook framework ---
    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    std::string selfModule = "rva_hook_demo.exe";

    // --- Hook 1: 非导出静态函数 (参数采集) ---
    std::cout << "[1] Hook non-exported function: secret_compute (capture)\n";
    engine.hookByRva(selfModule, rvaSecretCompute)
        .params({{"a", hookfw::ParamType::Int32},
                 {"b", hookfw::ParamType::Int32},
                 {"mode", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    // --- Hook 2: 非导出静态函数 (替换) ---
    std::cout << "[2] Hook non-exported function: auth_check (replace)\n";
    engine.hookByRva(selfModule, rvaAuth)
        .replace(reinterpret_cast<void*>(&hooked_secret_compute))
        .install();

    // --- Hook 3: C++ 静态成员函数 (参数采集) ---
    std::cout << "[3] Hook C++ static method: NetworkSession::checksum (capture)\n";
    engine.hookByRva(selfModule, rvaChecksum)
        .params({{"data", hookfw::ParamType::Pointer},
                 {"len", hookfw::ParamType::UInt64}})
        .returnType(hookfw::ParamType::UInt32)
        .install();

    // --- Hook 4: 命名空间函数 (参数采集) ---
    std::cout << "[4] Hook namespace function: engine::render::drawRect (capture)\n";
    engine.hookByRva(selfModule, rvaDrawRect)
        .params({{"x", hookfw::ParamType::Int32},
                 {"y", hookfw::ParamType::Int32},
                 {"w", hookfw::ParamType::Int32},
                 {"h", hookfw::ParamType::Int32}})
        .install();

    // --- Hook 5: 命名空间函数 (参数采集) ---
    std::cout << "[5] Hook namespace function: engine::render::blendColors (capture)\n";
    engine.hookByRva(selfModule, rvaBlend)
        .params({{"src", hookfw::ParamType::UInt32},
                 {"dst", hookfw::ParamType::UInt32},
                 {"alpha", hookfw::ParamType::Float32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    std::cout << "\n[*] Installing all hooks...\n";
    bool ok = engine.installAll();
    if (!ok) {
        std::cerr << "FATAL: Failed to install hooks\n";
        return 1;
    }
    std::cout << "[*] All hooks active\n\n";

    // --- 调用被hook的函数 ---
    std::cout << "--- Calling hooked functions ---\n\n";

    // 非导出函数
    std::cout << "[Call] secret_compute(10, 7, 2) = " << secret_compute(10, 7, 2) << "\n";
    std::cout << "[Call] secret_compute(255, 128, 0) = " << secret_compute(255, 128, 0) << "\n";

    // 替换的函数 (auth_check被替换为hooked_secret_compute)
    std::cout << "[Call] auth_check(\"admin\", \"12345678\") = "
              << auth_check("admin", "12345678") << "  (replaced)\n";

    // C++ 静态方法
    uint8_t testData[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint32_t cksum = NetworkSession::checksum(testData, 5);
    std::cout << "[Call] NetworkSession::checksum([01..05], 5) = 0x"
              << std::hex << cksum << std::dec << "\n";

    // 命名空间函数
    engine::render::drawRect(100, 200, 50, 30, 0xFF0000);
    std::cout << "[Call] engine::render::drawRect(100, 200, 50, 30, 0xFF0000)\n";

    int blended = engine::render::blendColors(0xFF0000, 0x0000FF, 0.5f);
    std::cout << "[Call] engine::render::blendColors(0xFF0000, 0x0000FF, 0.5) = 0x"
              << std::hex << blended << std::dec << "\n";

    // --- 验证输出 ---
    std::cout << "\n--- Verifying output ---\n";
    engine.uninstallAll();

    int fileCount = 0;
    if (fs::exists(DUMP_DIR)) {
        for (auto& entry : fs::recursive_directory_iterator(DUMP_DIR)) {
            if (entry.is_regular_file()) fileCount++;
        }
    }
    std::cout << "Output files: " << fileCount << "\n";

    if (fileCount > 0) {
        std::cout << "\nDump directory listing:\n";
        for (auto& entry : fs::recursive_directory_iterator(DUMP_DIR)) {
            if (entry.is_regular_file()) {
                auto rel = fs::relative(entry.path(), DUMP_DIR);
                std::cout << "  " << rel.string();
                if (entry.path().extension() == ".json") {
                    std::ifstream f(entry.path());
                    nlohmann::json j;
                    f >> j;
                    if (j.contains("params")) {
                        std::cout << "  <- params captured";
                    }
                    if (j.contains("return_value")) {
                        std::cout << "  <- return=" << j["return_value"]["value"];
                    }
                }
                std::cout << "\n";
            }
        }
    }

    std::cout << "\n=== Demo Complete ===\n";
    return (fileCount >= 8) ? 0 : 1;
}

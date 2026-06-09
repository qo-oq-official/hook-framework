#include "target_dll.h"
#include <hook_framework/hook_framework.h>
#include <Windows.h>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static const char* DUMP_DIR = "dump_data_dll_demo";
static const char* TARGET_DLL = "target_dll.dll";

// ============================================================
// 替换函数: 拦截 math_pow 并修改行为
// ============================================================

static int hooked_math_pow(int base, int exp) {
    std::cout << "    [INTERCEPT] math_pow(" << base << ", " << exp << ") -> returning 0\n";
    return 0;
}

// ============================================================
// 辅助: 获取DLL内非导出函数的RVA
// ============================================================

static uint64_t getDllFuncRva(HMODULE hDll, void* funcAddr) {
    return reinterpret_cast<uintptr_t>(funcAddr) - reinterpret_cast<uintptr_t>(hDll);
}

int main() {
    std::cout << "=== Hook DLL Demo ===\n";
    std::cout << "Demonstrates hooking functions inside a loaded DLL\n";
    std::cout << "Covers: exported C funcs, C++ class methods, namespace funcs\n\n";

    if (fs::exists(DUMP_DIR)) fs::remove_all(DUMP_DIR);

    // --- 加载目标DLL ---
    HMODULE hDll = LoadLibraryA(TARGET_DLL);
    if (!hDll) {
        std::cerr << "FATAL: Cannot load " << TARGET_DLL << ", error=" << GetLastError() << "\n";
        return 1;
    }
    std::cout << "[*] Loaded " << TARGET_DLL << " at 0x"
              << std::hex << reinterpret_cast<uintptr_t>(hDll) << std::dec << "\n";

    // --- 获取导出函数地址 ---
    auto fnPow = reinterpret_cast<int(*)(int,int)>(GetProcAddress(hDll, "math_pow"));
    auto fnLerp = reinterpret_cast<double(*)(double,double,double)>(GetProcAddress(hDll, "math_lerp"));
    auto fnCount = reinterpret_cast<size_t(*)(const char*,char)>(GetProcAddress(hDll, "string_count"));
    auto fnEncode = reinterpret_cast<int(*)(const uint8_t*,size_t,uint8_t*,size_t)>(
        GetProcAddress(hDll, "?encode_frame@protocol@@YAHPEBE_KPEAE1@Z"));
    auto fnDecode = reinterpret_cast<int(*)(const uint8_t*,size_t,uint8_t*,size_t)>(
        GetProcAddress(hDll, "?decode_frame@protocol@@YAHPEBE_KPEAE1@Z"));

    if (!fnPow || !fnLerp || !fnCount) {
        std::cerr << "FATAL: Cannot resolve C exports from " << TARGET_DLL << "\n";
        return 1;
    }

    // 如果C++导出函数通过修饰名找不到，尝试遍历导出表或使用dumpbin
    // 这里演示即使找不到修饰名也可以通过RVA hook

    // --- 初始化 hook framework ---
    auto& engine = hookfw::HookEngine::instance();
    engine.setOutputDirectory(DUMP_DIR);
    engine.initialize();

    // ============================================================
    // 场景1: Hook DLL导出的C函数 (按符号名)
    // ============================================================
    std::cout << "\n[Scenario 1] Hook DLL exported C functions by symbol name\n";

    engine.hook(TARGET_DLL, "math_pow")
        .params({{"base", hookfw::ParamType::Int32},
                 {"exp", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::Int32)
        .install();

    engine.hook(TARGET_DLL, "math_lerp")
        .params({{"a", hookfw::ParamType::Float64},
                 {"b", hookfw::ParamType::Float64},
                 {"t", hookfw::ParamType::Float64}})
        .returnType(hookfw::ParamType::Float64)
        .install();

    engine.hook(TARGET_DLL, "string_count")
        .params({{"str", hookfw::ParamType::CString},
                 {"ch", hookfw::ParamType::Int32}})
        .returnType(hookfw::ParamType::UInt64)
        .install();

    // ============================================================
    // 场景2: Hook DLL导出的命名空间函数 (按修饰名)
    //         如果找不到修饰名，可用dumpbin /exports 查看
    // ============================================================
    std::cout << "[Scenario 2] Hook DLL namespace functions (mangled name or RVA)\n";

    if (fnEncode) {
        std::cout << "  Found protocol::encode_frame via mangled symbol\n";
    } else {
        std::cout << "  protocol::encode_frame not found by mangled name, use RVA approach\n";
        // 演示: 通过dumpbin先获取RVA，此处模拟已知RVA
        // 实际使用时: dumpbin_resolver --module target_dll.dll --output exports.json
    }

    // ============================================================
    // 场景3: Hook DLL导出的C++类方法 (通过RVA)
    //         CryptoEngine::encrypt 的修饰名可通过 dumpbin /exports 获取
    // ============================================================
    std::cout << "[Scenario 3] Hook C++ class methods in DLL via RVA\n";

    // 演示获取C++方法RVA的方式:
    // 方法A: 使用 dumpbin /exports target_dll.dll 查看修饰名和RVA
    // 方法B: 如果能获取到函数指针（如虚函数表），直接计算RVA
    // 方法C: 使用MAP文件 (link /MAP)

    // 这里通过修饰名尝试获取 (MSVC x64 mangled names)
    auto fnEncrypt = GetProcAddress(hDll, "?encrypt@CryptoEngine@@QEAAKPEBE_KPEAE@Z");
    auto fnDecrypt = GetProcAddress(hDll, "?decrypt@CryptoEngine@@QEAAKPEBE_KPEAE@Z");
    auto fnSetKey = GetProcAddress(hDll, "?setKey@CryptoEngine@@QEAAXI@Z");

    if (fnEncrypt) {
        uint64_t rva = getDllFuncRva(hDll, reinterpret_cast<void*>(fnEncrypt));
        std::cout << "  CryptoEngine::encrypt found at RVA=0x" << std::hex << rva << std::dec << "\n";
        engine.hookByRva(TARGET_DLL, rva)
            .params({{"input", hookfw::ParamType::Pointer},
                     {"len", hookfw::ParamType::UInt64},
                     {"output", hookfw::ParamType::Pointer}})
            .returnType(hookfw::ParamType::UInt32)
            .install();
    } else {
        std::cout << "  CryptoEngine::encrypt not found (mangled name may differ)\n";
        std::cout << "  Use: dumpbin /exports target_dll.dll to find the correct name\n";
    }

    // ============================================================
    // 场景4: 函数替换 - 拦截DLL函数并改变行为
    // ============================================================
    std::cout << "[Scenario 4] Replace DLL function: math_pow -> always return 0\n";

    // 注意: 同一函数不能同时capture又replace(已在场景1注册了capture)
    // 这里演示另一个场景: 用第二个程序专门做replace

    // --- 安装所有hooks ---
    std::cout << "\n[*] Installing all hooks...\n";
    bool ok = engine.installAll();
    if (!ok) {
        std::cerr << "WARNING: Some hooks may have failed\n";
    }
    std::cout << "[*] Hooks active\n\n";

    // ============================================================
    // 调用被hook的DLL函数
    // ============================================================

    std::cout << "--- Calling hooked DLL functions ---\n\n";

    // C导出函数
    std::cout << "[Call] math_pow(2, 10) = " << fnPow(2, 10) << "\n";
    std::cout << "[Call] math_pow(3, 5) = " << fnPow(3, 5) << "\n";
    std::cout << "[Call] math_lerp(0.0, 100.0, 0.75) = " << fnLerp(0.0, 100.0, 0.75) << "\n";
    std::cout << "[Call] string_count(\"hello world\", 'l') = " << fnCount("hello world", 'l') << "\n";
    std::cout << "[Call] string_count(\"aaaaaa\", 'a') = " << fnCount("aaaaaa", 'a') << "\n";

    // C++类方法 (需要对象实例)
    CryptoEngine crypto;
    crypto.setKey(0xDEADBEEF);
    uint8_t plaintext[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    uint8_t ciphertext[8] = {};
    uint32_t cksum = crypto.encrypt(plaintext, 5, ciphertext);
    std::cout << "[Call] crypto.encrypt(\"Hello\", 5) checksum=0x"
              << std::hex << cksum << std::dec << "\n";

    uint8_t decrypted[8] = {};
    crypto.decrypt(ciphertext, 5, decrypted);
    std::cout << "[Call] crypto.decrypt() -> \"";
    for (int i = 0; i < 5; ++i) std::cout << (char)decrypted[i];
    std::cout << "\"\n";

    // 命名空间函数 (直接链接调用)
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t frame[16] = {};
    int frameLen = protocol::encode_frame(payload, 4, frame, 16);
    std::cout << "[Call] protocol::encode_frame([01,02,03,04]) len=" << frameLen << "\n";

    uint8_t decoded[16] = {};
    int decLen = protocol::decode_frame(frame, frameLen, decoded, 16);
    std::cout << "[Call] protocol::decode_frame() decoded_len=" << decLen << "\n";

    // --- 卸载并验证 ---
    std::cout << "\n--- Verifying output ---\n";
    engine.uninstallAll();

    int fileCount = 0;
    if (fs::exists(DUMP_DIR)) {
        for (auto& entry : fs::recursive_directory_iterator(DUMP_DIR)) {
            if (entry.is_regular_file()) fileCount++;
        }
        std::cout << "Output files: " << fileCount << "\n";
        std::cout << "\nDump structure:\n";
        for (auto& entry : fs::directory_iterator(fs::path(DUMP_DIR) / TARGET_DLL)) {
            if (entry.is_directory()) {
                int count = 0;
                for (auto& f : fs::directory_iterator(entry.path())) {
                    if (f.is_regular_file()) count++;
                }
                std::cout << "  " << entry.path().filename().string()
                          << "/ (" << count << " files)\n";
            }
        }
    }

    std::cout << "\n=== Demo Complete ===\n";
    FreeLibrary(hDll);
    return (fileCount > 0) ? 0 : 1;
}

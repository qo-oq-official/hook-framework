#include "target_funcs.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <Windows.h>

static wchar_t g_concatBuf[1024];

// ============================================================
// 导出函数 (extern "C", dllexport)
// ============================================================

extern "C" {

TARGET_API __declspec(noinline) int add_numbers(int a, int b) {
    return a + b;
}

TARGET_API __declspec(noinline) int multiply_numbers(int a, int b) {
    return a * b;
}

TARGET_API __declspec(noinline) const wchar_t* concat_strings(const wchar_t* a, const wchar_t* b) {
    wcscpy_s(g_concatBuf, a);
    wcscat_s(g_concatBuf, b);
    return g_concatBuf;
}

TARGET_API __declspec(noinline) void* alloc_buffer(size_t size) {
    return malloc(size);
}

TARGET_API __declspec(noinline) void free_buffer(void* ptr) {
    free(ptr);
}

}

// ============================================================
// 非导出的静态函数 (只能通过RVA hook)
// ============================================================

static __declspec(noinline) int internal_calculate(int x, int y, int op) {
    switch (op) {
        case 0: return x + y;
        case 1: return x - y;
        case 2: return x * y;
        case 3: return (y != 0) ? x / y : 0;
        default: return 0;
    }
}

static __declspec(noinline) double internal_distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

static __declspec(noinline) bool internal_validate(const char* data, size_t len) {
    if (!data || len == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\0') return false;
    }
    return true;
}

// ============================================================
// C++ 类方法 (name mangling, 只能通过RVA或修饰名hook)
// ============================================================

class DataProcessor {
public:
    __declspec(noinline) int transform(int input, int factor) {
        lastResult_ = input * factor + offset_;
        callCount_++;
        return lastResult_;
    }

    __declspec(noinline) std::string format(const std::string& prefix, int value) {
        std::string result = prefix + "=" + std::to_string(value);
        callCount_++;
        return result;
    }

    __declspec(noinline) static double computeAverage(const int* data, size_t count) {
        if (!data || count == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += data[i];
        }
        return sum / count;
    }

    int getCallCount() const { return callCount_; }
    void setOffset(int off) { offset_ = off; }

private:
    int lastResult_ = 0;
    int callCount_ = 0;
    int offset_ = 0;
};

// ============================================================
// 命名空间内函数
// ============================================================

namespace crypto {

__declspec(noinline) uint32_t simple_hash(const uint8_t* data, size_t len) {
    uint32_t hash = 0x811c9dc5;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 0x01000193;
    }
    return hash;
}

__declspec(noinline) void xor_encrypt(uint8_t* buf, size_t len, uint8_t key) {
    for (size_t i = 0; i < len; ++i) {
        buf[i] ^= key;
    }
}

}

// ============================================================
// 函数指针 / std::function (模拟运行时绑定的回调)
// ============================================================

typedef int (*BinaryOp)(int, int);

static __declspec(noinline) int callback_power(int base, int exp) {
    int result = 1;
    for (int i = 0; i < exp; ++i) result *= base;
    return result;
}

static __declspec(noinline) int callback_modulo(int a, int b) {
    return (b != 0) ? a % b : 0;
}

// ============================================================
// 打印RVA辅助 (方便用户获取非导出函数的RVA)
// ============================================================

static void printRvaInfo() {
    HMODULE hSelf = GetModuleHandleA(nullptr);
    auto base = reinterpret_cast<uintptr_t>(hSelf);

    std::cout << "\n--- Non-exported function RVAs (for hook config) ---\n";
    std::cout << "  Module base:           0x" << std::hex << base << "\n";
    std::cout << "  internal_calculate:    RVA=0x" << (reinterpret_cast<uintptr_t>(&internal_calculate) - base) << "\n";
    std::cout << "  internal_distance:     RVA=0x" << (reinterpret_cast<uintptr_t>(&internal_distance) - base) << "\n";
    std::cout << "  internal_validate:     RVA=0x" << (reinterpret_cast<uintptr_t>(&internal_validate) - base) << "\n";
    {
        union { int(DataProcessor::*mp)(int,int); uintptr_t addr; } u;
        u.mp = &DataProcessor::transform;
        std::cout << "  DataProcessor::transform:    RVA=0x" << (u.addr - base) << "\n";
    }
    std::cout << "  DataProcessor::computeAverage: RVA=0x" << (reinterpret_cast<uintptr_t>(&DataProcessor::computeAverage) - base) << "\n";
    std::cout << "  crypto::simple_hash:   RVA=0x" << (reinterpret_cast<uintptr_t>(&crypto::simple_hash) - base) << "\n";
    std::cout << "  crypto::xor_encrypt:   RVA=0x" << (reinterpret_cast<uintptr_t>(&crypto::xor_encrypt) - base) << "\n";
    std::cout << "  callback_power:        RVA=0x" << (reinterpret_cast<uintptr_t>(&callback_power) - base) << "\n";
    std::cout << "  callback_modulo:       RVA=0x" << (reinterpret_cast<uintptr_t>(&callback_modulo) - base) << "\n";
    std::cout << std::dec << "---\n\n";
}

// ============================================================
// main
// ============================================================

typedef int (*FnAdd)(int, int);
typedef int (*FnMul)(int, int);
typedef const wchar_t* (*FnConcat)(const wchar_t*, const wchar_t*);
typedef void* (*FnAlloc)(size_t);
typedef void (*FnFree)(void*);

int main() {
    std::cout << "=== Target App Started ===\n";
    std::cout << "PID: " << GetCurrentProcessId() << "\n";

    printRvaInfo();

    std::cout << "Press Enter to run test functions...\n";
    std::cin.get();

    // --- 导出函数调用 ---
    std::cout << "[Exported functions]\n";
    HMODULE hSelf = GetModuleHandleA(nullptr);
    auto fnAdd = reinterpret_cast<FnAdd>(GetProcAddress(hSelf, "add_numbers"));
    auto fnMul = reinterpret_cast<FnMul>(GetProcAddress(hSelf, "multiply_numbers"));
    auto fnConcat = reinterpret_cast<FnConcat>(GetProcAddress(hSelf, "concat_strings"));
    auto fnAlloc = reinterpret_cast<FnAlloc>(GetProcAddress(hSelf, "alloc_buffer"));
    auto fnFree = reinterpret_cast<FnFree>(GetProcAddress(hSelf, "free_buffer"));

    std::cout << "  add_numbers(3, 4) = " << fnAdd(3, 4) << "\n";
    std::cout << "  multiply_numbers(5, 6) = " << fnMul(5, 6) << "\n";
    const wchar_t* result = fnConcat(L"Hello, ", L"World!");
    std::wcout << L"  concat_strings = " << result << L"\n";
    void* buf = fnAlloc(256);
    std::cout << "  alloc_buffer(256) = 0x" << std::hex << buf << std::dec << "\n";
    fnFree(buf);

    // --- 非导出静态函数调用 (通过函数指针防止内联) ---
    std::cout << "\n[Internal static functions]\n";
    volatile auto pfnCalc = &internal_calculate;
    volatile auto pfnDist = &internal_distance;
    volatile auto pfnValid = &internal_validate;

    std::cout << "  internal_calculate(10, 3, 2) = " << pfnCalc(10, 3, 2) << "\n";
    std::cout << "  internal_calculate(100, 7, 1) = " << pfnCalc(100, 7, 1) << "\n";
    std::cout << "  internal_distance(0,0, 3,4) = " << pfnDist(0.0, 0.0, 3.0, 4.0) << "\n";
    const char* testData = "ABCDEF";
    std::cout << "  internal_validate(\"ABCDEF\", 6) = " << pfnValid(testData, 6) << "\n";

    // --- C++ 类方法调用 ---
    std::cout << "\n[C++ class methods]\n";
    DataProcessor proc;
    proc.setOffset(5);
    std::cout << "  proc.transform(10, 3) = " << proc.transform(10, 3) << "\n";
    std::cout << "  proc.transform(7, 2) = " << proc.transform(7, 2) << "\n";
    std::string formatted = proc.format("result", 42);
    std::cout << "  proc.format(\"result\", 42) = " << formatted << "\n";

    int data[] = {10, 20, 30, 40, 50};
    double avg = DataProcessor::computeAverage(data, 5);
    std::cout << "  DataProcessor::computeAverage([10..50]) = " << avg << "\n";

    // --- 命名空间函数调用 ---
    std::cout << "\n[Namespace functions]\n";
    uint8_t hashInput[] = "Hello Hook Framework";
    uint32_t h = crypto::simple_hash(hashInput, sizeof(hashInput) - 1);
    std::cout << "  crypto::simple_hash(\"Hello Hook Framework\") = 0x" << std::hex << h << std::dec << "\n";

    uint8_t encBuf[] = {0x41, 0x42, 0x43, 0x44};
    crypto::xor_encrypt(encBuf, 4, 0xFF);
    std::cout << "  crypto::xor_encrypt([41,42,43,44], key=0xFF) = ["
              << std::hex << (int)encBuf[0] << "," << (int)encBuf[1] << ","
              << (int)encBuf[2] << "," << (int)encBuf[3] << "]" << std::dec << "\n";

    // --- 函数指针/回调调用 ---
    std::cout << "\n[Function pointer callbacks]\n";
    volatile BinaryOp op1 = &callback_power;
    volatile BinaryOp op2 = &callback_modulo;
    std::cout << "  callback_power(2, 10) = " << op1(2, 10) << "\n";
    std::cout << "  callback_modulo(17, 5) = " << op2(17, 5) << "\n";

    std::cout << "\n=== Test Complete. Press Enter to exit ===\n";
    std::cin.get();
    return 0;
}

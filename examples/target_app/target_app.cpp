#include "target_funcs.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <Windows.h>

static wchar_t g_concatBuf[1024];

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

typedef int (*FnAdd)(int, int);
typedef int (*FnMul)(int, int);
typedef const wchar_t* (*FnConcat)(const wchar_t*, const wchar_t*);
typedef void* (*FnAlloc)(size_t);
typedef void (*FnFree)(void*);

int main() {
    std::cout << "=== Target App Started ===\n";
    std::cout << "PID: " << GetCurrentProcessId() << "\n";
    std::cout << "Press Enter to run test functions...\n";
    std::cin.get();

    HMODULE hSelf = GetModuleHandleA(nullptr);
    auto fnAdd = reinterpret_cast<FnAdd>(GetProcAddress(hSelf, "add_numbers"));
    auto fnMul = reinterpret_cast<FnMul>(GetProcAddress(hSelf, "multiply_numbers"));
    auto fnConcat = reinterpret_cast<FnConcat>(GetProcAddress(hSelf, "concat_strings"));
    auto fnAlloc = reinterpret_cast<FnAlloc>(GetProcAddress(hSelf, "alloc_buffer"));
    auto fnFree = reinterpret_cast<FnFree>(GetProcAddress(hSelf, "free_buffer"));

    std::cout << "Calling add_numbers(3, 4) = " << fnAdd(3, 4) << "\n";
    std::cout << "Calling multiply_numbers(5, 6) = " << fnMul(5, 6) << "\n";

    const wchar_t* result = fnConcat(L"Hello, ", L"World!");
    std::wcout << L"Calling concat_strings = " << result << L"\n";

    void* buf = fnAlloc(256);
    std::cout << "Calling alloc_buffer(256) = 0x" << std::hex << buf << std::dec << "\n";
    fnFree(buf);
    std::cout << "Calling free_buffer done\n";

    std::cout << "\n=== Test Complete. Press Enter to exit ===\n";
    std::cin.get();
    return 0;
}

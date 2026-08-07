#include <Windows.h>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <string>

namespace fs = std::filesystem;

static int g_ok = 0, g_bad = 0;
#define CHECK(cond, msg) do { if (cond) { std::cout << "  [PASS] " << msg << "\n"; g_ok++; } else { std::cout << "  [FAIL] " << msg << "\n"; g_bad++; } } while(0)

typedef int      (*FnAdd)(int,int);
typedef int      (*FnSub)(int,int);
typedef double   (*FnMulDouble)(double,double);
typedef float    (*FnDivFloat)(float,float);
typedef int64_t  (*FnMulI64)(int64_t,int64_t);
typedef int      (*FnSum4)(int,int,int,int);
typedef const char*   (*FnEchoStr)(const char*);
typedef const wchar_t*(*FnConcatW)(const wchar_t*,const wchar_t*);
typedef int      (*FnReadInt)(int*);
typedef int      (*FnReadDouble)(double*);
typedef int      (*FnReadPair)(int*,int*);
typedef const char*   (*FnFormat)(const char*,int);
typedef void*    (*FnAlloc)(size_t);
typedef void     (*FnFree)(void*);
typedef void     (*FnSetFlag)(int*,int);
typedef int      (*FnIsPositive)(int);
typedef int      (*FnDeepL3)(int);
typedef int      (*FnDeepCall)(int);
typedef int      (*FnSum6)(int,int,int,int,int,int);
typedef int      (*FnSum9)(int,int,int,int,int,int,double,double,double);

static std::string g_dllDir;

static HMODULE loadTarget() {
    std::string path = g_dllDir + "\\test_pdb_target.dll";
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) std::cerr << "[ERROR] Cannot load: " << path << "\n";
    return h;
}

template<typename T>
T getProc(HMODULE h, const char* name) {
    T fn = reinterpret_cast<T>(GetProcAddress(h, name));
    if (!fn) std::cerr << "[WARN]  " << name << " not found\n";
    return fn;
}

int main(int argc, char* argv[]) {
    int waitSec = 0;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--wait") == 0) {
            waitSec = atoi(argv[i + 1]);
        }
    }

    // Get current exe directory as DLL location
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    g_dllDir = fs::path(exePath).parent_path().string();

    if (waitSec > 0) {
        std::cout << "PID=" << GetCurrentProcessId() << "  Waiting " << waitSec << "s for injection...\n" << std::flush;
        Sleep(waitSec * 1000);
    }

    std::cout << "=== PDB Hook Framework Test ===\n\n";

    HMODULE h = loadTarget();
    CHECK(h != nullptr, "test_pdb_target.dll loaded");
    if (!h) return 1;

    auto fnAdd         = getProc<FnAdd>(h, "pdb_add");
    auto fnSub         = getProc<FnSub>(h, "pdb_sub");
    auto fnMulDouble   = getProc<FnMulDouble>(h, "pdb_mul_double");
    auto fnDivFloat    = getProc<FnDivFloat>(h, "pdb_div_float");
    auto fnMulI64      = getProc<FnMulI64>(h, "pdb_mul_i64");
    auto fnSum4        = getProc<FnSum4>(h, "pdb_sum4");
    auto fnEchoStr     = getProc<FnEchoStr>(h, "pdb_echo_str");
    auto fnConcatW     = getProc<FnConcatW>(h, "pdb_concat_w");
    auto fnReadInt     = getProc<FnReadInt>(h, "pdb_read_int");
    auto fnReadDouble  = getProc<FnReadDouble>(h, "pdb_read_double");
    auto fnReadPair    = getProc<FnReadPair>(h, "pdb_read_pair");
    auto fnFormat      = getProc<FnFormat>(h, "pdb_format");
    auto fnSetFlag     = getProc<FnSetFlag>(h, "pdb_set_flag");
    auto fnIsPositive  = getProc<FnIsPositive>(h, "pdb_is_positive");
    auto fnDeepCall    = getProc<FnDeepCall>(h, "pdb_deep_call");
    auto fnSum6        = getProc<FnSum6>(h, "pdb_sum6");
    auto fnSum9        = getProc<FnSum9>(h, "pdb_sum9");

    CHECK(fnAdd != nullptr, "pdb_add resolved");
    CHECK(fnSub != nullptr, "pdb_sub resolved");
    CHECK(fnMulDouble != nullptr, "pdb_mul_double resolved");
    CHECK(fnDivFloat != nullptr, "pdb_div_float resolved");
    CHECK(fnMulI64 != nullptr, "pdb_mul_i64 resolved");
    CHECK(fnSum4 != nullptr, "pdb_sum4 resolved");
    CHECK(fnEchoStr != nullptr, "pdb_echo_str resolved");
    CHECK(fnConcatW != nullptr, "pdb_concat_w resolved");
    CHECK(fnFormat != nullptr, "pdb_format resolved");
    CHECK(fnSetFlag != nullptr, "pdb_set_flag resolved");
    CHECK(fnIsPositive != nullptr, "pdb_is_positive resolved");
    CHECK(fnDeepCall != nullptr, "pdb_deep_call resolved");
    CHECK(fnSum6 != nullptr, "pdb_sum6 resolved");
    CHECK(fnSum9 != nullptr, "pdb_sum9 resolved");

    // Call functions and verify
    std::cout << "\n[1] Calling all functions ...\n";

    CHECK(fnAdd(7, 8) == 15, "pdb_add(7,8) == 15");
    CHECK(fnSub(100, 37) == 63, "pdb_sub(100,37) == 63");
    CHECK(std::abs(fnMulDouble(3.0, 4.0) - 12.0) < 0.001, "pdb_mul_double(3,4) ~= 12");
    CHECK(std::abs(fnDivFloat(10.0f, 3.0f) - 3.3333f) < 0.01f, "pdb_div_float(10,3) ~= 3.33");
    CHECK(fnMulI64(1000000, 999999) == 999999000000LL, "pdb_mul_i64(1000000,999999)");
    CHECK(fnSum4(1, 2, 3, 4) == 10, "pdb_sum4(1,2,3,4) == 10");

    CHECK(strcmp(fnEchoStr("hello"), "hello") == 0, "pdb_echo_str('hello')");
    CHECK(wcscmp(fnConcatW(L"foo", L"bar"), L"foobar") == 0, "pdb_concat_w('foo','bar')");

    int outInt = 0;
    CHECK(fnReadInt(&outInt) == 1 && outInt == 12345, "pdb_read_int to 12345");
    double outDouble = 0;
    CHECK(fnReadDouble(&outDouble) == 1 && std::abs(outDouble - 3.14159) < 0.001, "pdb_read_double to 3.14159");
    int outA = 0, outB = 0;
    CHECK(fnReadPair(&outA, &outB) == 2 && outA == 100 && outB == 200, "pdb_read_pair to (100,200)");

    CHECK(strncmp(fnFormat("test_%d", 42), "test_42", 7) == 0, "pdb_format('test_%d', 42)");

    int flag = 0;
    fnSetFlag(&flag, 99);
    CHECK(flag == 99, "pdb_set_flag to 99");
    CHECK(fnIsPositive(5) == 1, "pdb_is_positive(5) == 1");
    CHECK(fnIsPositive(-1) == 0, "pdb_is_positive(-1) == 0");

    CHECK(fnDeepCall(5) == -1, "pdb_deep_call(5) == -1 (l3 threw, caught by l0)");
    CHECK(fnSum6(1, 2, 3, 4, 5, 6) == 21, "pdb_sum6(1,2,3,4,5,6) == 21");
    CHECK(fnSum9(1, 2, 3, 4, 5, 6, 7, 8, 9) == 21, "pdb_sum9(1,2,3,4,5,6,7,8,9) == 45");

    std::cout << "\n=== Results: " << g_ok << " passed, " << g_bad << " failed ===\n";

    FreeLibrary(h);
    return g_bad > 0 ? 1 : 0;
}

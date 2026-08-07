#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <stdexcept>

#ifdef TEST_PDB_EXPORTS
#define TEST_API __declspec(dllexport)
#else
#define TEST_API __declspec(dllimport)
#endif
namespace {
// ── deep call chain with exception ─────────────────────────────────────
__declspec(noinline) int pdb_deep_l3(int x) { 
    throw;
    return x * 2; 
}
}
__declspec(noinline) int pdb_deep_l2(int x) {
    return pdb_deep_l3(x) + 1;
}

__declspec(noinline) int pdb_deep_l1(int x) {
    return pdb_deep_l2(x) + 1;
}

__declspec(noinline) int pdb_deep_call(int x) {
    try {
        return pdb_deep_l1(x) + 1;
    } catch (...) {
        return -1;  // exception indicator
    }
}

extern "C" {

// ── scalar params ─────────────────────────────────────────────────────

TEST_API __declspec(noinline) int pdb_add(int a, int b) {
    return a + b;
}

TEST_API __declspec(noinline) int pdb_sub(int a, int b) {
    return a - b;
}

TEST_API __declspec(noinline) double pdb_mul_double(double a, double b) {
    return a * b;
}

TEST_API __declspec(noinline) float pdb_div_float(float a, float b) {
    return a / b;
}

TEST_API __declspec(noinline) int64_t pdb_mul_i64(int64_t a, int64_t b) {
    return a * b;
}

TEST_API __declspec(noinline) int pdb_sum4(int a, int b, int c, int d) {
    return a + b + c + d;
}

// ── string params ─────────────────────────────────────────────────────

TEST_API __declspec(noinline) const char* pdb_echo_str(const char* msg) {
    return msg;
}

TEST_API __declspec(noinline) const wchar_t* pdb_concat_w(const wchar_t* a, const wchar_t* b) {
    static wchar_t buf[512];
    wcscpy_s(buf, a);
    wcscat_s(buf, b);
    return buf;
}

// ── pointer output params ─────────────────────────────────────────────

TEST_API __declspec(noinline) int pdb_read_int(int* out) {
    if (out) *out = 12345;
    return 1;
}

TEST_API __declspec(noinline) int pdb_read_double(double* out) {
    if (out) *out = 3.14159265;
    return 1;
}

TEST_API __declspec(noinline) int pdb_read_pair(int* outA, int* outB) {
    if (outA) *outA = 100;
    if (outB) *outB = 200;
    return 2;
}

// ── mixed params ──────────────────────────────────────────────────────

TEST_API __declspec(noinline) const char* pdb_format(const char* fmt, int value) {
    static char buf[256];
    sprintf_s(buf, fmt, value);
    return buf;
}

TEST_API __declspec(noinline) void* pdb_alloc(size_t size) {
    return malloc(size);
}

TEST_API __declspec(noinline) void pdb_free(void* ptr) {
    free(ptr);
}

// ── void return / bool ─────────────────────────────────────────────────

TEST_API __declspec(noinline) void pdb_set_flag(int* flag, int val) {
    if (flag) *flag = val;
}

TEST_API __declspec(noinline) int pdb_is_positive(int x) {
    return x > 0 ? 1 : 0;
}


// ── >4 args test (exercises stack parameter path) ─────────────────────

TEST_API __declspec(noinline) int pdb_sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

TEST_API __declspec(noinline) int pdb_sum9(int a, int b, int c, int d, int e, int f, double j, double i, double k) {
    return a + b + c + d + e + f + i + j +k;
}

}



#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef TEST_TARGET_EXPORTS
#define TEST_API __declspec(dllexport)
#else
#define TEST_API __declspec(dllimport)
#endif

static wchar_t g_buf[1024];

extern "C" {

TEST_API __declspec(noinline) int test_add(int a, int b) {
    return a + b;
}

TEST_API __declspec(noinline) int test_sub(int a, int b) {
    return a - b;
}

TEST_API __declspec(noinline) int64_t test_multiply_i64(int64_t a, int64_t b) {
    return a * b;
}

TEST_API __declspec(noinline) double test_double_add(double a, double b) {
    return a + b;
}

TEST_API __declspec(noinline) const char* test_echo_str(const char* msg) {
    return msg;
}

TEST_API __declspec(noinline) const wchar_t* test_concat_w(const wchar_t* a, const wchar_t* b) {
    wcscpy_s(g_buf, a);
    wcscat_s(g_buf, b);
    return g_buf;
}

TEST_API __declspec(noinline) void* test_alloc(size_t size) {
    return malloc(size);
}

TEST_API __declspec(noinline) void test_free(void* ptr) {
    free(ptr);
}

TEST_API __declspec(noinline) int test_sum4(int a, int b, int c, int d) {
    return a + b + c + d;
}

TEST_API __declspec(noinline) int test_read_int(int* out) {
    if (out) *out = 12345;
    return 1;
}

TEST_API __declspec(noinline) int test_read_double(double* out) {
    if (out) *out = 3.14159265;
    return 1;
}

TEST_API __declspec(noinline) int test_read_pair(int* outA, int* outB) {
    if (outA) *outA = 100;
    if (outB) *outB = 200;
    return 2;
}

TEST_API __declspec(noinline) int test_read_int64(int64_t* out) {
    if (out) *out = 9876543210LL;
    return 1;
}

TEST_API __declspec(noinline) int test_read_float(float* out) {
    if (out) *out = 2.718f;
    return 1;
}

}

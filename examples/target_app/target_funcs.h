#pragma once

#ifdef TARGET_APP_EXPORTS
#define TARGET_API __declspec(dllexport)
#else
#define TARGET_API __declspec(dllimport)
#endif

extern "C" {
    TARGET_API int add_numbers(int a, int b);
    TARGET_API int multiply_numbers(int a, int b);
    TARGET_API const wchar_t* concat_strings(const wchar_t* a, const wchar_t* b);
    TARGET_API void* alloc_buffer(size_t size);
    TARGET_API void free_buffer(void* ptr);
}

#pragma once
#include <atomic>
#include <string>

#ifdef HOOKFW_EXPORTS
#define INJECTOR_TEST_API __declspec(dllexport)
#else
#define INJECTOR_TEST_API __declspec(dllimport)
#endif

struct InjectorValues {
    std::atomic<int> pre_x{0};
    std::atomic<int> pre_flag{0};
    int pre_y_int = 0;
    std::string pre_name;
    std::wstring pre_wname;

    std::atomic<int> post_x{0};
    std::atomic<int> post_flag{0};
    int post_y_int = 0;
    std::string post_name;
    std::wstring post_wname;

    void reset() {
        pre_x = 0; post_x = 0;
        pre_flag = 0; post_flag = 0;
        pre_y_int = 0; post_y_int = 0;
        pre_name.clear(); post_name.clear();
        pre_wname.clear(); post_wname.clear();
    }
};

extern INJECTOR_TEST_API InjectorValues g_values;

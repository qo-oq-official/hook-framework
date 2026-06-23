#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#ifdef HOOKFW_EXPORTS
#define INJECTOR_TEST_API __declspec(dllexport)
#else
#define INJECTOR_TEST_API __declspec(dllimport)
#endif

struct StackTraceInjectorData {
    std::mutex mutex;
    std::vector<std::string> lastFrames;
    std::atomic<int> callCount{0};
};

extern INJECTOR_TEST_API StackTraceInjectorData g_stackData;

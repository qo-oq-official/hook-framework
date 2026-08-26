#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

struct RegisterContext;
namespace hookfw {
class HookDataSource {
public:
    static void Register();

    static void TraceSlice(const std::string& name,
                           const std::string& category,
                           char phase,
                           const nlohmann::json& args);

        static void TraceSlice(const std::string& name,
                           const std::string& category,
                           char phase,
                           const RegisterContext* ctx);

    static void registerHookName(uint32_t hookId, const std::string& name);
    static std::string getHookName(uint32_t hookId);

private:
    static std::unordered_map<uint32_t, std::string> hookNames_;
    static std::mutex namesMutex_;
};

}

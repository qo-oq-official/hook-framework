#include "etw/etw_loader.h"

#include "logging.h"

#include <map>
#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace hookfw {

namespace {

std::wstring toLowerW(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return out;
}

std::wstring basenameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

}  // namespace

DllLoaderEngine::~DllLoaderEngine() {
    stop();
}

bool DllLoaderEngine::start(const EtwLoaderConfig& config) {
    config_ = config;
    compiled_.clear();
    for (const auto& r : config_.rules) {
        CompiledRule cr;
        cr.onLoad = toLowerW(r.onLoad);
        cr.inject = toLowerW(r.inject);
        compiled_.emplace_back(cr);
    }

    running_ = true;
    monitor_.setSessionName(config_.sessionName);

    ImageLoadCallback imageCb = [this](ULONG processId, ULONG_PTR imageBase,
                                       const std::wstring& filePath) {
        onImageLoad(processId, imageBase, filePath);
    };

    ULONG status = monitor_.start(imageCb);
    if (status != ERROR_SUCCESS) {
        if (status == ERROR_ALREADY_EXISTS) {
            SPDLOG_ERROR(L"Failed to start ETW monitoring: session '{}' still exists and could "
                          L"not be cleaned; stop it manually with: logman stop {} -ets",
                          config_.sessionName, config_.sessionName);
        } else {
            SPDLOG_ERROR("Failed to start ETW monitoring, error={}; "
                          "run as Administrator / Performance Log Users",
                          status);
        }
        running_ = false;
        return false;
    }

    SPDLOG_INFO("ETW monitoring active ({} rule(s))", config_.rules.size());

    injectThread_ = std::thread([this]() { injectionLoop(); });
    return true;
}

void DllLoaderEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    monitor_.stop();

    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        queueCv_.notify_all();
    }

    if (injectThread_.joinable()) {
        injectThread_.join();
    }
}

void DllLoaderEngine::onImageLoad(ULONG processId, ULONG_PTR /*imageBase*/,
                                  const std::wstring& filePath) {
    const std::wstring imageLower = toLowerW(basenameOf(filePath));

    for (const auto& rule : compiled_) {
        bool hit = rule.onLoad == imageLower;
        if (!hit) {
            continue;
        }

        SPDLOG_INFO(L"[+] trigger {} in pid {}-> injecting {} dll",
                     filePath, processId, rule.inject);

        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            queue_.emplace_back(processId, std::move(rule.inject));
        }
        queueCv_.notify_one();
        break;
    }
}

void DllLoaderEngine::injectionLoop() {
    while (running_) {
        std::map<ULONG, std::vector<std::wstring>> job;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            queueCv_.wait(lk, [this]() { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) {
                break;
            }
            auto dllJob = queue_.front();
            auto [iter, isEmplace] = job.try_emplace(dllJob.first);
            iter->second.push_back(std::move(dllJob.second));
            queue_.pop_front();
        }

        for (const auto& [pid, dlls] : job) {
            for (const auto& dll : dlls) {
                std::wstring key = std::to_wstring(pid) + L"|" + toLowerW(dll);
                if (injected_.count(key)) {
                    SPDLOG_DEBUG(L"[-] skip already-injected {} into pid {}", dll, pid);
                    continue;
                }
                injected_.insert(key);
                if (injectDll(pid, dll)) {
                    SPDLOG_INFO(L"[+] injected {} into pid {}", dll, pid);
                } else {
                    SPDLOG_WARN(L"[-] failed to inject {} into pid {}", dll, pid);
                }
            }
        }
    }
}

bool DllLoaderEngine::injectDll(ULONG pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                      PROCESS_VM_OPERATION | PROCESS_VM_WRITE,
                                  FALSE, pid);
    if (!hProcess) {
        DWORD err = GetLastError();
        if (err == ERROR_INVALID_PARAMETER) {
            SPDLOG_WARN(L"target process pid={} already exited; cannot inject {}", pid, dllPath);
        } else if (err == ERROR_ACCESS_DENIED) {
            SPDLOG_WARN(L"access denied opening pid={} (run as Administrator); cannot inject {}",
                         pid, dllPath);
        } else {
            SPDLOG_WARN("OpenProcess failed pid={} err={}", pid, err);
        }
        return false;
    }

    SIZE_T pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remote, dllPath.c_str(), pathBytes, nullptr)) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    auto pLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(hKernel32, "LoadLibraryW"));

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibrary, remote, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return exitCode != 0;
}

}  // namespace hookfw

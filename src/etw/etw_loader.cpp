#include "etw/etw_loader.h"

#include "logging.h"

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
        for (const auto& b : r.onLoad) {
            cr.onLoad.push_back(toLowerW(b));
        }
        cr.inject = r.inject;
        compiled_.push_back(std::move(cr));
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
            spdlog::error(L"Failed to start ETW monitoring: session '{}' still exists and could "
                          L"not be cleaned; stop it manually with: logman stop {} -ets",
                          config_.sessionName, config_.sessionName);
        } else {
            spdlog::error("Failed to start ETW monitoring, error={}; "
                          "run as Administrator / Performance Log Users",
                          status);
        }
        running_ = false;
        return false;
    }

    spdlog::info("ETW monitoring active ({} rule(s))", config_.rules.size());

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
        bool hit = std::find(rule.onLoad.begin(), rule.onLoad.end(), imageLower) != rule.onLoad.end();
        if (!hit) {
            continue;
        }

        spdlog::info(L"[+] trigger {} in pid {}-> injecting {} dll(s)",
                     filePath, processId, rule.inject.size());

        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            queue_.emplace_back(processId, rule.inject);
        }
        queueCv_.notify_one();
        break;
    }
}

void DllLoaderEngine::injectionLoop() {
    while (running_) {
        std::pair<ULONG, std::vector<std::wstring>> job;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            queueCv_.wait(lk, [this]() { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) {
                break;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        for (const auto& dll : job.second) {
            std::wstring key = std::to_wstring(job.first) + L"|" + toLowerW(dll);
            {
                std::lock_guard<std::mutex> lk(injectedMutex_);
                if (injected_.count(key)) {
                    spdlog::debug(L"[-] skip already-injected {} into pid {}", dll, job.first);
                    continue;
                }
                injected_.insert(key);
            }

            if (injectDll(job.first, dll)) {
                spdlog::info(L"[+] injected {} into pid {}", dll, job.first);
            } else {
                spdlog::warn(L"[-] failed to inject {} into pid {}", dll, job.first);
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
            spdlog::warn(L"target process pid={} already exited; cannot inject {}", pid, dllPath);
        } else if (err == ERROR_ACCESS_DENIED) {
            spdlog::warn(L"access denied opening pid={} (run as Administrator); cannot inject {}",
                         pid, dllPath);
        } else {
            spdlog::warn("OpenProcess failed pid={} err={}", pid, err);
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

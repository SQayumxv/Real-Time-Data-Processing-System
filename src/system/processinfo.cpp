#include "system/processinfo.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <array>
#include <chrono>
#include <set>

namespace {
class Handle {
public:
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { if (valid()) CloseHandle(handle_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
};
std::uint64_t ticks(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
ReadingState failure() {
    return GetLastError() == ERROR_ACCESS_DENIED ? ReadingState::accessDenied : ReadingState::unavailable;
}
std::wstring description(const std::wstring& path) {
    DWORD ignored{};
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 4 * 1024 * 1024) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct Translation { WORD language, codepage; };
    Translation* translations{}; UINT length{};
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length)) return {};
    for (UINT i = 0; i < length / sizeof(Translation); ++i) {
        wchar_t key[80]{};
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[i].language, translations[i].codepage);
        wchar_t* value{}; UINT count{};
        if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &count) && count > 1) return value;
    }
    return {};
}
BOOL CALLBACK applicationWindow(HWND window, LPARAM parameter) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    DWORD cloaked{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    DWORD process{}; GetWindowThreadProcessId(window, &process);
    reinterpret_cast<std::set<std::uint32_t>*>(parameter)->insert(process);
    return TRUE;
}
}
ProcessIcon::~ProcessIcon() {
    if (smallIcon) DestroyIcon(smallIcon);
    if (largeIcon) DestroyIcon(largeIcon);
}
ProcessList ProcessSampler::sample() {
    ProcessList result;
    std::set<std::uint32_t> applications;
    EnumWindows(applicationWindow, reinterpret_cast<LPARAM>(&applications));
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) { reset(); return result; }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) { reset(); return result; }
    std::set<std::uint32_t> sampled;
    std::set<std::wstring> paths;
    const unsigned processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    do {
        ProcessData row;
        row.name = entry.szExeFile;
        row.processId = entry.th32ProcessID;
        row.parentId = entry.th32ParentProcessID;
        row.application = applications.contains(row.processId);
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.processId));
        if (process.valid()) {
            std::array<wchar_t, 32768> path{};
            DWORD length = static_cast<DWORD>(path.size());
            if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
                std::wstring executable(path.data(), length);
                row.executable = executable;
                paths.insert(executable);
                auto found = icons_.find(executable);
                if (found == icons_.end()) {
                    auto icon = std::make_shared<ProcessIcon>();
                    ExtractIconExW(executable.c_str(), 0, &icon->largeIcon, &icon->smallIcon, 1);
                    Appearance appearance{std::move(icon), description(executable)};
                    found = icons_.emplace(std::move(executable), std::move(appearance)).first;
                }
                row.icon = found->second.icon;
                row.displayName = found->second.name;
            }
            FILETIME created{}, exited{}, kernel{}, user{}, now{};
            if (GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) {
                row.creationTime = ticks(created);
                GetSystemTimeAsFileTime(&now);
                if (ticks(now) >= row.creationTime)
                    row.uptimeSec = Reading<std::uint64_t>::ready((ticks(now) - row.creationTime) / 10000000ULL);
                const double seconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                row.cpu = cpu_[row.processId].sample(row.creationTime, ticks(kernel) + ticks(user), seconds, processors);
                sampled.insert(row.processId);
            } else {
                row.cpu.state = row.uptimeSec.state = failure();
            }
            Handle memoryProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, row.processId));
            if (memoryProcess.valid()) {
                PROCESS_MEMORY_COUNTERS counters{};
                counters.cb = sizeof(counters);
                if (GetProcessMemoryInfo(memoryProcess.get(), &counters, sizeof(counters)))
                    row.memoryBytes = Reading<std::uint64_t>::ready(counters.WorkingSetSize);
                else row.memoryBytes.state = failure();
            } else row.memoryBytes.state = failure();
        } else {
            row.cpu.state = row.memoryBytes.state = row.uptimeSec.state = failure();
        }
        result.rows.push_back(std::move(row));
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) { reset(); return {}; }
    std::erase_if(cpu_, [&](const auto& item) { return !sampled.contains(item.first); });
    std::erase_if(icons_, [&](const auto& item) { return !paths.contains(item.first); });
    result.available = true;
    return result;
}

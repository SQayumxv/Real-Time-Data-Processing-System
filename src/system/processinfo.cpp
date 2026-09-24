#include "system/processinfo.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
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
}
ProcessList ProcessSampler::sample() {
    ProcessList result;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) { reset(); return result; }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) { reset(); return result; }
    std::set<std::uint32_t> sampled;
    const unsigned processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    do {
        ProcessData row;
        row.name = entry.szExeFile;
        row.processId = entry.th32ProcessID;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.processId));
        if (process.valid()) {
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
    result.available = true;
    return result;
}

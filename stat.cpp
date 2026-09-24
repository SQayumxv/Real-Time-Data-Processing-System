#include "stat.hpp"
#include <windows.h>
std::optional<CpuCounters> readCpuCounters() {
    if (GetActiveProcessorGroupCount() > 1) return std::nullopt;
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return std::nullopt;
    const auto ticks = [](FILETIME time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return CpuCounters{ticks(idle), ticks(kernel), ticks(user)};
}

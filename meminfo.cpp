#include "meminfo.hpp"
#include <windows.h>
Reading<MemoryUsage> getMemoryUsage() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys == 0 ||
        status.ullAvailPhys > status.ullTotalPhys) return {};
    return Reading<MemoryUsage>::ready({status.ullTotalPhys, status.ullAvailPhys});
}

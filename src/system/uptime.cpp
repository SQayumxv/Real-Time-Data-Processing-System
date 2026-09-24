#include "system/uptime.hpp"
#include <windows.h>

ULONGLONG getUptimeSeconds()
{
    ULONGLONG ms = GetTickCount64();
    return (ms / 1000ULL);
}

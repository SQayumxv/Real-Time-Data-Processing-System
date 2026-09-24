#pragma once
#include "core/measurement.hpp"
#include <map>
#include <memory>
#include <vector>
#include <windows.h>
struct ProcessIcon {
    HICON smallIcon{}, largeIcon{};
    ~ProcessIcon();
    ProcessIcon() = default;
    ProcessIcon(const ProcessIcon&) = delete;
    ProcessIcon& operator=(const ProcessIcon&) = delete;
};
struct ProcessData {
    std::wstring name;
    std::uint32_t processId{};
    std::uint64_t creationTime{};
    Reading<std::uint64_t> memoryBytes;
    Reading<std::uint64_t> uptimeSec;
    Reading<double> cpu;
    std::shared_ptr<const ProcessIcon> icon;
};
struct ProcessList {
    std::vector<ProcessData> rows;
    bool available = false;
};
class ProcessSampler {
public:
    ProcessList sample();
    void reset() { cpu_.clear(); }
private:
    std::map<std::uint32_t, ProcessCpuSampler> cpu_;
    std::map<std::wstring, std::shared_ptr<const ProcessIcon>> icons_;
};

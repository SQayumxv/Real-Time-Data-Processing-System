#pragma once
#include "measurement.hpp"
#include <map>
#include <vector>
struct ProcessData {
    std::wstring name;
    std::uint32_t processId{};
    std::uint64_t creationTime{};
    Reading<std::uint64_t> memoryBytes;
    Reading<std::uint64_t> uptimeSec;
    Reading<double> cpu;
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
};

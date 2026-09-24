#pragma once
#include "meminfo.hpp"
#include "processinfo.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

struct Snapshot {
    Reading<double> cpu{0, ReadingState::collecting};
    Reading<MemoryUsage> memory;
    std::uint64_t uptime{};
    ProcessList processes;
    std::chrono::steady_clock::time_point captured{};
    std::chrono::system_clock::time_point wallTime{};
    bool failed = false;
};

class SystemSampler {
public:
    Snapshot sample(bool reset);
private:
    CpuSampler cpu_;
    ProcessSampler processes_;
};

class MonitorWorker {
public:
    using Source = std::function<Snapshot(bool)>;
    explicit MonitorWorker(Source source, int intervalMs = 2000);
    ~MonitorWorker();
    void configure(int intervalMs, bool paused);
    std::optional<Snapshot> take();
    MonitorWorker(const MonitorWorker&) = delete;
    MonitorWorker& operator=(const MonitorWorker&) = delete;
private:
    void run();
    Source source_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Snapshot> pending_;
    int intervalMs_;
    bool paused_ = false, stopping_ = false;
    std::uint64_t generation_ = 0;
    std::thread thread_;
};

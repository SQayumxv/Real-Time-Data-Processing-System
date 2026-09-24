#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

double monotonicSeconds();
std::uint64_t utcTicks();

struct SampleTiming {
    double scheduled{}, started{}, completed{};
    std::uint64_t utc{}, skipped{}, generation{};
    double deadlineSeconds{};
    bool reset{};
    double processingStarted{}, collected{};
    double delayMs() const { return (started - scheduled) * 1000; }
    double processingMs() const { return (completed - (processingStarted > 0 ? processingStarted : started)) * 1000; }
    double queueMs() const { return processingStarted > 0 && collected > 0 ? std::max(0.0, processingStarted - collected) * 1000 : 0; }
    double latencyMs() const { return (completed - scheduled) * 1000; }
};

struct Distribution {
    double median{}, p95{}, p99{}, maximum{};
};

struct TimingSummary {
    Distribution scheduling, processing, latency;
    std::uint64_t samples{}, deadlineMisses{}, skipped{}, failures{};
    double lastCompleted{};
};

class TimingHistory {
public:
    void add(const SampleTiming& timing, bool failed = false);
    TimingSummary summary() const;
private:
    mutable std::mutex mutex_;
    std::array<std::array<double, 3>, 1024> values_{};
    std::size_t cursor_{}, count_{};
    TimingSummary totals_;
};

class PeriodicTask {
public:
    using Work = std::function<void(SampleTiming)>;
    PeriodicTask(Work work, int intervalMs);
    ~PeriodicTask();
    void configure(int intervalMs, bool paused);
    bool current(std::uint64_t generation) const { return generation_.load() == generation && !stopping_.load(); }
    bool highResolution() const { return highResolution_; }
    bool failed() const { return failed_.load(); }
    std::uint64_t skipped() const { return skipped_.load(); }
    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;
private:
    void run();
    Work work_;
    HANDLE stop_{}, change_{}, timer_{};
    bool highResolution_{};
    std::atomic<bool> stopping_{false}, paused_{false}, failed_{false};
    std::atomic<int> intervalMs_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> skipped_{0};
    std::thread thread_;
};

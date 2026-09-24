#include "core/timing.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

double monotonicSeconds() {
    static const double frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) / frequency;
}
std::uint64_t utcTicks() {
    FILETIME value{};
    GetSystemTimePreciseAsFileTime(&value);
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
void TimingHistory::add(const SampleTiming& timing, bool failed) {
    std::lock_guard lock(mutex_);
    values_[cursor_] = {std::max(0.0, timing.delayMs()), std::max(0.0, timing.processingMs()), std::max(0.0, timing.latencyMs())};
    cursor_ = (cursor_ + 1) % values_.size();
    count_ = std::min(count_ + 1, values_.size());
    ++totals_.samples;
    totals_.skipped += timing.skipped;
    totals_.deadlineMisses += timing.skipped + (timing.completed > timing.scheduled + timing.deadlineSeconds ? 1 : 0);
    totals_.failures += failed ? 1 : 0;
    totals_.lastCompleted = timing.completed;
    totals_.scheduling.maximum = std::max(totals_.scheduling.maximum, timing.delayMs());
    totals_.processing.maximum = std::max(totals_.processing.maximum, timing.processingMs());
    totals_.latency.maximum = std::max(totals_.latency.maximum, timing.latencyMs());
}
TimingSummary TimingHistory::summary() const {
    std::array<std::array<double, 3>, 1024> values;
    TimingSummary result;
    std::size_t count;
    { std::lock_guard lock(mutex_); values = values_; count = count_; result = totals_; }
    if (count == 0) return result;
    std::array<double, 1024> sorted{};
    const auto fill = [&](std::size_t column, Distribution& distribution) {
        for (std::size_t i = 0; i < count; ++i) sorted[i] = values[i][column];
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
        distribution.median = sorted[(count - 1) / 2];
        distribution.p95 = sorted[static_cast<std::size_t>(std::ceil(count * 0.95)) - 1];
        distribution.p99 = sorted[static_cast<std::size_t>(std::ceil(count * 0.99)) - 1];
    };
    fill(0, result.scheduling); fill(1, result.processing); fill(2, result.latency);
    return result;
}
PeriodicTask::PeriodicTask(Work work, int intervalMs) : work_(std::move(work)), intervalMs_(std::clamp(intervalMs, 1, 60000)) {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    change_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    highResolution_ = timer_ != nullptr;
    if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!stop_ || !change_ || !timer_) {
        if (stop_) CloseHandle(stop_);
        if (change_) CloseHandle(change_);
        if (timer_) CloseHandle(timer_);
        throw std::runtime_error("Could not create sampling timers");
    }
    try { thread_ = std::thread([this] { run(); }); }
    catch (...) { CloseHandle(timer_); CloseHandle(change_); CloseHandle(stop_); throw; }
}
PeriodicTask::~PeriodicTask() {
    stopping_ = true;
    SetEvent(stop_);
    if (thread_.joinable()) thread_.join();
    CloseHandle(timer_); CloseHandle(change_); CloseHandle(stop_);
}
void PeriodicTask::configure(int intervalMs, bool paused) {
    intervalMs_ = std::clamp(intervalMs, 1, 60000);
    paused_ = paused;
    ++generation_;
    SetEvent(change_);
}
void PeriodicTask::run() {
    double scheduled = monotonicSeconds();
    bool reset = true;
    auto generation = generation_.load();
    const HANDLE handles[]{stop_, change_, timer_};
    while (!stopping_) {
        if (generation != generation_.load()) {
            generation = generation_.load(); scheduled = monotonicSeconds(); reset = true;
        }
        if (paused_) {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) break;
            continue;
        }
        double now = monotonicSeconds();
        if (now < scheduled) {
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(std::ceil((scheduled - now) * 10000000.0)));
            if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) { failed_ = true; break; }
            const DWORD result = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0) break;
            if (result == WAIT_FAILED) { failed_ = true; break; }
            if (result == WAIT_OBJECT_0 + 1) continue;
            now = monotonicSeconds();
            if (now < scheduled) continue;
        }
        if (generation != generation_.load() || paused_) continue;
        const double period = intervalMs_.load() / 1000.0;
        const double late = std::max(0.0, now - scheduled);
        const auto skipped = static_cast<std::uint64_t>(late / period);
        skipped_ += skipped;
        if (late > std::max(5.0, period * 10)) reset = true;
        scheduled += static_cast<double>(skipped) * period;
        SampleTiming timing{scheduled, now, 0, utcTicks(), skipped, generation, period, reset};
        try { work_(timing); }
        catch (...) { failed_ = true; }
        reset = false;
        scheduled += period;
    }
}

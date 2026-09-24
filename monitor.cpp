#include "monitor.hpp"
#include "stat.hpp"
#include "uptime.hpp"
#include <algorithm>

Snapshot SystemSampler::sample(bool reset) {
    if (reset) { cpu_.reset(); processes_.reset(); }
    Snapshot result;
    result.cpu = cpu_.sample(readCpuCounters());
    result.memory = getMemoryUsage();
    result.uptime = getUptimeSeconds();
    result.processes = processes_.sample();
    result.captured = std::chrono::steady_clock::now();
    result.wallTime = std::chrono::system_clock::now();
    return result;
}
MonitorWorker::MonitorWorker(Source source, int intervalMs)
    : source_(std::move(source)), intervalMs_(std::max(50, intervalMs)) {
    thread_ = std::thread([this] { run(); });
}
MonitorWorker::~MonitorWorker() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    changed_.notify_one();
    if (thread_.joinable()) thread_.join();
}
void MonitorWorker::configure(int intervalMs, bool paused) {
    { std::lock_guard lock(mutex_);
      intervalMs_ = std::max(50, intervalMs);
      paused_ = paused;
      ++generation_;
      pending_.reset(); }
    changed_.notify_one();
}
std::optional<Snapshot> MonitorWorker::take() {
    std::lock_guard lock(mutex_);
    auto result = std::move(pending_);
    pending_.reset();
    return result;
}
void MonitorWorker::run() {
    std::unique_lock lock(mutex_);
    bool reset = true;
    while (!stopping_) {
        changed_.wait(lock, [this] { return stopping_ || !paused_; });
        if (stopping_) break;
        const auto generation = generation_;
        const auto next = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs_);
        lock.unlock();
        Snapshot snapshot;
        try { snapshot = source_(reset); }
        catch (...) {
            snapshot.failed = true;
            snapshot.cpu = {};
            snapshot.captured = std::chrono::steady_clock::now();
            snapshot.wallTime = std::chrono::system_clock::now();
        }
        lock.lock();
        if (stopping_) break;
        if (generation != generation_) { reset = true; continue; }
        reset = snapshot.failed;
        pending_ = std::move(snapshot);
        if (changed_.wait_until(lock, next, [this, generation] {
            return stopping_ || generation != generation_;
        })) reset = true;
    }
}

#pragma once
#include "core/buffer.hpp"
#include "core/recording.hpp"
#include "core/sensors.hpp"
#include "system/meminfo.hpp"
#include "system/processinfo.hpp"
#include <memory>

struct PipelineConfig {
    int systemMs = 250, processMs = 1000, displayMs = 100;
    SensorConfig sensors;
};
struct SystemFrame {
    Reading<double> cpu{0, ReadingState::collecting};
    Reading<MemoryUsage> memory;
    std::uint64_t uptime{};
    SampleTiming timing;
    bool failed{};
};
struct ProcessFrame {
    std::shared_ptr<const ProcessList> data;
    SampleTiming timing, dataTiming;
    bool failed{};
};
struct DashboardSnapshot {
    SystemFrame system;
    ProcessFrame processes;
    SensorFrame sensors;
    std::array<TimingSummary, 3> timings;
    std::uint64_t droppedSamples{}, abandonedSamples{};
    std::size_t queuedSamples{};
    bool paused{}, highResolution{}, timerFailed{};
    double lastOverflow{};
    std::uint64_t generation{};
};
class MonitorEngine {
public:
    explicit MonitorEngine(PipelineConfig config = {});
    ~MonitorEngine();
    void configure(PipelineConfig config, bool paused);
    DashboardSnapshot snapshot() const;
    PipelineConfig config() const;
    CsvWriter& recorder() { return *recorder_; }
    static std::vector<DataRecord> records(const DashboardSnapshot& snapshot, double now);
    MonitorEngine(const MonitorEngine&) = delete;
    MonitorEngine& operator=(const MonitorEngine&) = delete;
private:
    void collectSystem(SampleTiming timing);
    void collectProcesses(SampleTiming timing);
    void collectSensors(SampleTiming timing);
    void processSensors();
    mutable std::mutex configMutex_, stateMutex_;
    PipelineConfig config_;
    std::atomic<std::uint64_t> generation_{0}, dropped_{0}, abandoned_{0};
    std::atomic<bool> paused_{false};
    std::atomic<double> lastOverflow_{0};
    CpuSampler cpu_;
    ProcessSampler processes_;
    SensorSimulator simulator_;
    SensorProcessor processor_;
    SystemFrame system_;
    ProcessFrame process_;
    SensorFrame sensor_;
    std::array<TimingHistory, 3> timings_;
    SpscBuffer<SensorInput, 256> queue_;
    HANDLE inputReady_{}, stopProcessing_{};
    std::thread processing_;
    std::unique_ptr<CsvWriter> recorder_;
    std::unique_ptr<PeriodicTask> systemTask_, processTask_, sensorTask_;
};

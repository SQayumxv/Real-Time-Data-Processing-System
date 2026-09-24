#include "core/monitor.hpp"
#include "system/stat.hpp"
#include "system/uptime.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
DataRecord baseRecord(DataSource source, Metric metric, SampleTiming timing) {
    DataRecord record;
    record.source = source; record.metric = metric; record.timing = timing;
    return record;
}
template<class Emit> void systemRecords(const SystemFrame& frame, Emit emit) {
    auto cpu = baseRecord(DataSource::system, Metric::cpu, frame.timing);
    cpu.value = frame.cpu; emit(cpu);
    auto memory = baseRecord(DataSource::system, Metric::memory, frame.timing);
    if (frame.memory.valid()) memory.value = Reading<double>::ready(
        static_cast<double>(frame.memory.value.totalBytes - frame.memory.value.availableBytes) / 1048576.0);
    emit(memory);
    auto uptime = baseRecord(DataSource::system, Metric::uptime, frame.timing);
    if (!frame.failed) uptime.value = Reading<double>::ready(static_cast<double>(frame.uptime));
    emit(uptime);
}
template<class Emit> void processRecords(const ProcessFrame& frame, Emit emit) {
    if (!frame.data) return;
    for (const auto& row : frame.data->rows) {
        auto record = baseRecord(DataSource::process, Metric::cpu, frame.dataTiming);
        record.pid = row.processId;
        std::copy_n(row.name.data(), std::min(row.name.size(), record.name.size() - 1), record.name.data());
        record.value = row.cpu; emit(record);
        record.metric = Metric::memory;
        record.value = {static_cast<double>(row.memoryBytes.value) / 1048576.0, row.memoryBytes.state};
        emit(record);
        record.metric = Metric::uptime;
        record.value = {static_cast<double>(row.uptimeSec.value), row.uptimeSec.state};
        emit(record);
    }
}
template<class Emit> void sensorRecords(const SensorFrame& frame, Emit emit) {
    for (std::size_t i = 0; i < 3; ++i) {
        auto record = baseRecord(DataSource::simulated, static_cast<Metric>(static_cast<int>(Metric::temperature) + i), frame.timing);
        const auto& value = frame.values[i];
        record.value = value.raw; record.average = value.average; record.minimum = value.minimum; record.maximum = value.maximum;
        record.alert = value.raw.valid() && value.alert; record.sequence = frame.sequence;
        emit(record);
    }
}
}
MonitorEngine::MonitorEngine(PipelineConfig config) : config_(config), recorder_(std::make_unique<CsvWriter>()) {
    config_.systemMs = std::clamp(config_.systemMs, 50, 5000);
    config_.processMs = std::clamp(config_.processMs, 250, 10000);
    config_.displayMs = std::clamp(config_.displayMs, 50, 1000);
    config_.sensors = validated(config_.sensors);
    inputReady_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopProcessing_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!inputReady_ || !stopProcessing_) {
        if (inputReady_) CloseHandle(inputReady_);
        if (stopProcessing_) CloseHandle(stopProcessing_);
        throw std::runtime_error("Could not create pipeline events");
    }
    try {
        processing_ = std::thread([this] { processSensors(); });
        systemTask_ = std::make_unique<PeriodicTask>([this](auto timing) { collectSystem(timing); }, config_.systemMs);
        processTask_ = std::make_unique<PeriodicTask>([this](auto timing) { collectProcesses(timing); }, config_.processMs);
        sensorTask_ = std::make_unique<PeriodicTask>([this](auto timing) { collectSensors(timing); }, config_.sensors.intervalMs);
    } catch (...) {
        sensorTask_.reset(); processTask_.reset(); systemTask_.reset();
        SetEvent(stopProcessing_);
        if (processing_.joinable()) processing_.join();
        CloseHandle(inputReady_); CloseHandle(stopProcessing_); throw;
    }
}
MonitorEngine::~MonitorEngine() {
    ++generation_;
    sensorTask_.reset(); processTask_.reset(); systemTask_.reset();
    SetEvent(stopProcessing_);
    if (processing_.joinable()) processing_.join();
    recorder_->cancel();
    recorder_.reset();
    CloseHandle(inputReady_); CloseHandle(stopProcessing_);
}
PipelineConfig MonitorEngine::config() const {
    std::lock_guard lock(configMutex_); return config_;
}
void MonitorEngine::configure(PipelineConfig config, bool paused) {
    config.systemMs = std::clamp(config.systemMs, 50, 5000);
    config.processMs = std::clamp(config.processMs, 250, 10000);
    config.displayMs = std::clamp(config.displayMs, 50, 1000);
    config.sensors = validated(config.sensors);
    { std::lock_guard lock(configMutex_); config_ = config; paused_ = paused; ++generation_; }
    systemTask_->configure(config.systemMs, paused);
    processTask_->configure(config.processMs, paused);
    sensorTask_->configure(config.sensors.intervalMs, paused);
    SetEvent(inputReady_);
}
void MonitorEngine::collectSystem(SampleTiming timing) {
    if (timing.reset) cpu_.reset();
    SystemFrame frame;
    frame.timing = timing;
    try {
        frame.cpu = cpu_.sample(readCpuCounters());
        frame.memory = getMemoryUsage();
        frame.uptime = getUptimeSeconds();
    } catch (...) { frame.failed = true; frame.cpu = {}; frame.memory = {}; cpu_.reset(); }
    frame.timing.completed = monotonicSeconds(); frame.timing.utc = utcTicks();
    if (timing.generation != generation_) return;
    timings_[0].add(frame.timing, frame.failed || !frame.memory.valid() || frame.cpu.state == ReadingState::unavailable);
    { std::lock_guard lock(stateMutex_); system_ = frame; }
    systemRecords(frame, [this](const auto& record) { recorder_->submit(record); });
}
void MonitorEngine::collectProcesses(SampleTiming timing) {
    if (timing.reset) processes_.reset();
    ProcessFrame frame;
    frame.timing = timing;
    try {
        auto data = std::make_shared<ProcessList>(processes_.sample());
        frame.failed = !data->available;
        if (!frame.failed) frame.data = std::move(data);
    } catch (...) { frame.failed = true; processes_.reset(); }
    frame.timing.completed = monotonicSeconds(); frame.timing.utc = utcTicks();
    frame.dataTiming = frame.timing;
    if (timing.generation != generation_) return;
    timings_[1].add(frame.timing, frame.failed);
    { std::lock_guard lock(stateMutex_);
      if (frame.failed) { frame.data = process_.data; frame.dataTiming = process_.dataTiming; }
      process_ = frame; }
    if (!frame.failed) processRecords(frame, [this](const auto& record) { recorder_->submit(record); });
}
void MonitorEngine::collectSensors(SampleTiming timing) {
    if (timing.reset) simulator_.reset(config().sensors);
    auto input = simulator_.sample(timing);
    input.timing.collected = monotonicSeconds();
    if (timing.generation != generation_) return;
    if (!queue_.push(input)) { ++dropped_; lastOverflow_ = monotonicSeconds(); }
    else SetEvent(inputReady_);
}
void MonitorEngine::processSensors() {
    const HANDLE events[]{stopProcessing_, inputReady_};
    std::uint64_t generation = UINT64_MAX;
    while (WaitForSingleObject(stopProcessing_, 0) != WAIT_OBJECT_0) {
        SensorInput input;
        if (!queue_.pop(input)) {
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) break;
            continue;
        }
        if (input.timing.generation != generation_ || paused_) { ++abandoned_; continue; }
        if (generation != input.timing.generation || input.timing.reset) {
            processor_.reset(config().sensors); generation = input.timing.generation;
        }
        input.timing.processingStarted = monotonicSeconds();
        auto frame = processor_.process(input);
        frame.timing.completed = monotonicSeconds(); frame.timing.utc = utcTicks();
        if (input.timing.generation != generation_) { ++abandoned_; continue; }
        timings_[2].add(frame.timing);
        { std::lock_guard lock(stateMutex_); sensor_ = frame; }
        sensorRecords(frame, [this](const auto& record) { recorder_->submit(record); });
    }
}
DashboardSnapshot MonitorEngine::snapshot() const {
    DashboardSnapshot result;
    { std::lock_guard lock(stateMutex_); result.system = system_; result.processes = process_; result.sensors = sensor_; }
    for (std::size_t i = 0; i < 3; ++i) result.timings[i] = timings_[i].summary();
    const std::array<const PeriodicTask*, 3> tasks{systemTask_.get(),processTask_.get(),sensorTask_.get()};
    for (std::size_t i = 0; i < 3; ++i) {
        result.timings[i].deadlineMisses -= result.timings[i].skipped;
        result.timings[i].skipped = tasks[i]->skipped();
        result.timings[i].deadlineMisses += result.timings[i].skipped;
    }
    result.timings[2].deadlineMisses += dropped_.load();
    result.droppedSamples = dropped_; result.abandonedSamples = abandoned_; result.queuedSamples = queue_.size(); result.paused = paused_;
    result.highResolution = systemTask_->highResolution() && processTask_->highResolution() && sensorTask_->highResolution();
    result.timerFailed = systemTask_->failed() || processTask_->failed() || sensorTask_->failed();
    result.lastOverflow = lastOverflow_;
    result.generation = generation_;
    return result;
}
std::vector<DataRecord> MonitorEngine::records(const DashboardSnapshot& snapshot, double now) {
    std::vector<DataRecord> result;
    result.reserve(6 + (snapshot.processes.data ? snapshot.processes.data->rows.size() * 3 : 0));
    const auto add = [&](const DataRecord& record, bool stale) {
        result.push_back(record);
        result.back().stale = stale || snapshot.paused || record.timing.generation != snapshot.generation;
    };
    if (snapshot.system.timing.completed > 0)
        systemRecords(snapshot.system, [&](const auto& record) {
            add(record, snapshot.system.failed || now - record.timing.completed > record.timing.deadlineSeconds * 3);
        });
    if (snapshot.processes.data)
        processRecords(snapshot.processes, [&](const auto& record) {
            add(record, snapshot.processes.failed || now - record.timing.completed > record.timing.deadlineSeconds * 3);
        });
    if (snapshot.sensors.timing.completed > 0)
        sensorRecords(snapshot.sensors, [&](const auto& record) {
            add(record, now - record.timing.completed > std::max(0.1, record.timing.deadlineSeconds * 3));
        });
    return result;
}

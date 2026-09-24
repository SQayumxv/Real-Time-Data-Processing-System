#pragma once
#include "core/measurement.hpp"
#include "core/timing.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>

enum class DataSource { system, process, simulated };
enum class Metric { cpu, memory, uptime, temperature, pressure, vibration };
struct DataRecord {
    DataSource source{};
    Metric metric{};
    std::array<wchar_t, 260> name{};
    std::uint32_t pid{};
    Reading<double> value, average, minimum, maximum;
    SampleTiming timing;
    std::uint64_t sequence{};
    bool alert{}, stale{};
};
struct RecordingStatus {
    bool running{}, failed{}, stopping{};
    std::uint64_t written{}, dropped{};
    std::size_t queued{};
    DWORD error{};
};
class CsvWriter {
public:
    CsvWriter();
    ~CsvWriter();
    bool start(std::filesystem::path path, std::vector<DataRecord> initial = {}, bool snapshotOnly = false);
    void submit(const DataRecord& record);
    void finish();
    void cancel();
    RecordingStatus status() const;
    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;
private:
    void run(std::filesystem::path path, std::vector<DataRecord> initial, bool snapshotOnly);
    std::array<DataRecord, 2048> queue_{};
    mutable std::mutex mutex_;
    std::size_t read_{}, count_{};
    HANDLE wake_{}, stop_{};
    std::atomic<bool> running_{false}, accepting_{false}, finishing_{false}, failed_{false};
    std::atomic<std::uint64_t> written_{0}, dropped_{0};
    std::atomic<DWORD> error_{0};
    std::thread thread_;
};

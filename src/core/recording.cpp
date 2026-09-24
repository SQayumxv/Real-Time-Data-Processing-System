#include "core/recording.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {
std::string utf8(const wchar_t* value) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back(); return result;
}
std::string quote(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && std::string("=+-@").find(value[first]) != std::string::npos) value.insert(0, "'");
    std::string result = "\"";
    for (const char c : value) { if (c == '"') result += '"'; result += c; }
    return result + "\"";
}
std::string line(const DataRecord& record) {
    static const char* sources[]{"windows system", "windows process", "simulated sensor"};
    static const char* metrics[]{"cpu", "working memory", "uptime", "temperature", "pressure", "vibration"};
    static const char* units[]{"percent total capacity", "MiB", "seconds", "degC", "kPa", "g"};
    FILETIME ft{static_cast<DWORD>(record.timing.utc), static_cast<DWORD>(record.timing.utc >> 32)};
    SYSTEMTIME time{}; FileTimeToSystemTime(&ft, &time);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
        << 'T' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute << ':' << std::setw(2) << time.wSecond
        << '.' << std::setw(3) << time.wMilliseconds << "Z,";
    const char* metric = record.metric == Metric::memory ? (record.source == DataSource::system ? "physical memory used" : "working set") : metrics[static_cast<int>(record.metric)];
    out << sources[static_cast<int>(record.source)] << ',' << metric << ','
        << quote(utf8(record.name.data())) << ',' << record.pid << ',' << units[static_cast<int>(record.metric)] << ',';
    out << std::fixed << std::setprecision(6);
    for (const auto value : {record.value, record.average, record.minimum, record.maximum}) {
        if (value.valid()) out << value.value;
        out << ',';
    }
    out << quote(utf8(stateText(record.value.state).c_str())) << ',' << (record.stale ? "true" : "false") << ','
        << (record.alert ? "true" : "false") << ',' << record.sequence << ',' << record.timing.delayMs() << ','
        << record.timing.processingMs() << ',' << record.timing.queueMs() << ',' << record.timing.latencyMs() << ','
        << record.timing.deadlineSeconds * 1000 << ','
        << (record.timing.completed > record.timing.scheduled + record.timing.deadlineSeconds ? "true" : "false") << "\r\n";
    return out.str();
}
}
CsvWriter::CsvWriter() {
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!wake_ || !stop_) {
        if (wake_) CloseHandle(wake_);
        if (stop_) CloseHandle(stop_);
        throw std::runtime_error("Could not create recording events");
    }
}
CsvWriter::~CsvWriter() {
    cancel();
    if (thread_.joinable()) thread_.join();
    CloseHandle(wake_); CloseHandle(stop_);
}
bool CsvWriter::start(std::filesystem::path path, std::vector<DataRecord> initial, bool snapshotOnly) {
    if (running_ || initial.size() > 20000) return false;
    if (thread_.joinable()) thread_.join();
    { std::lock_guard lock(mutex_); read_ = count_ = 0; }
    ResetEvent(stop_); ResetEvent(wake_);
    written_ = dropped_ = 0; failed_ = false; error_ = 0; finishing_ = false;
    running_ = true; accepting_ = !snapshotOnly;
    try { thread_ = std::thread([this, path = std::move(path), initial = std::move(initial), snapshotOnly]() mutable {
        run(std::move(path), std::move(initial), snapshotOnly);
    }); }
    catch (...) { accepting_ = false; running_ = false; failed_ = true; error_ = ERROR_NOT_ENOUGH_MEMORY; return false; }
    return true;
}
void CsvWriter::submit(const DataRecord& record) {
    if (!accepting_) return;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { ++dropped_; return; }
    if (!accepting_) return;
    if (count_ == queue_.size()) { ++dropped_; return; }
    queue_[(read_ + count_) % queue_.size()] = record; ++count_;
    lock.unlock(); SetEvent(wake_);
}
void CsvWriter::finish() { accepting_ = false; finishing_ = true; SetEvent(wake_); }
void CsvWriter::cancel() { accepting_ = false; finishing_ = true; SetEvent(stop_); SetEvent(wake_); }
RecordingStatus CsvWriter::status() const {
    std::lock_guard lock(mutex_);
    return {running_.load(), failed_.load(), finishing_.load(), written_.load(), dropped_.load(), count_, error_.load()};
}
void CsvWriter::run(std::filesystem::path path, std::vector<DataRecord> initial, bool snapshotOnly) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
    const DWORD openError = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::uint64_t offset = 0;
    const auto fail = [&](DWORD error) { error_ = error; failed_ = true; accepting_ = false; };
    const auto write = [&](const std::string& bytes) {
        OVERLAPPED operation{};
        operation.Offset = static_cast<DWORD>(offset); operation.OffsetHigh = static_cast<DWORD>(offset >> 32);
        operation.hEvent = completed; ResetEvent(completed);
        DWORD count{};
        if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, &operation)) {
            if (GetLastError() != ERROR_IO_PENDING) { fail(GetLastError()); return false; }
            const HANDLE events[]{stop_, completed};
            const auto wait = WaitForMultipleObjects(2, events, FALSE, 2000);
            if (wait != WAIT_OBJECT_0 + 1) {
                CancelIoEx(file, &operation);
                GetOverlappedResult(file, &operation, &count, TRUE);
                if (wait != WAIT_OBJECT_0) fail(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
                return false;
            }
            if (!GetOverlappedResult(file, &operation, &count, FALSE)) { fail(GetLastError()); return false; }
        }
        if (count != bytes.size()) { fail(ERROR_WRITE_FAULT); return false; }
        offset += count; return true;
    };
    try {
        if (file == INVALID_HANDLE_VALUE || !completed) fail(openError != ERROR_SUCCESS ? openError : GetLastError());
        else if (write("\xEF\xBB\xBF" "timestamp_utc,source,metric,name,pid,units,value,average,minimum,maximum,validity,stale,alert,sequence,scheduling_ms,processing_ms,queue_ms,total_latency_ms,deadline_ms,deadline_missed\r\n")) {
            for (const auto& record : initial) {
                if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 || !write(line(record))) break;
                ++written_;
            }
            while (!failed_ && !snapshotOnly && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
                DataRecord record;
                bool found = false;
                { std::lock_guard lock(mutex_);
                  if (count_) { record = queue_[read_]; read_ = (read_ + 1) % queue_.size(); --count_; found = true; } }
                if (found) { if (!write(line(record))) { ++dropped_; break; } ++written_; }
                else {
                    if (finishing_) break;
                    const HANDLE events[]{stop_, wake_};
                    WaitForMultipleObjects(2, events, FALSE, INFINITE);
                }
            }
        }
    } catch (...) { fail(ERROR_NOT_ENOUGH_MEMORY); }
    accepting_ = false;
    if (written_ < initial.size()) dropped_ += initial.size() - written_.load();
    { std::lock_guard lock(mutex_); dropped_ += count_; count_ = 0; }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (completed) CloseHandle(completed);
    running_ = false;
}

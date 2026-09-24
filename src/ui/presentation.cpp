#include "ui/presentation.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {
int compareText(const std::wstring& a, const std::wstring& b) {
    const int result = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
    return result == CSTR_LESS_THAN ? -1 : result == CSTR_GREATER_THAN ? 1 : 0;
}
template<class T> int compareReading(const Reading<T>& a, const Reading<T>& b, bool ascending) {
    if (a.valid() != b.valid()) return a.valid() ? -1 : 1;
    if (!a.valid()) return 0;
    const int result = a.value < b.value ? -1 : a.value > b.value ? 1 : 0;
    return ascending ? result : -result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string csvText(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first != std::wstring::npos && std::wstring(L"=+-@").find(value[first]) != std::wstring::npos)
        value.insert(value.begin(), L'\'');
    std::string result = "\"";
    for (char c : utf8(value)) { if (c == '"') result += '"'; result += c; }
    return result + "\"";
}
}
std::vector<ProcessData> makeRows(std::span<const ProcessData> processes,
                                const std::wstring& filter, SortColumn column, bool ascending) {
    std::vector<ProcessData> result;
    for (const auto& process : processes) {
        if (filter.empty() || FindStringOrdinal(FIND_FROMSTART, process.name.c_str(), -1, filter.c_str(), -1, TRUE) >= 0 ||
            std::to_wstring(process.processId).find(filter) != std::wstring::npos)
            result.push_back(process);
    }
    std::sort(result.begin(), result.end(), [=](const auto& a, const auto& b) {
        int order = 0;
        switch (column) {
        case SortColumn::name: order = compareText(a.name, b.name) * (ascending ? 1 : -1); break;
        case SortColumn::pid: order = (a.processId < b.processId ? -1 : a.processId > b.processId ? 1 : 0) * (ascending ? 1 : -1); break;
        case SortColumn::cpu: order = compareReading(a.cpu, b.cpu, ascending); break;
        case SortColumn::memory: order = compareReading(a.memoryBytes, b.memoryBytes, ascending); break;
        case SortColumn::uptime: order = compareReading(a.uptimeSec, b.uptimeSec, ascending); break;
        }
        return order != 0 ? order < 0 : a.processId < b.processId;
    });
    return result;
}
std::wstring decimal(double value, int precision) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}
std::wstring durationText(std::uint64_t seconds) {
    const auto days = seconds / 86400;
    return (days ? std::to_wstring(days) + L"d " : L"") + std::to_wstring(seconds / 3600 % 24) + L"h " +
           std::to_wstring(seconds / 60 % 60) + L"m " + std::to_wstring(seconds % 60) + L"s";
}
std::array<std::wstring, 5> cells(const ProcessData& process) {
    return {process.name, std::to_wstring(process.processId),
        process.cpu.valid() ? decimal(process.cpu.value) : stateText(process.cpu.state),
        process.memoryBytes.valid() ? decimal(static_cast<double>(process.memoryBytes.value) / 1048576.0) : stateText(process.memoryBytes.state),
        process.uptimeSec.valid() ? durationText(process.uptimeSec.value) : stateText(process.uptimeSec.state)};
}
bool sameProcess(const ProcessData& a, const ProcessData& b) {
    return a.processId == b.processId && a.creationTime == b.creationTime && a.name == b.name;
}
bool exportCsv(const std::filesystem::path& path, std::span<const ProcessData> rows,
               const std::wstring& capturedAt, bool stale) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "\xEF\xBB\xBF" << "Captured at,Stale,Process,PID,CPU (% total capacity),Working set (MiB),Uptime (seconds),CPU status,Memory status,Uptime status\r\n";
    for (const auto& row : rows) {
        out << csvText(capturedAt) << ',' << (stale ? "true" : "false") << ',' << csvText(row.name) << ',' << row.processId << ',';
        if (row.cpu.valid()) out << utf8(decimal(row.cpu.value, 2));
        out << ',';
        if (row.memoryBytes.valid()) out << utf8(decimal(static_cast<double>(row.memoryBytes.value) / 1048576.0, 2));
        out << ',';
        if (row.uptimeSec.valid()) out << row.uptimeSec.value;
        out << ',' << csvText(stateText(row.cpu.state)) << ',' << csvText(stateText(row.memoryBytes.state)) << ','
            << csvText(stateText(row.uptimeSec.state)) << "\r\n";
    }
    out.flush();
    return out.good();
}

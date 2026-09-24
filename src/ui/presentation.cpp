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
}
bool processLess(const ProcessData& a, const ProcessData& b, SortColumn column, bool ascending) {
        int order = 0;
        switch (column) {
        case SortColumn::name: order = compareText(a.name, b.name) * (ascending ? 1 : -1); break;
        case SortColumn::pid: order = (a.processId < b.processId ? -1 : a.processId > b.processId ? 1 : 0) * (ascending ? 1 : -1); break;
        case SortColumn::cpu: order = compareReading(a.cpu, b.cpu, ascending); break;
        case SortColumn::memory: order = compareReading(a.memoryBytes, b.memoryBytes, ascending); break;
        case SortColumn::uptime: order = compareReading(a.uptimeSec, b.uptimeSec, ascending); break;
        }
        return order != 0 ? order < 0 : a.processId < b.processId;
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
        return processLess(a, b, column, ascending);
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
std::vector<ProcessTableRow> groupedRows(std::span<const ProcessData> processes, SortColumn column,
    bool ascending, const std::map<std::wstring, bool>& expansion, bool searching) {
    std::map<std::wstring, std::vector<ProcessData>> families;
    for (const auto& process : processes) {
        std::wstring key = process.executable;
        if (!key.empty()) {
            std::wstring normalized(key.size(), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, key.data(), static_cast<int>(key.size()),
                normalized.data(), static_cast<int>(normalized.size()), nullptr, nullptr, 0)) key = std::move(normalized);
        } else key = L"pid:" + std::to_wstring(process.processId) + L":" + std::to_wstring(process.creationTime);
        families[key].push_back(process);
    }
    struct Family { ProcessTableRow row; std::vector<ProcessData> members; };
    std::vector<Family> groups;
    for (auto& [key, members] : families) {
        std::sort(members.begin(), members.end(), [&](const auto& a, const auto& b) { return processLess(a,b,column,ascending); });
        ProcessTableRow row; row.family = key; row.process = members.front(); row.count = members.size();
        row.header = members.size() > 1;
        const auto state = expansion.find(key);
        row.expanded = state == expansion.end() ? searching : state->second;
        if (row.header) {
            row.key = L"group:" + key;
            const auto total = [&]<class T>(Reading<T> ProcessData::* field, bool maximum = false) {
                T value{};
                ReadingState invalid = ReadingState::ready;
                for (const auto& member : members) {
                    const auto reading = member.*field;
                    if (!reading.valid()) {
                        if (invalid == ReadingState::ready || reading.state == ReadingState::accessDenied) invalid = reading.state;
                    } else value = maximum ? std::max(value, reading.value) : value + reading.value;
                }
                return Reading<T>{value, invalid};
            };
            row.process.cpu = total(&ProcessData::cpu);
            row.process.memoryBytes = total(&ProcessData::memoryBytes);
            row.process.uptimeSec = total(&ProcessData::uptimeSec, true);
        } else row.key = L"process:" + std::to_wstring(row.process.processId) + L":" + std::to_wstring(row.process.creationTime);
        groups.push_back({std::move(row), std::move(members)});
    }
    std::stable_sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b) {
        return processLess(a.row.process,b.row.process,column,ascending);
    });
    std::vector<ProcessTableRow> result;
    for (auto& group : groups) {
        result.push_back(group.row);
        if (group.row.header && group.row.expanded) for (auto& process : group.members) {
            ProcessTableRow child;
            child.family = group.row.family; child.child = true;
            child.key = L"process:" + std::to_wstring(process.processId) + L":" + std::to_wstring(process.creationTime);
            child.process = std::move(process); result.push_back(std::move(child));
        }
    }
    return result;
}

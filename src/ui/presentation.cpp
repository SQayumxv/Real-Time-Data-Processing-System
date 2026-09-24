#include "ui/presentation.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>

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
ProcessTable processTable(std::span<const ProcessData> processes, const std::wstring& filter, SortColumn column,
    bool ascending, const std::map<std::wstring, bool>& expansion) {
    const auto executableKey = [](const ProcessData& process) {
        std::wstring key = process.executable;
        if (!key.empty()) {
            std::wstring normalized(key.size(), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, key.data(), static_cast<int>(key.size()),
                normalized.data(), static_cast<int>(normalized.size()), nullptr, nullptr, 0)) key = std::move(normalized);
        } else key = L"pid:" + std::to_wstring(process.processId) + L":" + std::to_wstring(process.creationTime);
        return key;
    };
    std::map<std::uint32_t, const ProcessData*> byId;
    std::map<std::wstring, const ProcessData*> apps;
    for (const auto& process : processes) {
        byId[process.processId] = &process;
        if (process.application) apps.try_emplace(executableKey(process), &process);
    }
    struct Family { ProcessTableRow row; std::vector<ProcessData> members; };
    std::map<std::wstring, Family> families;
    for (const auto& process : processes) {
        const ProcessData* root = &process;
        const ProcessData* ancestor = &process;
        bool application = false;
        std::set<std::uint32_t> visited;
        while (ancestor && visited.insert(ancestor->processId).second) {
            const auto app = apps.find(executableKey(*ancestor));
            if (app != apps.end()) {
                if (ancestor == &process || compareText(app->second->name, L"explorer.exe") != 0) {
                    root = app->second; application = true;
                }
                break;
            }
            const auto parent = byId.find(ancestor->parentId);
            if (parent == byId.end() || !ancestor->creationTime || !parent->second->creationTime ||
                parent->second->creationTime > ancestor->creationTime) break;
            ancestor = parent->second;
        }
        const auto key = executableKey(*root);
        auto& family = families[key];
        if (family.members.empty()) {
            family.row.family = key;
            family.row.process = *root;
            family.row.application = application;
        }
        family.members.push_back(process);
    }
    std::vector<Family> groups;
    const auto matches = [&](const std::wstring& value) {
        return filter.empty() || (!value.empty() && FindStringOrdinal(FIND_FROMSTART,value.c_str(),-1,filter.c_str(),-1,TRUE)>=0);
    };
    for (auto& [key, family] : families) {
        auto& row = family.row;
        auto& members = family.members;
        if (!matches(row.process.displayName) && !matches(row.process.name) &&
            std::none_of(members.begin(),members.end(),[&](const auto& p) {
                return matches(p.name) || matches(p.displayName) || matches(std::to_wstring(p.processId));
            })) continue;
        std::sort(members.begin(), members.end(), [&](const auto& a, const auto& b) { return processLess(a,b,column,ascending); });
        row.count = members.size();
        row.header = row.application || members.size() > 1;
        const auto state = expansion.find(key);
        row.expanded = state == expansion.end() ? !filter.empty() : state->second;
        if (!row.process.displayName.empty()) row.process.name = row.process.displayName;
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
        groups.push_back(std::move(family));
    }
    std::stable_sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b) {
        if (a.row.application != b.row.application) return a.row.application;
        return processLess(a.row.process,b.row.process,column,ascending);
    });
    ProcessTable result;
    for (const bool application : {true, false}) {
        const auto count = std::count_if(groups.begin(), groups.end(), [&](const auto& g) { return g.row.application == application; });
        if (!count) continue;
        ProcessTableRow section; section.section = true;
        section.key = application ? L"apps" : L"background";
        section.process.name = (application ? L"Apps (" : L"Background processes (") + std::to_wstring(count) + L")";
        result.rows.push_back(std::move(section));
        for (auto& group : groups) if (group.row.application == application) {
            result.rows.push_back(group.row);
            for (const auto& process : group.members) {
                result.processes.push_back(process);
                if (group.row.header && group.row.expanded) {
                    ProcessTableRow child;
                    child.family = group.row.family; child.child = true;
                    child.key = L"process:" + std::to_wstring(process.processId) + L":" + std::to_wstring(process.creationTime);
                    child.process = process; result.rows.push_back(std::move(child));
                }
            }
        }
    }
    return result;
}

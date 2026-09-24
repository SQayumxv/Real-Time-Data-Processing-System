#pragma once
#include "system/processinfo.hpp"
#include <array>
#include <filesystem>
#include <span>

enum class SortColumn { name, pid, cpu, memory, uptime };
struct ProcessTableRow {
    ProcessData process;
    std::wstring family, key;
    std::size_t count = 1;
    bool header{}, child{}, expanded{}, application{}, section{};
};

struct ProcessTable { std::vector<ProcessTableRow> rows; std::vector<ProcessData> processes; };
ProcessTable processTable(std::span<const ProcessData> processes, const std::wstring& filter, SortColumn column,
    bool ascending, const std::map<std::wstring, bool>& expansion);
std::vector<ProcessData> makeRows(std::span<const ProcessData> processes,
                                const std::wstring& filter, SortColumn column, bool ascending);
std::wstring decimal(double value, int precision = 1);
std::wstring durationText(std::uint64_t seconds);
std::array<std::wstring, 5> cells(const ProcessData& process);
bool sameProcess(const ProcessData& a, const ProcessData& b);

#pragma once
#include "processinfo.hpp"
#include <array>
#include <filesystem>
#include <span>

enum class SortColumn { name, pid, cpu, memory, uptime };
std::vector<ProcessData> makeRows(std::span<const ProcessData> processes,
                                const std::wstring& filter, SortColumn column, bool ascending);
std::wstring decimal(double value, int precision = 1);
std::wstring durationText(std::uint64_t seconds);
std::array<std::wstring, 5> cells(const ProcessData& process);
bool sameProcess(const ProcessData& a, const ProcessData& b);
bool exportCsv(const std::filesystem::path& path, std::span<const ProcessData> rows,
               const std::wstring& capturedAt, bool stale);

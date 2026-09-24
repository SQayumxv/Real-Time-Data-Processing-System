#pragma once
#include <array>
#include <filesystem>

struct Settings {
    int left = 100, top = 100, width = 1100, height = 780;
    bool maximized = false;
    int refreshMs = 2000, sortColumn = 3;
    bool ascending = false;
    std::array<int, 5> columns{300, 90, 160, 185, 210};
};
std::filesystem::path settingsPath();
Settings loadSettings(const std::filesystem::path& path);
bool saveSettings(const std::filesystem::path& path, const Settings& settings);

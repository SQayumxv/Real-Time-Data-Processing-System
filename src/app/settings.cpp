#include "app/settings.hpp"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>

std::filesystem::path settingsPath() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    auto path = std::filesystem::path(local) / L"RealTimeDataMonitor" / L"settings.ini";
    CoTaskMemFree(local);
    return path;
}
Settings loadSettings(const std::filesystem::path& path) {
    Settings result;
    if (path.empty()) return result;
    const auto get = [&](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"Window", key, fallback, path.c_str()));
    };
    result.left = std::clamp(get(L"Left", result.left), -100000, 100000);
    result.top = std::clamp(get(L"Top", result.top), -100000, 100000);
    result.width = std::clamp(get(L"Width", result.width), 900, 4000);
    result.height = std::clamp(get(L"Height", result.height), 600, 3000);
    result.maximized = get(L"Maximized", 0) != 0;
    result.refreshMs = get(L"RefreshMs", 2000);
    if (result.refreshMs != 500 && result.refreshMs != 1000 && result.refreshMs != 2000 && result.refreshMs != 5000)
        result.refreshMs = 2000;
    result.sortColumn = std::clamp(get(L"SortColumn", 3), 0, 4);
    result.ascending = get(L"Ascending", 0) != 0;
    for (size_t i = 0; i < result.columns.size(); ++i)
        result.columns[i] = std::clamp(get((L"Column" + std::to_wstring(i)).c_str(), result.columns[i]), 50, 1500);
    return result;
}
bool saveSettings(const std::filesystem::path& path, const Settings& settings) {
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    bool saved = true;
    const auto put = [&](const wchar_t* key, int value) {
        if (!WritePrivateProfileStringW(L"Window", key, std::to_wstring(value).c_str(), path.c_str())) saved = false;
    };
    put(L"Left", settings.left); put(L"Top", settings.top);
    put(L"Width", settings.width); put(L"Height", settings.height);
    put(L"Maximized", settings.maximized);
    put(L"RefreshMs", settings.refreshMs); put(L"SortColumn", settings.sortColumn);
    put(L"Ascending", settings.ascending);
    for (size_t i = 0; i < settings.columns.size(); ++i)
        put((L"Column" + std::to_wstring(i)).c_str(), settings.columns[i]);
    return saved;
}

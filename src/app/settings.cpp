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
    result.width = std::clamp(get(L"Width", result.width), 1000, 4000);
    result.height = std::clamp(get(L"Height", result.height), 640, 3000);
    result.maximized = get(L"Maximized", 0) != 0;
    result.pipeline.systemMs = std::clamp(get(L"SystemMs", 250), 50, 5000);
    result.pipeline.processMs = std::clamp(get(L"ProcessMs", 1000), 250, 10000);
    result.pipeline.displayMs = std::clamp(get(L"DisplayMs", 100), 50, 1000);
    auto& sensor = result.pipeline.sensors;
    sensor.intervalMs = get(L"SensorMs", 10);
    sensor.window = get(L"WindowSamples", 100);
    sensor.noisePercent = get(L"NoisePercent", 10);
    sensor.spikeEvery = get(L"SpikeEvery", 200);
    sensor.missingEvery = get(L"MissingEvery", 97);
    sensor.seed = static_cast<std::uint32_t>(get(L"Seed", 42));
    for (std::size_t i = 0; i < 3; ++i) {
        wchar_t value[64]{};
        const auto threshold = L"Threshold" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Window", threshold.c_str(), std::to_wstring(sensor.thresholds[i]).c_str(), value, 64, path.c_str());
        wchar_t* end{};
        double parsed = std::wcstod(value, &end);
        if (end != value && *end == 0) sensor.thresholds[i] = parsed;
        const auto hysteresis = L"Hysteresis" + std::to_wstring(i);
        GetPrivateProfileStringW(L"Window", hysteresis.c_str(), std::to_wstring(sensor.hysteresis[i]).c_str(), value, 64, path.c_str());
        parsed = std::wcstod(value, &end);
        if (end != value && *end == 0) sensor.hysteresis[i] = parsed;
    }
    sensor = validated(sensor);
    result.view = std::clamp(get(L"View", 0), 0, 3);
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
    put(L"SystemMs", settings.pipeline.systemMs); put(L"ProcessMs", settings.pipeline.processMs);
    put(L"DisplayMs", settings.pipeline.displayMs); put(L"SensorMs", settings.pipeline.sensors.intervalMs);
    put(L"WindowSamples", settings.pipeline.sensors.window); put(L"NoisePercent", settings.pipeline.sensors.noisePercent);
    put(L"SpikeEvery", settings.pipeline.sensors.spikeEvery); put(L"MissingEvery", settings.pipeline.sensors.missingEvery);
    if (!WritePrivateProfileStringW(L"Window", L"Seed", std::to_wstring(settings.pipeline.sensors.seed).c_str(), path.c_str())) saved = false;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!WritePrivateProfileStringW(L"Window", (L"Threshold" + std::to_wstring(i)).c_str(), std::to_wstring(settings.pipeline.sensors.thresholds[i]).c_str(), path.c_str())) saved = false;
        if (!WritePrivateProfileStringW(L"Window", (L"Hysteresis" + std::to_wstring(i)).c_str(), std::to_wstring(settings.pipeline.sensors.hysteresis[i]).c_str(), path.c_str())) saved = false;
    }
    put(L"View", settings.view); put(L"SortColumn", settings.sortColumn);
    put(L"Ascending", settings.ascending);
    for (size_t i = 0; i < settings.columns.size(); ++i)
        put((L"Column" + std::to_wstring(i)).c_str(), settings.columns[i]);
    return saved;
}

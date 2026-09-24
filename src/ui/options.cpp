#include "ui/options.hpp"
#include <cmath>
#include <string>

namespace {
struct Options {
    PipelineConfig config;
    std::array<HWND, 15> edits{};
    bool accepted{}, closed{};
    HFONT font{};
    UINT dpi = 96;
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};
const std::array<const wchar_t*, 15> labels{
    L"System interval (ms)", L"Process interval (ms)", L"Display interval (ms)", L"Sensor interval (ms)",
    L"Rolling window (samples)", L"Noise (%)", L"Spike every N (0 = off)", L"Missing every N (0 = off)",
    L"Simulation seed", L"Temperature threshold (C)", L"Temperature hysteresis", L"Pressure threshold (kPa)",
    L"Pressure hysteresis", L"Vibration threshold (g)", L"Vibration hysteresis"};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* options = reinterpret_cast<Options*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        options = static_cast<Options*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(options));
    }
    if (!options) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_CREATE) {
        options->dpi = GetDpiForWindow(window);
        options->font = CreateFontW(-MulDiv(10, static_cast<int>(options->dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const auto child = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
            HWND result = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text, WS_CHILD | WS_VISIBLE | style,
                options->px(x), options->px(y), options->px(width), options->px(height), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(options->font), TRUE);
            return result;
        };
        const auto& c = options->config;
        const auto& s = c.sensors;
        const std::array<double, 15> values{static_cast<double>(c.systemMs), static_cast<double>(c.processMs), static_cast<double>(c.displayMs),
            static_cast<double>(s.intervalMs), static_cast<double>(s.window), static_cast<double>(s.noisePercent), static_cast<double>(s.spikeEvery),
            static_cast<double>(s.missingEvery), static_cast<double>(s.seed), s.thresholds[0], s.hysteresis[0], s.thresholds[1], s.hysteresis[1], s.thresholds[2], s.hysteresis[2]};
        child(L"STATIC", L"Windows measurements and simulated sensors have independent schedules.", 0, 18, 12, 650, 24, 0);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const int x = 18 + static_cast<int>(i / 8) * 340;
            const int y = 48 + static_cast<int>(i % 8) * 38;
            child(L"STATIC", labels[i], 0, x, y + 4, 218, 24, 0);
            const auto value = i < 9 ? std::to_wstring(static_cast<std::uint64_t>(values[i])) : std::to_wstring(values[i]);
            options->edits[i] = child(L"EDIT", value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, x + 220, y, 95, 28, 200 + static_cast<int>(i));
        }
        child(L"STATIC", L"Applying settings restarts baselines and repeatable sensor sequences.", 0, 18, 360, 650, 24, 0);
        child(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, 458, 396, 95, 30, IDOK);
        child(L"BUTTON", L"Cancel", WS_TABSTOP, 568, 396, 95, 30, IDCANCEL);
        SetFocus(options->edits[0]);
        return 0;
    }
    if (message == WM_DPICHANGED) {
        const UINT previousDpi = options->dpi;
        options->dpi = HIWORD(wParam);
        HFONT replacement = CreateFontW(-MulDiv(10, static_cast<int>(options->dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT bounds{}; GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            const auto scale = [&](int value) { return MulDiv(value, static_cast<int>(options->dpi), static_cast<int>(previousDpi)); };
            MoveWindow(child, scale(bounds.left), scale(bounds.top), scale(bounds.right - bounds.left), scale(bounds.bottom - bounds.top), TRUE);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
        }
        if (options->font) DeleteObject(options->font);
        options->font = replacement;
        const auto* bounds = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        const std::array<double, 15> minimum{50,250,50,1,1,0,0,0,1,-1000000,0,-1000000,0,-1000000,0};
        const std::array<double, 15> maximum{5000,10000,1000,1000,256,100,1000000,1000000,4294967295.0,1000000,1000000,1000000,1000000,1000000,1000000};
        std::array<double, 15> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            wchar_t buffer[100]{}; GetWindowTextW(options->edits[i], buffer, 100);
            wchar_t* end{}; values[i] = std::wcstod(buffer, &end);
            if (end == buffer || *end || !std::isfinite(values[i]) || values[i] < minimum[i] || values[i] > maximum[i] || (i < 9 && std::floor(values[i]) != values[i])) {
                const auto error = std::wstring(labels[i]) + L" must be between " + std::to_wstring(minimum[i]) + L" and " + std::to_wstring(maximum[i]) + (i < 9 ? L" (whole numbers)." : L".");
                MessageBoxW(window, error.c_str(), L"Check setting", MB_ICONINFORMATION);
                SetFocus(options->edits[i]); return 0;
            }
        }
        auto& c = options->config; auto& s = c.sensors;
        c.systemMs = static_cast<int>(values[0]); c.processMs = static_cast<int>(values[1]); c.displayMs = static_cast<int>(values[2]);
        s.intervalMs = static_cast<int>(values[3]); s.window = static_cast<int>(values[4]); s.noisePercent = static_cast<int>(values[5]);
        s.spikeEvery = static_cast<int>(values[6]); s.missingEvery = static_cast<int>(values[7]); s.seed = static_cast<std::uint32_t>(values[8]);
        for (std::size_t i = 0; i < 3; ++i) { s.thresholds[i] = values[9 + i * 2]; s.hysteresis[i] = values[10 + i * 2]; }
        options->accepted = true; DestroyWindow(window); return 0;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { options->closed = true; if (options->font) DeleteObject(options->font); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
bool showOptions(HWND owner, PipelineConfig& config) {
    WNDCLASSW type{}; type.lpfnWndProc = procedure; type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = L"MonitorOptions";
    RegisterClassW(&type);
    Options options; options.config = config; options.dpi = GetDpiForWindow(owner);
    RECT parent{}; GetWindowRect(owner, &parent);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, type.lpszClassName, L"Processing settings",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, parent.left + options.px(30), parent.top + options.px(30), options.px(710), options.px(480),
        owner, nullptr, type.hInstance, &options);
    if (!window) return false;
    EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW);
    MSG message{};
    while (!options.closed) {
        const int result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
        if (result <= 0) { if (result == 0) PostQuitMessage(static_cast<int>(message.wParam)); break; }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (IsWindow(window)) DestroyWindow(window);
    EnableWindow(owner, TRUE); SetActiveWindow(owner);
    if (options.accepted) config = options.config;
    return options.accepted;
}

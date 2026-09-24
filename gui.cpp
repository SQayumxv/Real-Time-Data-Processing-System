#include "gui.hpp"
#include "monitor.hpp"
#include "presentation.hpp"
#include "settings.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <ctime>

namespace {
constexpr int searchId = 101, refreshId = 102, pauseId = 103, exportId = 104;
constexpr UINT_PTR pollTimer = 1;
const std::array<int, 4> intervals{500, 1000, 2000, 5000};
struct HistoryPoint {
    std::chrono::steady_clock::time_point time;
    Reading<double> cpu, memory;
};
struct App {
    HWND window{}, table{}, search{}, refresh{}, pause{}, exportButton{}, searchLabel{}, refreshLabel{};
    HFONT font{};
    UINT dpi = 96;
    Settings settings;
    std::filesystem::path settingsFile;
    SystemSampler sampler;
    std::unique_ptr<MonitorWorker> worker;
    Snapshot latest;
    std::vector<ProcessData> processes, rows;
    std::vector<std::array<std::wstring, 5>> textRows;
    std::deque<HistoryPoint> history;
    std::chrono::steady_clock::time_point processTime{};
    std::chrono::system_clock::time_point processWallTime{};
    bool paused = false, received = false, processStale = true, awaiting = true;
    std::wstring notice;
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    int interval() const { return settings.refreshMs; }
    bool stale() const {
        return paused || awaiting || !received || latest.failed ||
            std::chrono::steady_clock::now() - latest.captured > std::chrono::milliseconds(interval() * 3);
    }
};
std::wstring windowText(HWND window) {
    const int count = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(count) + 1, L'\0');
    GetWindowTextW(window, text.data(), count + 1);
    text.resize(static_cast<size_t>(count));
    return text;
}
std::wstring timestamp(std::chrono::system_clock::time_point time) {
    if (time == std::chrono::system_clock::time_point{}) return L"Unavailable";
    const auto raw = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_s(&local, &raw);
    wchar_t buffer[64]{};
    std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}
void font(App& app) {
    HFONT replacement = CreateFontW(-MulDiv(10, static_cast<int>(app.dpi), 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!replacement) return;
    for (HWND child : {app.table, app.search, app.refresh, app.pause, app.exportButton, app.searchLabel, app.refreshLabel})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (app.font) DeleteObject(app.font);
    app.font = replacement;
}
void layout(App& app) {
    RECT client{};
    GetClientRect(app.window, &client);
    const int margin = app.px(18), y = app.px(254), h = app.px(30);
    const int width = client.right;
    const int searchWidth = std::max(app.px(150), width - app.px(665));
    MoveWindow(app.searchLabel, margin, y + app.px(5), app.px(52), h, TRUE);
    MoveWindow(app.search, margin + app.px(58), y, searchWidth, h, TRUE);
    int x = margin + app.px(58) + searchWidth + app.px(18);
    MoveWindow(app.refreshLabel, x, y + app.px(5), app.px(58), h, TRUE);
    x += app.px(63);
    MoveWindow(app.refresh, x, y, app.px(88), app.px(180), TRUE);
    x += app.px(104);
    MoveWindow(app.pause, x, y, app.px(100), h, TRUE);
    MoveWindow(app.exportButton, x + app.px(112), y, app.px(116), h, TRUE);
    MoveWindow(app.table, margin, app.px(300), std::max(1, width - margin * 2),
               std::max<LONG>(1, client.bottom - app.px(338)), TRUE);
    InvalidateRect(app.window, nullptr, FALSE);
}
void sortHeader(App& app) {
    HWND header = ListView_GetHeader(app.table);
    for (int column = 0; column < 5; ++column) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, column, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (column == app.settings.sortColumn) item.fmt |= app.settings.ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, column, &item);
    }
}
void rebuild(App& app) {
    const int selectedIndex = ListView_GetNextItem(app.table, -1, LVNI_SELECTED);
    const int topIndex = ListView_GetTopIndex(app.table);
    std::optional<ProcessData> selected, top;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(app.rows.size())) selected = app.rows[selectedIndex];
    if (topIndex >= 0 && topIndex < static_cast<int>(app.rows.size())) top = app.rows[topIndex];
    auto rows = makeRows(app.processes, windowText(app.search),
                         static_cast<SortColumn>(app.settings.sortColumn), app.settings.ascending);
    SendMessageW(app.table, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemState(app.table, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    app.rows = std::move(rows);
    app.textRows.clear();
    for (const auto& row : app.rows) app.textRows.push_back(cells(row));
    ListView_SetItemCountEx(app.table, static_cast<int>(app.rows.size()), LVSICF_NOSCROLL);
    int restoredTop = -1;
    for (int i = 0; i < static_cast<int>(app.rows.size()); ++i) {
        if (selected && sameProcess(app.rows[i], *selected))
            ListView_SetItemState(app.table, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (top && sameProcess(app.rows[i], *top)) restoredTop = i;
    }
    if (restoredTop >= 0) {
        RECT rowRect{};
        if (ListView_GetItemRect(app.table, 0, &rowRect, LVIR_BOUNDS))
            ListView_Scroll(app.table, 0, (restoredTop - ListView_GetTopIndex(app.table)) * (rowRect.bottom - rowRect.top));
    }
    sortHeader(app);
    SendMessageW(app.table, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(app.table, nullptr, FALSE);
    InvalidateRect(app.window, nullptr, FALSE);
}
void configure(App& app) {
    app.awaiting = true;
    app.worker->configure(app.interval(), app.paused);
    app.history.push_back({std::chrono::steady_clock::now(), {}, {}});
    SetWindowTextW(app.pause, app.paused ? L"Resume" : L"Pause");
    InvalidateRect(app.window, nullptr, FALSE);
}
void accept(App& app, Snapshot snapshot) {
    app.awaiting = false;
    app.received = true;
    if (snapshot.processes.available) {
        app.processes = snapshot.processes.rows;
        app.processTime = snapshot.captured;
        app.processWallTime = snapshot.wallTime;
        app.processStale = false;
    } else app.processStale = true;
    Reading<double> memory;
    if (snapshot.memory.valid())
        memory = Reading<double>::ready(100.0 * static_cast<double>(snapshot.memory.value.totalBytes - snapshot.memory.value.availableBytes) /
                                        static_cast<double>(snapshot.memory.value.totalBytes));
    app.history.push_back({snapshot.captured, snapshot.cpu, memory});
    app.latest = std::move(snapshot);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(120);
    while (!app.history.empty() && app.history.front().time < cutoff) app.history.pop_front();
    rebuild(app);
}
void text(HDC dc, RECT rect, const std::wstring& value, COLORREF color = RGB(30, 41, 59)) {
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}
void graph(App& app, HDC dc, RECT rect, bool cpu) {
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, background);
    DeleteObject(background);
    const COLORREF color = cpu ? RGB(37, 99, 235) : RGB(5, 130, 100);
    RECT heading{rect.left + app.px(12), rect.top + app.px(4), rect.right - app.px(12), rect.top + app.px(30)};
    text(dc, heading, cpu ? L"CPU  |  0-100%  |  last 120 seconds" : L"Memory used  |  0-100%  |  last 120 seconds", color);
    RECT plot{rect.left + app.px(12), rect.top + app.px(36), rect.right - app.px(12), rect.bottom - app.px(14)};
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(225, 231, 239));
    const auto old = SelectObject(dc, grid);
    for (int i = 0; i <= 2; ++i) {
        const int y = plot.top + (plot.bottom - plot.top) * i / 2;
        MoveToEx(dc, plot.left, y, nullptr); LineTo(dc, plot.right, y);
    }
    HPEN line = CreatePen(PS_SOLID, std::max(1, app.px(2)), color);
    SelectObject(dc, line);
    const auto now = std::chrono::steady_clock::now();
    bool connected = false;
    for (const auto& point : app.history) {
        const double age = std::chrono::duration<double>(now - point.time).count();
        const auto reading = cpu ? point.cpu : point.memory;
        if (age > 120 || !reading.valid()) { connected = false; continue; }
        const int x = plot.right - static_cast<int>(age / 120.0 * (plot.right - plot.left));
        const int y = plot.bottom - static_cast<int>(reading.value / 100.0 * (plot.bottom - plot.top));
        if (connected) LineTo(dc, x, y); else MoveToEx(dc, x, y, nullptr);
        connected = true;
    }
    SelectObject(dc, old);
    DeleteObject(line); DeleteObject(grid);
}
void render(App& app, HDC dc, RECT client) {
    HBRUSH background = CreateSolidBrush(RGB(243, 246, 250));
    FillRect(dc, &client, background); DeleteObject(background);
    auto oldFont = SelectObject(dc, app.font);
    SetBkMode(dc, TRANSPARENT);
    const int m = app.px(18), right = client.right - m;
    const bool stale = app.stale();
    std::wstring cpu = app.latest.cpu.valid() ? decimal(app.latest.cpu.value) + L"%" : stateText(app.latest.cpu.state);
    if (app.awaiting && !app.paused) cpu = L"Collecting...";
    text(dc, {m, app.px(12), right, app.px(42)}, L"Real-Time Data Monitor  |  CPU: " + cpu +
         (stale && app.received ? L"  |  Retained readings are stale" : L""));
    std::wstring memory = L"Memory: " + stateText(app.latest.memory.state);
    if (app.latest.memory.valid()) {
        const auto& value = app.latest.memory.value;
        memory = L"Memory: " + decimal(static_cast<double>(value.totalBytes - value.availableBytes) / 1073741824.0, 2) +
            L" GiB used  /  " + decimal(static_cast<double>(value.totalBytes) / 1073741824.0, 2) + L" GiB total  |  " +
            decimal(static_cast<double>(value.availableBytes) / 1073741824.0, 2) + L" GiB available";
    }
    text(dc, {m, app.px(44), right, app.px(70)}, memory);
    text(dc, {m, app.px(70), right, app.px(96)}, L"System uptime: " +
         (app.received && !app.latest.failed ? durationText(app.latest.uptime) : L"Collecting...") +
         L"  |  Process CPU is a percentage of total machine capacity");
    const int middle = client.right / 2;
    graph(app, dc, {m, app.px(106), middle - app.px(6), app.px(236)}, true);
    graph(app, dc, {middle + app.px(6), app.px(106), right, app.px(236)}, false);
    std::wstring status = app.paused ? L"Paused - stale snapshot" :
        app.awaiting ? L"Collecting..." : app.latest.failed ? L"Collection failed - stale snapshot" :
        app.stale() ? L"Collection delayed - stale snapshot" : L"Live";
    status += L"  |  " + std::to_wstring(app.rows.size()) + L" / " + std::to_wstring(app.processes.size()) + L" processes";
    if (app.processStale) status += app.processes.empty() ? L"  |  Process list unavailable" : L"  |  Process list stale";
    status += L"  |  Updated " + timestamp(app.processWallTime);
    if (!app.notice.empty()) status += L"  |  " + app.notice;
    text(dc, {m, client.bottom - app.px(32), right, client.bottom - app.px(4)}, status,
         stale || app.processStale ? RGB(146, 64, 14) : RGB(51, 65, 85));
    SelectObject(dc, oldFont);
}
void paintRegion(App& app, HDC target, RECT update) {
    RECT client{};
    GetClientRect(app.window, &client);
    if (client.right > 0 && client.bottom > 0) {
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        if (buffer && bitmap) {
            const auto old = SelectObject(buffer, bitmap);
            render(app, buffer, client);
            BitBlt(target, update.left, update.top,
                update.right - update.left, update.bottom - update.top,
                buffer, update.left, update.top, SRCCOPY);
            SelectObject(buffer, old);
        } else render(app, target, client);
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
    }
}
void paint(App& app) {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(app.window, &ps);
    paintRegion(app, target, ps.rcPaint);
    EndPaint(app.window, &ps);
}
void exportRows(App& app) {
    wchar_t path[32768] = L"processes.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    if (!exportCsv(path, app.rows, timestamp(app.processWallTime), app.stale() || app.processStale))
        MessageBoxW(app.window, L"The CSV file could not be written.", L"Export failed", MB_OK | MB_ICONERROR);
    else app.notice = L"CSV exported";
    InvalidateRect(app.window, nullptr, FALSE);
}
void persist(App& app) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(app.window, &placement)) {
        app.settings.left = placement.rcNormalPosition.left;
        app.settings.top = placement.rcNormalPosition.top;
        MONITORINFO monitorInfo{sizeof(monitorInfo), {}, {}, 0};
        if (GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
            app.settings.left += monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
            app.settings.top += monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
        }
        app.settings.width = MulDiv(placement.rcNormalPosition.right - placement.rcNormalPosition.left, 96, static_cast<int>(app.dpi));
        app.settings.height = MulDiv(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top, 96, static_cast<int>(app.dpi));
        app.settings.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }
    for (int i = 0; i < 5; ++i)
        app.settings.columns[i] = MulDiv(ListView_GetColumnWidth(app.table, i), 96, static_cast<int>(app.dpi));
    saveSettings(app.settingsFile, app.settings);
}
bool createControls(App& app, HINSTANCE instance) {
    const auto child = [&](const wchar_t* type, const wchar_t* label, DWORD style, int id, DWORD extended = 0) {
        return CreateWindowExW(extended, type, label, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    app.searchLabel = child(L"STATIC", L"&Search", 0, 0);
    app.search = child(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, searchId, WS_EX_CLIENTEDGE);
    SendMessageW(app.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Process name or PID"));
    app.refreshLabel = child(L"STATIC", L"Re&fresh", 0, 0);
    app.refresh = child(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, refreshId);
    for (const auto* label : {L"0.5 sec", L"1 sec", L"2 sec", L"5 sec"})
        SendMessageW(app.refresh, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    const auto selected = std::find(intervals.begin(), intervals.end(), app.settings.refreshMs) - intervals.begin();
    SendMessageW(app.refresh, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    app.pause = child(L"BUTTON", L"Pause", WS_TABSTOP | BS_PUSHBUTTON, pauseId);
    app.exportButton = child(L"BUTTON", L"Export CSV", WS_TABSTOP | BS_PUSHBUTTON, exportId);
    app.table = child(WC_LISTVIEWW, L"Processes", WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                       105, WS_EX_CLIENTEDGE);
    if (!app.search || !app.refresh || !app.pause || !app.exportButton || !app.table) return false;
    ListView_SetExtendedListViewStyle(app.table, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(app.table, L"Explorer", nullptr);
    const std::array<const wchar_t*, 5> labels{L"Process", L"PID", L"CPU (%)", L"Working set (MiB)", L"Uptime"};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.pszText = const_cast<wchar_t*>(labels[i]);
        column.cx = app.px(app.settings.columns[i]);
        column.fmt = i > 0 && i < 4 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        ListView_InsertColumn(app.table, i, &column);
    }
    font(app);
    sortHeader(app);
    layout(app);
    return true;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        app->dpi = GetDpiForWindow(window);
        if (!createControls(*app, reinterpret_cast<CREATESTRUCTW*>(lParam)->hInstance)) return -1;
        app->worker = std::make_unique<MonitorWorker>([app](bool reset) { return app->sampler.sample(reset); }, app->interval());
        if (!SetTimer(window, pollTimer, 50, nullptr)) return -1;
        return 0;
    case WM_TIMER:
        if (wParam == pollTimer) {
            if (auto snapshot = app->worker->take()) accept(*app, std::move(*snapshot));
            RECT client{}; GetClientRect(window, &client);
            RECT summary{0, 0, client.right, app->px(245)};
            RECT status{0, client.bottom - app->px(34), client.right, client.bottom};
            static unsigned ticks = 0;
            if (++ticks % 20 == 0) { InvalidateRect(window, &summary, FALSE); InvalidateRect(window, &status, FALSE); }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case searchId: if (HIWORD(wParam) == EN_CHANGE && app->table) rebuild(*app); break;
        case refreshId:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const auto index = SendMessageW(app->refresh, CB_GETCURSEL, 0, 0);
                if (index >= 0 && index < static_cast<LRESULT>(intervals.size())) {
                    app->settings.refreshMs = intervals[static_cast<size_t>(index)]; configure(*app);
                }
            }
            break;
        case pauseId: app->paused = !app->paused; configure(*app); break;
        case exportId: exportRows(*app); break;
        }
        return 0;
    case WM_NOTIFY: {
        auto* notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification->hwndFrom == app->table) {
            if (notification->code == LVN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 &&
                    info->item.iItem < static_cast<int>(app->textRows.size()) &&
                    info->item.iSubItem >= 0 && info->item.iSubItem < 5)
                    lstrcpynW(info->item.pszText, app->textRows[info->item.iItem][info->item.iSubItem].c_str(), info->item.cchTextMax);
            } else if (notification->code == LVN_COLUMNCLICK) {
                const int column = reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;
                if (column == app->settings.sortColumn) app->settings.ascending = !app->settings.ascending;
                else { app->settings.sortColumn = column; app->settings.ascending = column < 2; }
                rebuild(*app);
            }
        }
        return 0;
    }
    case WM_SIZE: if (app->table) layout(*app); return 0;
    case WM_GETMINMAXINFO: {
        auto* bounds = reinterpret_cast<MINMAXINFO*>(lParam);
        bounds->ptMinTrackSize = {app->px(900), app->px(600)};
        return 0;
    }
    case WM_DPICHANGED: {
        const UINT previous = app->dpi;
        app->dpi = HIWORD(wParam);
        for (int i = 0; i < 5; ++i)
            ListView_SetColumnWidth(app->table, i, MulDiv(ListView_GetColumnWidth(app->table, i), static_cast<int>(app->dpi), static_cast<int>(previous)));
        font(*app);
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(*app);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: {
        RECT client{}; GetClientRect(window, &client);
        render(*app, reinterpret_cast<HDC>(wParam), client);
        return 0;
    }
    case WM_PAINT: paint(*app); return 0;
    case WM_CLOSE: persist(*app); DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, pollTimer);
        app->worker.reset();
        if (app->font) { DeleteObject(app->font); app->font = nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
int runGUI(HINSTANCE instance, int show) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    App app;
    app.settingsFile = settingsPath();
    app.settings = loadSettings(app.settingsFile);
    app.dpi = GetDpiForSystem();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = L"RealTimeDataMonitorClass";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&windowClass)) return 1;
    RECT desired{app.settings.left, app.settings.top, app.settings.left + app.px(app.settings.width), app.settings.top + app.px(app.settings.height)};
    HMONITOR monitor = MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info), {}, {}, 0};
    GetMonitorInfoW(monitor, &info);
    const int width = std::min(desired.right - desired.left, info.rcWork.right - info.rcWork.left);
    const int height = std::min(desired.bottom - desired.top, info.rcWork.bottom - info.rcWork.top);
    const int left = std::clamp(desired.left, info.rcWork.left, info.rcWork.right - width);
    const int top = std::clamp(desired.top, info.rcWork.top, info.rcWork.bottom - height);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, windowClass.lpszClassName, L"Real-Time Data Monitor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, left, top, width, height, nullptr, nullptr, instance, &app);
    if (!window) return 1;
    const int scaledWidth = std::min(app.px(app.settings.width), static_cast<int>(info.rcWork.right - info.rcWork.left));
    const int scaledHeight = std::min(app.px(app.settings.height), static_cast<int>(info.rcWork.bottom - info.rcWork.top));
    SetWindowPos(window, nullptr,
        std::clamp(left, static_cast<int>(info.rcWork.left), static_cast<int>(info.rcWork.right) - scaledWidth),
        std::clamp(top, static_cast<int>(info.rcWork.top), static_cast<int>(info.rcWork.bottom) - scaledHeight),
        scaledWidth, scaledHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, app.settings.maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(window);
    MSG message{};
    int result;
    while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

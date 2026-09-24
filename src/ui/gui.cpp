#include "ui/gui.hpp"
#include "ui/options.hpp"
#include "ui/presentation.hpp"
#include "app/settings.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <algorithm>
#include <deque>
#include <memory>

namespace {
constexpr int searchId = 101, pauseId = 103, exportId = 104, tableId = 105, tabsId = 106, optionsId = 107, recordId = 108;
constexpr UINT_PTR pollTimer = 1;
struct Point { double time; std::array<Reading<double>, 3> values; };
struct App {
    HWND window{}, table{}, tabs{}, search{}, searchLabel{}, pause{}, exportButton{}, options{}, record{};
    HFONT font{};
    HIMAGELIST processIcons{};
    std::vector<int> rowIcons;
    UINT dpi = 96;
    Settings settings;
    std::filesystem::path settingsFile;
    std::unique_ptr<MonitorEngine> engine;
    std::unique_ptr<CsvWriter> exporter;
    DashboardSnapshot latest;
    std::vector<ProcessData> rows;
    std::vector<ProcessData> filteredRows;
    std::vector<ProcessTableRow> processRows;
    std::map<std::wstring, bool> expansion;
    std::vector<std::vector<std::wstring>> textRows;
    std::deque<Point> systemHistory, sensorHistory;
    double systemSeen{}, processSeen{}, sensorSeen{};
    bool paused{}, suspended{};
    std::wstring notice;
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};
std::wstring windowText(HWND window) {
    const int count = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    GetWindowTextW(window, text.data(), count + 1); text.resize(static_cast<std::size_t>(count));
    return text;
}
std::wstring number(Reading<double> value, int precision = 2) {
    return value.valid() ? decimal(value.value, precision) : stateText(value.state);
}
std::wstring age(double completed) {
    return completed > 0 ? decimal(std::max(0.0, monotonicSeconds() - completed), 1) + L" s old" : L"no sample";
}
bool stale(const App& app, const SampleTiming& timing) {
    return app.paused || app.suspended || timing.completed == 0 || timing.generation != app.latest.generation ||
        monotonicSeconds() - timing.completed > std::max(0.1, timing.deadlineSeconds * 3);
}
std::wstring state(const App& app, const SampleTiming& timing, bool failed = false) {
    if (app.suspended) return L"Suspended";
    if (app.paused) return L"Paused";
    if (failed) return timing.completed > 0 ? L"Unavailable / retained data stale" : L"Unavailable";
    if (timing.completed == 0) return L"Collecting...";
    if (stale(app, timing)) return L"Stale";
    return L"Live";
}
void font(App& app) {
    HFONT replacement = CreateFontW(-MulDiv(10, static_cast<int>(app.dpi), 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!replacement) return;
    for (HWND child : {app.table, app.tabs, app.search, app.searchLabel, app.pause, app.exportButton, app.options, app.record})
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (app.font) DeleteObject(app.font);
    app.font = replacement;
}
void layout(App& app) {
    RECT client{}; GetClientRect(app.window, &client);
    const int m = app.px(18), h = app.px(30), y = app.px(90);
    MoveWindow(app.tabs, m, app.px(48), client.right - m * 2, app.px(30), TRUE);
    MoveWindow(app.pause, m, y, app.px(90), h, TRUE);
    MoveWindow(app.record, m + app.px(100), y, app.px(130), h, TRUE);
    MoveWindow(app.exportButton, m + app.px(240), y, app.px(110), h, TRUE);
    MoveWindow(app.options, m + app.px(360), y, app.px(100), h, TRUE);
    MoveWindow(app.searchLabel, m + app.px(478), y + app.px(5), app.px(50), h, TRUE);
    MoveWindow(app.search, m + app.px(535), y, std::max(app.px(100), static_cast<int>(client.right) - m * 2 - app.px(535)), h, TRUE);
    const int tableTop = app.settings.view == 2 ? app.px(318) : app.px(166);
    MoveWindow(app.table, m, tableTop, client.right - m * 2, std::max<LONG>(1, client.bottom - tableTop - app.px(70)), TRUE);
    ShowWindow(app.table, app.settings.view == 0 ? SW_HIDE : SW_SHOW);
    ShowWindow(app.search, app.settings.view == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(app.searchLabel, app.settings.view == 1 ? SW_SHOW : SW_HIDE);
    InvalidateRect(app.window, nullptr, FALSE);
}
void rememberColumns(App& app) {
    if (app.settings.view == 1)
        for (int i = 0; i < 5; ++i)
            app.settings.columns[i] = MulDiv(ListView_GetColumnWidth(app.table, i), 96, static_cast<int>(app.dpi));
}
void updateIcons(App& app) {
    ListView_SetImageList(app.table, nullptr, LVSIL_SMALL);
    if (app.processIcons) ImageList_Destroy(app.processIcons);
    app.processIcons = nullptr;
    app.rowIcons.assign(app.rows.size(), 0);
    if (app.settings.view != 1) return;
    const int size = app.px(16);
    app.processIcons = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 16, 16);
    if (!app.processIcons) return;
    ImageList_AddIcon(app.processIcons, LoadIconW(nullptr, IDI_APPLICATION));
    std::map<const ProcessIcon*, int> indices;
    for (std::size_t i = 0; i < app.rows.size(); ++i) {
        const auto* icon = app.rows[i].icon.get();
        if (!icon) continue;
        auto found = indices.find(icon);
        if (found == indices.end()) {
            HICON handle = size <= 16 ? icon->smallIcon : icon->largeIcon;
            if (!handle) handle = icon->largeIcon ? icon->largeIcon : icon->smallIcon;
            const int index = handle ? ImageList_AddIcon(app.processIcons, handle) : 0;
            found = indices.emplace(icon, std::max(0, index)).first;
        }
        app.rowIcons[i] = found->second;
    }
    ListView_SetImageList(app.table, app.processIcons, LVSIL_SMALL);
}
void columns(App& app) {
    ListView_SetItemCount(app.table, 0);
    while (Header_GetItemCount(ListView_GetHeader(app.table)) > 0) ListView_DeleteColumn(app.table, 0);
    std::vector<std::pair<const wchar_t*, int>> fields;
    if (app.settings.view == 1) fields = {{L"Process",app.settings.columns[0]}, {L"PID",app.settings.columns[1]}, {L"CPU (%)",app.settings.columns[2]},
        {L"Working set (MiB)",app.settings.columns[3]}, {L"Uptime",app.settings.columns[4]}};
    if (app.settings.view == 2) fields = {{L"Simulated sensor",160}, {L"Units",65}, {L"Current",130}, {L"Average",100},
        {L"Minimum",100}, {L"Maximum",100}, {L"Valid/window",110}, {L"Threshold state",145}, {L"Transitions",95}};
    if (app.settings.view == 3) fields = {{L"Source",155}, {L"Stage",120}, {L"Median ms",100}, {L"p95 ms",95},
        {L"p99 ms",95}, {L"Max observed ms",135}, {L"Deadline misses",120}, {L"Skipped slots",110}, {L"Errors",80}};
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.pszText = const_cast<wchar_t*>(fields[i].first);
        column.cx = app.px(fields[i].second);
        column.fmt = i == 0 ? LVCFMT_LEFT : LVCFMT_RIGHT;
        ListView_InsertColumn(app.table, i, &column);
    }
    app.rows.clear(); app.processRows.clear(); app.textRows.clear(); app.processSeen = 0;
}
void rebuild(App& app) {
    const int selectedIndex = ListView_GetNextItem(app.table, -1, LVNI_SELECTED);
    const int topIndex = ListView_GetTopIndex(app.table);
    std::optional<ProcessTableRow> selected, top;
    if (app.settings.view == 1) {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(app.processRows.size())) selected = app.processRows[selectedIndex];
        if (topIndex >= 0 && topIndex < static_cast<int>(app.processRows.size())) top = app.processRows[topIndex];
    }
    SendMessageW(app.table, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemState(app.table, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    app.textRows.clear();
    if (app.settings.view == 1) {
        const auto& processes = app.latest.processes;
        if (processes.data)
            app.filteredRows = makeRows(processes.data->rows, windowText(app.search), static_cast<SortColumn>(app.settings.sortColumn), app.settings.ascending);
        else app.filteredRows.clear();
        app.processRows = groupedRows(app.filteredRows, static_cast<SortColumn>(app.settings.sortColumn),
            app.settings.ascending, app.expansion, !windowText(app.search).empty());
        app.rows.clear();
        for (const auto& row : app.processRows) {
            app.rows.push_back(row.process);
            auto rowCells = cells(row.process);
            if (row.header) {
                rowCells[0] = (row.expanded ? L"\u25bc " : L"\u25b6 ") + row.process.name + L" (" + std::to_wstring(row.count) + L")";
                rowCells[1] = L"";
            }
            if (row.child) rowCells[0] = L"      " + rowCells[0];
            app.textRows.emplace_back(rowCells.begin(), rowCells.end());
        }
        HWND header = ListView_GetHeader(app.table);
        for (int column = 0; column < 5; ++column) {
            HDITEMW item{}; item.mask = HDI_FORMAT; Header_GetItem(header, column, &item);
            item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (column == app.settings.sortColumn) item.fmt |= app.settings.ascending ? HDF_SORTUP : HDF_SORTDOWN;
            Header_SetItem(header, column, &item);
        }
    } else if (app.settings.view == 2) {
        const std::array<const wchar_t*, 3> names{L"Temperature",L"Pressure",L"Vibration"}, units{L"degC",L"kPa",L"g"};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& value = app.latest.sensors.values[i];
            app.textRows.push_back({names[i], units[i], number(value.raw), number(value.average), number(value.minimum), number(value.maximum),
                std::to_wstring(value.validCount) + L"/" + std::to_wstring(app.settings.pipeline.sensors.window),
                value.raw.valid() ? (value.alert ? L"Above threshold" : L"Normal") : L"Unknown (missing)",
                std::to_wstring(value.transitions)});
        }
    } else if (app.settings.view == 3) {
        const std::array<const wchar_t*, 3> names{L"Windows system",L"Windows processes",L"Simulated sensors"};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& timing = app.latest.timings[i];
            const std::array<Distribution, 3> distributions{timing.scheduling,timing.processing,timing.latency};
            const std::array<const wchar_t*, 3> stages{L"Scheduling",L"Processing",L"Total latency"};
            for (std::size_t j = 0; j < 3; ++j) {
                const auto& d = distributions[j];
                app.textRows.push_back({names[i],stages[j],timing.samples?decimal(d.median,3):L"Unavailable",timing.samples?decimal(d.p95,3):L"Unavailable",timing.samples?decimal(d.p99,3):L"Unavailable",timing.samples?decimal(d.maximum,3):L"Unavailable",
                    j == 2 ? std::to_wstring(timing.deadlineMisses) : L"", j == 2 ? std::to_wstring(timing.skipped) : L"",
                    j == 2 ? std::to_wstring(timing.failures) : L""});
            }
        }
    }
    updateIcons(app);
    ListView_SetItemCountEx(app.table, static_cast<int>(app.textRows.size()), LVSICF_NOSCROLL);
    int restoredTop = -1;
    if (app.settings.view == 1) for (int i = 0; i < static_cast<int>(app.rows.size()); ++i) {
        if (selected && app.processRows[i].key == selected->key)
            ListView_SetItemState(app.table, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (top && app.processRows[i].key == top->key) restoredTop = i;
    }
    if (app.settings.view == 1) for (int i = 0; i < static_cast<int>(app.processRows.size()); ++i) {
        const auto& row = app.processRows[i];
        if (!row.child && selected && row.family == selected->family && ListView_GetNextItem(app.table, -1, LVNI_SELECTED) < 0)
            ListView_SetItemState(app.table, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        if (!row.child && top && row.family == top->family && restoredTop < 0) restoredTop = i;
    }
    if (restoredTop >= 0) {
        RECT rect{};
        if (ListView_GetItemRect(app.table, 0, &rect, LVIR_BOUNDS))
            ListView_Scroll(app.table, 0, (restoredTop - ListView_GetTopIndex(app.table)) * (rect.bottom - rect.top));
    }
    SendMessageW(app.table, WM_SETREDRAW, TRUE, 0); InvalidateRect(app.table, nullptr, FALSE);
}
void expandProcess(App& app, int index, std::optional<bool> expanded = {}) {
    if (app.settings.view != 1 || index < 0 || index >= static_cast<int>(app.processRows.size())) return;
    const auto row = app.processRows[index];
    if (row.header) {
        app.expansion[row.family] = expanded.value_or(!row.expanded);
        rebuild(app);
    } else if (row.child && expanded == false) {
        app.expansion[row.family] = false;
        rebuild(app);
    }
}
LRESULT CALLBACK processKeys(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
    auto& app = *reinterpret_cast<App*>(reference);
    if (message == WM_KEYDOWN && app.settings.view == 1 &&
        (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_SPACE || wParam == VK_RETURN)) {
        const int index = ListView_GetNextItem(window, -1, LVNI_SELECTED);
        if (wParam == VK_LEFT || wParam == VK_RIGHT) expandProcess(app, index, wParam == VK_RIGHT);
        else expandProcess(app, index);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
void refresh(App& app) {
    app.latest = app.engine->snapshot();
    if (app.latest.system.timing.completed != app.systemSeen) {
        const auto& frame = app.latest.system;
        Reading<double> memory;
        if (frame.memory.valid()) memory = Reading<double>::ready(100.0 * static_cast<double>(frame.memory.value.totalBytes - frame.memory.value.availableBytes) /
            static_cast<double>(frame.memory.value.totalBytes));
        app.systemHistory.push_back({frame.timing.completed,{frame.cpu,memory,{}}});
        app.systemSeen = frame.timing.completed;
    }
    if (app.latest.sensors.timing.completed != app.sensorSeen) {
        std::array<Reading<double>, 3> values{};
        for (std::size_t i = 0; i < 3; ++i) values[i] = app.latest.sensors.values[i].raw;
        app.sensorHistory.push_back({app.latest.sensors.timing.completed,values});
        app.sensorSeen = app.latest.sensors.timing.completed;
    }
    const double cutoff = monotonicSeconds() - 120;
    for (auto* history : {&app.systemHistory,&app.sensorHistory})
        while (!history->empty() && (history->front().time < cutoff || history->size() > 2400)) history->pop_front();
    if (app.settings.view != 1 || app.processSeen != app.latest.processes.dataTiming.completed) {
        rebuild(app); app.processSeen = app.latest.processes.dataTiming.completed;
    }
    const auto recording = app.engine->recorder().status();
    SetWindowTextW(app.record, recording.running ? (recording.stopping ? L"Finishing..." : L"Stop recording") : L"Record CSV");
    EnableWindow(app.record, !recording.running || !recording.stopping);
    InvalidateRect(app.window, nullptr, FALSE);
}
void configure(App& app) {
    app.engine->configure(app.settings.pipeline, app.paused || app.suspended);
    KillTimer(app.window, pollTimer);
    SetTimer(app.window, pollTimer, static_cast<UINT>(app.settings.pipeline.displayMs), nullptr);
    const double now = monotonicSeconds();
    app.systemHistory.push_back({now,{}}); app.sensorHistory.push_back({now,{}});
    SetWindowTextW(app.pause, app.paused ? L"Resume" : L"Pause");
    refresh(app);
}
void text(HDC dc, RECT rect, const std::wstring& value, COLORREF color = RGB(30,41,59)) {
    SetTextColor(dc,color);
    DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}
void graph(App& app, HDC dc, RECT rect, const std::deque<Point>& history, std::size_t channel, const std::wstring& title,
           double minimum, double maximum, COLORREF color, double maxGap) {
    HBRUSH background = CreateSolidBrush(RGB(255,255,255)); FillRect(dc,&rect,background); DeleteObject(background);
    text(dc,{rect.left+app.px(12),rect.top+app.px(4),rect.right-app.px(12),rect.top+app.px(30)},title,color);
    text(dc,{rect.left+app.px(12),rect.top+app.px(30),rect.right-app.px(12),rect.top+app.px(52)},
        decimal(minimum,1)+L" to "+decimal(maximum,1)+L" | 120 s",RGB(100,116,139));
    RECT plot{rect.left+app.px(12),rect.top+app.px(60),rect.right-app.px(12),rect.bottom-app.px(12)};
    HPEN grid = CreatePen(PS_SOLID,1,RGB(225,231,239)); auto old = SelectObject(dc,grid);
    for(int i=0;i<=2;++i) { int y=plot.top+(plot.bottom-plot.top)*i/2; MoveToEx(dc,plot.left,y,nullptr); LineTo(dc,plot.right,y); }
    HPEN line=CreatePen(PS_SOLID,std::max(1,app.px(2)),color); SelectObject(dc,line);
    const double now=monotonicSeconds(); bool connected=false; double previous=0;
    for(const auto& point:history) {
        const double sampleAge=now-point.time; const auto value=point.values[channel];
        if(sampleAge>120 || !value.valid()) { connected=false; continue; }
        const int x=plot.right-static_cast<int>(sampleAge/120*(plot.right-plot.left));
        const double fraction=std::clamp((value.value-minimum)/(maximum-minimum),0.0,1.0);
        const int y=plot.bottom-static_cast<int>(fraction*(plot.bottom-plot.top));
        if(connected && point.time-previous<=maxGap) LineTo(dc,x,y); else MoveToEx(dc,x,y,nullptr);
        previous=point.time; connected=true;
    }
    SelectObject(dc,old); DeleteObject(line); DeleteObject(grid);
}
void render(App& app, HDC dc, RECT client) {
    HBRUSH background=CreateSolidBrush(RGB(243,246,250)); FillRect(dc,&client,background); DeleteObject(background);
    auto oldFont=SelectObject(dc,app.font); SetBkMode(dc,TRANSPARENT);
    const int m=app.px(18),right=client.right-m;
    text(dc,{m,app.px(10),right,app.px(40)},L"Real-Time Data Monitor  |  Windows soft real-time  |  Measured timing, no deadline guarantees");
    const auto& system=app.latest.system; const auto& processes=app.latest.processes; const auto& sensors=app.latest.sensors;
    if(app.settings.view==0) {
        text(dc,{m,app.px(130),right,app.px(158)},L"Windows system: "+state(app,system.timing,system.failed)+L" | "+age(system.timing.completed)+L" | CPU "+number(system.cpu,1)+(system.cpu.valid()?L"%":L""));
        std::wstring memory=L"Memory: "+stateText(system.memory.state);
        if(system.memory.valid()) {
            const auto& value=system.memory.value;
            memory=L"Memory: "+decimal(static_cast<double>(value.totalBytes-value.availableBytes)/1073741824.0,2)+L" GiB used / "+
                decimal(static_cast<double>(value.totalBytes)/1073741824.0,2)+L" GiB total | "+
                decimal(static_cast<double>(value.availableBytes)/1073741824.0,2)+L" GiB available";
        }
        text(dc,{m,app.px(160),right,app.px(188)},memory);
        text(dc,{m,app.px(190),right,app.px(218)},L"Uptime: "+(system.timing.completed>0&&!system.failed?durationText(system.uptime):L"Unavailable"));
        const int middle=client.right/2;
        graph(app,dc,{m,app.px(232),middle-app.px(6),app.px(420)},app.systemHistory,0,L"CPU (%)",0,100,RGB(37,99,235),app.settings.pipeline.systemMs/1000.0*3);
        graph(app,dc,{middle+app.px(6),app.px(232),right,app.px(420)},app.systemHistory,1,L"Memory used (%)",0,100,RGB(5,130,100),app.settings.pipeline.systemMs/1000.0*3);
        text(dc,{m,app.px(434),right,app.px(462)},L"System sampling "+std::to_wstring(app.settings.pipeline.systemMs)+L" ms | process sampling "+
            std::to_wstring(app.settings.pipeline.processMs)+L" ms | display "+std::to_wstring(app.settings.pipeline.displayMs)+L" ms");
        text(dc,{m,app.px(466),right,app.px(494)},L"Process values use their own sample timestamp. Simulated sensors are shown separately.");
    } else if(app.settings.view==1) {
        text(dc,{m,app.px(130),right,app.px(158)},L"Windows processes: "+state(app,processes.dataTiming,processes.failed)+L" | "+age(processes.dataTiming.completed)+
            L" | "+std::to_wstring(app.filteredRows.size())+L" processes | group CPU/RAM totals; uptime = oldest member");
    } else if(app.settings.view==2) {
        text(dc,{m,app.px(130),right,app.px(158)},L"SIMULATED DATA: "+state(app,sensors.timing)+L" | "+age(sensors.timing.completed)+
            L" | "+std::to_wstring(app.settings.pipeline.sensors.intervalMs)+L" ms | seed "+std::to_wstring(app.settings.pipeline.sensors.seed));
        const int width=(client.right-m*2-app.px(16))/3;
        const std::array<const wchar_t*,3> labels{L"Temperature (degC)",L"Pressure (kPa)",L"Vibration (g)"};
        const std::array<double,3> low{10,90,0},high{45,120,1.5};
        for(std::size_t i=0;i<3;++i) {
            const int left=m+static_cast<int>(i)*(width+app.px(8));
            graph(app,dc,{left,app.px(170),left+width,app.px(304)},app.sensorHistory,i,labels[i],low[i],high[i],
                i==0?RGB(220,80,45):i==1?RGB(37,99,235):RGB(5,130,100),std::max(0.25,app.settings.pipeline.displayMs/1000.0*3));
        }
    } else {
        text(dc,{m,app.px(130),right,app.px(158)},L"Percentiles: latest 1024 completed samples | maximum and counters: this session | deadline = source interval");
    }
    const auto recording=app.engine->recorder().status(), exporting=app.exporter->status();
    const bool overloaded=app.latest.lastOverflow>0 && monotonicSeconds()-app.latest.lastOverflow<2;
    std::wstring status=app.paused?L"Paused":app.suspended?L"Suspended":app.latest.timerFailed?L"Sampling error":overloaded?L"Overloaded":L"Running";
    status+=L" | sensor queue "+std::to_wstring(app.latest.queuedSamples)+L"/256 | dropped "+std::to_wstring(app.latest.droppedSamples)+
        L" | reset-discarded "+std::to_wstring(app.latest.abandonedSamples)+L" | "+(app.latest.highResolution?L"high-resolution timers":L"standard timer fallback");
    text(dc,{m,client.bottom-app.px(62),right,client.bottom-app.px(36)},status,overloaded?RGB(180,65,20):RGB(51,65,85));
    std::wstring bottom;
    if(recording.failed) bottom=L"Recording failed (Windows error "+std::to_wstring(recording.error)+L")";
    else if(recording.running) bottom=recording.stopping?L"Finishing recording":L"Recording";
    else bottom=L"Recording stopped";
    bottom+=L" | written "+std::to_wstring(recording.written)+L" | dropped "+std::to_wstring(recording.dropped);
    if(exporting.failed) bottom+=L" | Export failed ("+std::to_wstring(exporting.error)+L")";
    else if(exporting.running) bottom+=L" | Exporting...";
    else if(exporting.written) bottom+=L" | Exported "+std::to_wstring(exporting.written)+L" rows";
    if(!app.notice.empty()) bottom+=L" | "+app.notice;
    text(dc,{m,client.bottom-app.px(32),right,client.bottom-app.px(6)},bottom,recording.failed||exporting.failed?RGB(180,65,20):RGB(51,65,85));
    SelectObject(dc,oldFont);
}
void paintRegion(App& app,HDC target,RECT update) {
    RECT client{}; GetClientRect(app.window,&client);
    if(client.right<=0||client.bottom<=0) return;
    HDC buffer=CreateCompatibleDC(target); HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);
    if(buffer&&bitmap) {
        auto old=SelectObject(buffer,bitmap); render(app,buffer,client);
        BitBlt(target,update.left,update.top,update.right-update.left,update.bottom-update.top,buffer,update.left,update.top,SRCCOPY);
        SelectObject(buffer,old);
    } else render(app,target,client);
    if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
}
std::filesystem::path chooseCsv(App& app,bool recording) {
    wchar_t path[32768]{};
    lstrcpyW(path,recording?L"recording.csv":L"snapshot.csv");
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=app.window;
    dialog.lpstrFilter=L"CSV files (*.csv)\0*.csv\0\0"; dialog.lpstrFile=path; dialog.nMaxFile=static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt=L"csv"; dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&dialog)?std::filesystem::path(path):std::filesystem::path{};
}
void exportRows(App& app) {
    if(app.exporter->status().running) { app.notice=L"An export is already running"; return; }
    const auto path=chooseCsv(app,false); if(path.empty()) return;
    auto snapshot=app.latest;
    if(app.settings.view==1 && snapshot.processes.data) {
        auto filtered=std::make_shared<ProcessList>(); filtered->available=true; filtered->rows=app.filteredRows; snapshot.processes.data=std::move(filtered);
    }
    auto rows=MonitorEngine::records(snapshot,monotonicSeconds());
    std::erase_if(rows,[&](const auto& row) {
        return app.settings.view==0?row.source!=DataSource::system:app.settings.view==1?row.source!=DataSource::process:
            app.settings.view==2?row.source!=DataSource::simulated:false;
    });
    if(!app.exporter->start(path,std::move(rows),true)) app.notice=L"Export could not start";
    else app.notice.clear();
}
void persist(App& app) {
    rememberColumns(app);
    WINDOWPLACEMENT placement{}; placement.length=sizeof(placement);
    if(GetWindowPlacement(app.window,&placement)) {
        app.settings.left=placement.rcNormalPosition.left; app.settings.top=placement.rcNormalPosition.top;
        MONITORINFO info{sizeof(info),{},{},0};
        if(GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&info)) {
            app.settings.left+=info.rcWork.left-info.rcMonitor.left; app.settings.top+=info.rcWork.top-info.rcMonitor.top;
        }
        app.settings.width=MulDiv(placement.rcNormalPosition.right-placement.rcNormalPosition.left,96,static_cast<int>(app.dpi));
        app.settings.height=MulDiv(placement.rcNormalPosition.bottom-placement.rcNormalPosition.top,96,static_cast<int>(app.dpi));
        app.settings.maximized=placement.showCmd==SW_SHOWMAXIMIZED;
    }
    saveSettings(app.settingsFile,app.settings);
}
bool createControls(App& app,HINSTANCE instance) {
    const auto child=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id,DWORD extended=0) {
        return CreateWindowExW(extended,type,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,app.window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    };
    app.tabs=child(WC_TABCONTROLW,L"",WS_TABSTOP,tabsId);
    int index=0;
    for(const auto* label:{L"System",L"Processes",L"Simulated sensors",L"Timing"}) {
        TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(label); TabCtrl_InsertItem(app.tabs,index++,&item);
    }
    TabCtrl_SetCurSel(app.tabs,app.settings.view);
    app.pause=child(L"BUTTON",L"Pause",WS_TABSTOP|BS_PUSHBUTTON,pauseId);
    app.record=child(L"BUTTON",L"Record CSV",WS_TABSTOP|BS_PUSHBUTTON,recordId);
    app.exportButton=child(L"BUTTON",L"Export CSV",WS_TABSTOP|BS_PUSHBUTTON,exportId);
    app.options=child(L"BUTTON",L"Settings",WS_TABSTOP|BS_PUSHBUTTON,optionsId);
    app.searchLabel=child(L"STATIC",L"&Search",0,0);
    app.search=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,searchId,WS_EX_CLIENTEDGE);
    SendMessageW(app.search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Process name or PID"));
    app.table=child(WC_LISTVIEWW,L"Data",WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,tableId,WS_EX_CLIENTEDGE);
    if(!app.tabs||!app.table||!app.search||!app.record||!app.options||!app.pause||!app.exportButton) return false;
    ListView_SetExtendedListViewStyle(app.table,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    SetWindowSubclass(app.table, processKeys, 1, reinterpret_cast<DWORD_PTR>(&app));
    SetWindowTheme(app.table,L"Explorer",nullptr);
    font(app); columns(app); layout(app); return true;
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if(!app) return DefWindowProcW(window,message,wParam,lParam);
    switch(message) {
    case WM_CREATE:
        app->dpi=GetDpiForWindow(window);
        if(!createControls(*app,reinterpret_cast<CREATESTRUCTW*>(lParam)->hInstance)) return -1;
        try {
            app->engine=std::make_unique<MonitorEngine>(app->settings.pipeline);
            app->exporter=std::make_unique<CsvWriter>();
        } catch(...) { MessageBoxW(window,L"The data pipeline could not start.",L"Startup error",MB_ICONERROR); return -1; }
        if(!SetTimer(window,pollTimer,static_cast<UINT>(app->settings.pipeline.displayMs),nullptr)) return -1;
        return 0;
    case WM_TIMER: if(wParam==pollTimer) refresh(*app); return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case searchId: if(HIWORD(wParam)==EN_CHANGE&&app->table) rebuild(*app); break;
        case pauseId: app->paused=!app->paused; configure(*app); break;
        case exportId: exportRows(*app); break;
        case optionsId:
            if(showOptions(window,app->settings.pipeline)) { app->sensorHistory.clear(); configure(*app); }
            break;
        case recordId:
            if(app->engine->recorder().status().running) app->engine->recorder().finish();
            else {
                const auto path=chooseCsv(*app,true);
                if(!path.empty()&&!app->engine->recorder().start(path)) app->notice=L"Recording could not start";
            }
            break;
        }
        return 0;
    case WM_NOTIFY: {
        auto* notification=reinterpret_cast<NMHDR*>(lParam);
        if(notification->hwndFrom==app->tabs&&notification->code==TCN_SELCHANGE) {
            rememberColumns(*app); app->settings.view=TabCtrl_GetCurSel(app->tabs); columns(*app); rebuild(*app); layout(*app);
        } else if(notification->hwndFrom==app->table) {
            if (notification->code == NM_CLICK && app->settings.view == 1) {
                const auto* click = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (click->iSubItem == 0) expandProcess(*app, click->iItem);
            } else if(notification->code==LVN_GETDISPINFOW) {
                auto* info=reinterpret_cast<NMLVDISPINFOW*>(lParam);
                if (info->item.mask & LVIF_IMAGE)
                    info->item.iImage = app->settings.view == 1 && info->item.iItem >= 0 &&
                        info->item.iItem < static_cast<int>(app->rowIcons.size()) ? app->rowIcons[info->item.iItem] : I_IMAGENONE;
                if((info->item.mask&LVIF_TEXT)&&info->item.iItem>=0&&info->item.iItem<static_cast<int>(app->textRows.size())&&
                    info->item.iSubItem>=0&&info->item.iSubItem<static_cast<int>(app->textRows[info->item.iItem].size()))
                    lstrcpynW(info->item.pszText,app->textRows[info->item.iItem][info->item.iSubItem].c_str(),info->item.cchTextMax);
            } else if(notification->code==LVN_COLUMNCLICK&&app->settings.view==1) {
                const int column=reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;
                if(column==app->settings.sortColumn) app->settings.ascending=!app->settings.ascending;
                else { app->settings.sortColumn=column; app->settings.ascending=column<2; }
                rebuild(*app);
            }
        }
        return 0;
    }
    case WM_POWERBROADCAST:
        if(wParam==PBT_APMSUSPEND) { app->suspended=true; configure(*app); }
        else if(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND) { app->suspended=false; configure(*app); }
        return TRUE;
    case WM_SIZE: if(app->table) layout(*app); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize={app->px(1000),app->px(640)}; return 0;
    case WM_DPICHANGED: {
        const UINT previous=app->dpi; app->dpi=HIWORD(wParam);
        const int count=Header_GetItemCount(ListView_GetHeader(app->table));
        for(int i=0;i<count;++i) ListView_SetColumnWidth(app->table,i,MulDiv(ListView_GetColumnWidth(app->table,i),static_cast<int>(app->dpi),static_cast<int>(previous)));
        font(*app); updateIcons(*app); const auto* rect=reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE); layout(*app); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: { RECT client{}; GetClientRect(window,&client); if(app->engine) render(*app,reinterpret_cast<HDC>(wParam),client); return 0; }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint);
        if(app->engine&&app->exporter) paintRegion(*app,dc,paint.rcPaint);
        EndPaint(window,&paint); return 0;
    }
    case WM_CLOSE: persist(*app); DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window,pollTimer); app->engine.reset(); app->exporter.reset();
        if (app->processIcons) { ImageList_Destroy(app->processIcons); app->processIcons = nullptr; }
        if(app->font) { DeleteObject(app->font); app->font=nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int runGUI(HINSTANCE instance,int show) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_STANDARD_CLASSES};
    if(!InitCommonControlsEx(&controls)) return 1;
    App app; app.settingsFile=settingsPath(); app.settings=loadSettings(app.settingsFile); app.dpi=GetDpiForSystem();
    WNDCLASSEXW type{}; type.cbSize=sizeof(type); type.hInstance=instance; type.lpfnWndProc=windowProc;
    type.lpszClassName=L"RealTimeDataMonitorClass"; type.hCursor=LoadCursorW(nullptr,IDC_ARROW); type.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    if(!RegisterClassExW(&type)) return 1;
    RECT desired{app.settings.left,app.settings.top,app.settings.left+app.px(app.settings.width),app.settings.top+app.px(app.settings.height)};
    MONITORINFO info{sizeof(info),{},{},0}; GetMonitorInfoW(MonitorFromRect(&desired,MONITOR_DEFAULTTONEAREST),&info);
    const int width=std::min(desired.right-desired.left,info.rcWork.right-info.rcWork.left);
    const int height=std::min(desired.bottom-desired.top,info.rcWork.bottom-info.rcWork.top);
    const int left=std::clamp(desired.left,info.rcWork.left,info.rcWork.right-width),top=std::clamp(desired.top,info.rcWork.top,info.rcWork.bottom-height);
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"Real-Time Data Monitor",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        left,top,width,height,nullptr,nullptr,instance,&app);
    if(!window) return 1;
    const int scaledWidth=std::min(app.px(app.settings.width),static_cast<int>(info.rcWork.right-info.rcWork.left));
    const int scaledHeight=std::min(app.px(app.settings.height),static_cast<int>(info.rcWork.bottom-info.rcWork.top));
    SetWindowPos(window,nullptr,std::clamp(left,static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-scaledWidth),
        std::clamp(top,static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-scaledHeight),scaledWidth,scaledHeight,SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window,app.settings.maximized?SW_SHOWMAXIMIZED:show); UpdateWindow(window);
    MSG message{}; int result;
    while((result=static_cast<int>(GetMessageW(&message,nullptr,0,0)))>0)
        if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    return result==-1?1:static_cast<int>(message.wParam);
}

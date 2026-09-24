# Real-Time Data Monitor

A Windows desktop system monitor written in C++20 and Win32. It displays CPU,
physical memory, system uptime and a live process table without administrator access.

## Build and run

Requirements: Windows 10 (1703+) or Windows 11, CMake 3.31+, a Windows SDK, and an
MSVC C++ toolchain (Visual Studio Build Tools works). This project was verified
with the MSVC toolchain configured in CLion.

In CLion, open this directory, reload CMake, select the **untitled7** application
target, and click **Run** (Shift+F10). The target name is retained for existing CLion
run configurations.

From a Visual Studio developer terminal with CMake and Ninja on PATH:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\untitled7.exe
```

For the existing CLion Debug build, the executable is
`cmake-build-debug\untitled7.exe`.

## Using the monitor

- **Search** filters by process name (case insensitive) or PID. Clear it to show all processes.
- Click a **column header** to sort; click again to reverse direction. Unavailable
  values stay at the bottom. Drag header dividers to resize columns.
- The scrollable table shows full process names, PID, CPU, working-set memory and
  uptime. Selection follows process identity across refreshes and sorting; it
  does not transfer to a different process that reuses the PID.
- **Refresh** offers 0.5, 1, 2 or 5 seconds. Collection runs on one background
  worker, never concurrently, and does not block the UI.
- **Pause / Resume** freezes collection. Retained readings are labelled stale;
  CPU baselines restart on resume so a reading never averages across the pause.
- The two history graphs show the last **120 seconds** on a shared 0–100% scale.
  Missing readings and pauses create gaps rather than false zero readings.
- **Export CSV** exports the currently filtered and sorted process rows. The
  UTF-8 file includes the process snapshot timestamp (local time), stale flag,
  raw uptime seconds and separate measurement status fields. Unavailable numeric
  values are blank. Names are escaped and spreadsheet formula prefixes neutralized.
- Window position, size, maximized state, refresh interval, sort order and column
  widths are saved under `%LOCALAPPDATA%\RealTimeDataMonitor\settings.ini`.
  Off-screen saved positions are brought back onto an available display.
- Layout and fonts scale with monitor DPI. Standard controls support keyboard
  navigation; Tab moves between controls and Alt+S focuses search.

## Reading the numbers

System CPU needs two successful samples and initially displays **Collecting...**.
Process CPU is normalized to total logical processor capacity: one fully occupied
core on a four-core machine is approximately 25%. Newly seen processes also need
two samples. The system CPU query is explicitly unavailable on machines with
multiple processor groups (>64 logical processors), where GetSystemTimes would
otherwise report only one group's CPU time.

System memory is physical memory, shown in **GiB** (2^30 bytes). "Available" comes
from Windows' available-physical-memory value. Process memory is **working set**
in **MiB** (2^20 bytes), not private/committed memory; shared pages mean process
working sets should not be summed to reproduce total system usage.

Protected or exiting processes can show **Access denied** or **Unavailable** for
individual measurements. These are never represented as valid zero measurements.
If enumeration fails, the last complete process list is retained and marked stale
with its original timestamp. Delayed collection and paused snapshots are also
labelled stale. Uptime comes from system tick time or process creation time; a
system clock adjustment can affect process uptime, but CPU intervals use a
monotonic clock.

## Source layout

- `measurement.*`: CPU counter/baseline calculations and typed reading states.
- `stat.*`, `meminfo.*`, `processinfo.*`, `uptime.*`: Windows data collection.
- `monitor.*`: complete snapshots, worker scheduling and pause/resume lifecycle.
- `presentation.*`: numeric sorting, filtering, identity, formatting and CSV export.
- `settings.*`: bounded preference loading and saving.
- `gui.cpp`: native virtual list-view, controls, DPI layout and buffered painting.

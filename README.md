# Real-Time Data Monitor

A Windows desktop app for monitoring CPU, memory, uptime and running processes.
Includes search, sorting, history graphs, pause/resume and CSV export.

## Run

Open this folder in CLion with an MSVC toolchain and Windows SDK installed.
Reload CMake, select **untitled7**, then press **Shift+F10**.

Requires Windows 10 or later, C++20 and CMake 3.31+.

## Folders

- `src/app/`: startup and saved settings
- `src/core/`: measurements and background sampling
- `src/system/`: Windows system data collection
- `src/ui/`: interface and CSV export
- `resources/`: Windows application resources

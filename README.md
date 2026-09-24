# Real-Time Data Monitor

A Windows C++ desktop app for monitoring CPU, memory, uptime and running processes,
with simulated temperature, pressure and vibration streams.

Includes rolling statistics, threshold alerts, history graphs, timing measurements,
adjustable sampling, pause/resume and background CSV recording.
Uses soft real-time processing; measured timings are not guaranteed deadlines.

## Folders

- `src/app/`: startup and saved settings
- `src/core/`: sampling, processing, timing and recording
- `src/system/`: Windows system data collection
- `src/ui/`: dashboard and controls
- `resources/`: Windows application resources

# game-fps

[![CI](https://github.com/barnibeats/game-FPS/actions/workflows/ci.yml/badge.svg)](https://github.com/barnibeats/game-FPS/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/barnibeats/game-FPS)](https://github.com/barnibeats/game-FPS/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Tiny always-on-top FPS HUD for Windows 10 / 11 with a built-in standby list cleaner.
Real frame rate of any game (D3D9/10/11/12, OpenGL, Vulkan) and of the desktop, in the screen corner you choose.

[Русская версия](README.ru.md)

## Why it is light

- Single native C++ executable (~300 KB), no runtime, no installer, no GUI framework.
- FPS comes from a real-time ETW session on `Microsoft-Windows-DxgKrnl` limited to the *Present* keyword — the same source PresentMon uses. No hooks, no injection, so it is safe with anti-cheat.
- If the active window is not presenting frames (idle desktop), the HUD shows the compositor refresh rate in Hz (`DwmGetCompositionTimingInfo`); any app that draws (browser, video, game) shows its real FPS.
- HUD is repainted only when the text changes; runs at below-normal priority with EcoQoS; trims its working set. About 5 MB RAM, ~0% CPU.
- Hide the HUD and the ETW session is stopped completely.

## Features

- HUD in any corner, on the primary monitor or the monitor of the active window, or **free placement**: drag it with the mouse (`Ctrl+Alt+G` to start/finish moving).
- Layout: stacked in a column or **in a row**.
- Optional FPS colour by level (green / yellow / red) and *show only in games* (hidden on the idle desktop).
- Optional lines: frame time, 1% low FPS, RAM, **CPU load, CPU temperature, GPU load, GPU temperature, VRAM usage**.
- Size, colour, opacity, update interval (1 s / 2 s).
- Lives in the system tray, left click toggles the HUD, right click opens the menu.
- Autostart with Windows (Task Scheduler logon task, no UAC prompt at boot).
- **Memory cleaner** (Intelligent Standby List Cleaner style): purge the standby list manually or automatically when `standby >= N MB` and `free < M MB`.
- Hotkeys: `Ctrl+Alt+F` toggle HUD, `Ctrl+Alt+G` move HUD, `Ctrl+Alt+M` purge standby list.

### Sensors notes

- Sensors are opened only while their line is enabled and the HUD is visible; otherwise nothing is polled.
- CPU load: `GetSystemTimes`. GPU on NVIDIA: NVML (temperature, load, VRAM). Other GPUs: VRAM via DXGI and load via Windows `GPU Engine` counters; GPU temperature is not available there yet.
- **CPU temperature** is read from ACPI thermal zones, which many PCs (including most Ryzen boards) do not expose; the HUD then shows `--`. Reading the real CPU die temperature needs a kernel driver, which this project deliberately does not ship (anti-cheat friendliness).

## Install and update

Everything is distributed through [GitHub Releases](https://github.com/barnibeats/game-FPS/releases).
Download `game-fps-vX.Y.Z-win-x64.zip`, unpack anywhere, run `game-fps.exe`. To update, download the new release (tray menu → *Check for updates*).
Each release includes a SHA-256 checksum.

The app requests administrator rights: ETW sessions and memory-list control require them.

## Settings

Everything is available from the tray menu. The file `%APPDATA%\game-fps\settings.ini` is also editable (tray → *Open settings file*).

## Build from source

Requirements: CMake ≥ 3.16 and either MSVC (Visual Studio 2019+) or MinGW-w64.

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

## Project layout

| Path | Purpose |
| --- | --- |
| `src/etw.cpp` | ETW session, per-process FPS, 1% low, desktop FPS |
| `src/hud.cpp` | Layered click-through overlay window |
| `src/memory.cpp` | Standby list / working set cleaning |
| `src/autostart.cpp` | Task Scheduler autostart |
| `src/settings.cpp` | INI settings |
| `src/main.cpp` | Tray, menu, hotkeys, timers |

## Contributing and versioning

See [CONTRIBUTING.md](CONTRIBUTING.md). Versions follow [SemVer](https://semver.org), history is in [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE)

# Changelog

All notable changes to this project are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning: [SemVer](https://semver.org).

## [Unreleased]

### Added
- Free HUD placement by dragging (edit mode, `Ctrl+Alt+G`), position saved in `settings.ini`.
- Row layout for HUD items, FPS colour by level, *only in games* option.
- CPU load/temperature, GPU load/temperature and VRAM usage lines (NVML on NVIDIA, DXGI/PDH elsewhere).

### Changed
- HUD items now have a dim label and a bright value, separated by dividers (row) or aligned in a table (column).

### Fixed
- Broken degree sign in temperature values.

### Added (sensors)
- CPU temperature via HWiNFO shared memory when HWiNFO is running with Shared Memory Support.

### Changed
- Standby cleaner thresholds can be set from 512 MB up to 128 GB (menu and `settings.ini`).

## [0.1.0] - 2026-10-04

### Added
- FPS HUD in a configurable screen corner, driven by an ETW Present session; shows display refresh rate (Hz) when the active window is not presenting.
- Frame time, 1% low and RAM usage lines.
- Tray icon with full settings menu, INI settings in `%APPDATA%\game-fps`.
- Autostart via Task Scheduler (elevated, no UAC prompt).
- Standby list cleaner with manual and threshold-based automatic modes.
- Hotkeys `Ctrl+Alt+F` and `Ctrl+Alt+M`.
- CI build and tag-driven GitHub releases.

[Unreleased]: https://github.com/barnibeats/game-FPS/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/barnibeats/game-FPS/releases/tag/v0.1.0

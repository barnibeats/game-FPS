#include "settings.h"
#include <algorithm>
#include "compat.h"

namespace {
wchar_t g_path[MAX_PATH];

void InitPath() {
    if (g_path[0]) return;
    wchar_t dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) GetTempPathW(MAX_PATH, dir);
    wcscat_s(dir, L"\\game-fps");
    CreateDirectoryW(dir, nullptr);
    swprintf_s(g_path, L"%s\\settings.ini", dir);
}

int GetInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, g_path);
}

void PutInt(const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t b[16];
    swprintf_s(b, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, g_path);
}
}  // namespace

const wchar_t* SettingsPath() {
    InitPath();
    return g_path;
}

void SettingsLoad(Settings& s) {
    InitPath();
    s.visible = GetInt(L"hud", L"visible", 1) != 0;
    s.corner = std::clamp(GetInt(L"hud", L"corner", s.corner), 0, 4);
    s.posX = std::clamp(GetInt(L"hud", L"pos_x", s.posX), -20000, 20000);
    s.posY = std::clamp(GetInt(L"hud", L"pos_y", s.posY), -20000, 20000);
    s.layout = std::clamp(GetInt(L"hud", L"layout", s.layout), 0, 1);
    s.onlyInGames = GetInt(L"hud", L"only_in_games", 0) != 0;
    s.colorByFps = GetInt(L"hud", L"color_by_fps", 0) != 0;
    s.monitor = std::clamp(GetInt(L"hud", L"monitor", s.monitor), 0, 1);
    s.margin = std::clamp(GetInt(L"hud", L"margin", s.margin), 0, 200);
    s.fontSize = std::clamp(GetInt(L"hud", L"font_size", s.fontSize), 10, 96);
    s.opacity = std::clamp(GetInt(L"hud", L"opacity", s.opacity), 20, 100);
    s.color = (COLORREF)(GetInt(L"hud", L"color", (int)s.color) & 0xFFFFFF);  // 0xBBGGRR
    s.intervalMs = GetInt(L"hud", L"interval_ms", s.intervalMs) >= 2000 ? 2000 : 1000;
    s.showFrametime = GetInt(L"hud", L"show_frametime", 0) != 0;
    s.showLow = GetInt(L"hud", L"show_1pct_low", 0) != 0;
    s.showRam = GetInt(L"hud", L"show_ram", 0) != 0;
    s.showCpuLoad = GetInt(L"hud", L"show_cpu_load", 0) != 0;
    s.showCpuTemp = GetInt(L"hud", L"show_cpu_temp", 0) != 0;
    s.showGpuLoad = GetInt(L"hud", L"show_gpu_load", 0) != 0;
    s.showGpuTemp = GetInt(L"hud", L"show_gpu_temp", 0) != 0;
    s.showVram = GetInt(L"hud", L"show_vram", 0) != 0;
    s.autoClean = GetInt(L"memory", L"auto_clean", 0) != 0;
    s.standbyMB = std::clamp(GetInt(L"memory", L"standby_mb", s.standbyMB), 128, 131072);
    s.freeMB = std::clamp(GetInt(L"memory", L"free_mb", s.freeMB), 128, 131072);
}

void SettingsSave(const Settings& s) {
    InitPath();
    PutInt(L"hud", L"visible", s.visible);
    PutInt(L"hud", L"corner", s.corner);
    PutInt(L"hud", L"pos_x", s.posX);
    PutInt(L"hud", L"pos_y", s.posY);
    PutInt(L"hud", L"layout", s.layout);
    PutInt(L"hud", L"only_in_games", s.onlyInGames);
    PutInt(L"hud", L"color_by_fps", s.colorByFps);
    PutInt(L"hud", L"monitor", s.monitor);
    PutInt(L"hud", L"margin", s.margin);
    PutInt(L"hud", L"font_size", s.fontSize);
    PutInt(L"hud", L"opacity", s.opacity);
    PutInt(L"hud", L"color", (int)s.color);
    PutInt(L"hud", L"interval_ms", s.intervalMs);
    PutInt(L"hud", L"show_frametime", s.showFrametime);
    PutInt(L"hud", L"show_1pct_low", s.showLow);
    PutInt(L"hud", L"show_ram", s.showRam);
    PutInt(L"hud", L"show_cpu_load", s.showCpuLoad);
    PutInt(L"hud", L"show_cpu_temp", s.showCpuTemp);
    PutInt(L"hud", L"show_gpu_load", s.showGpuLoad);
    PutInt(L"hud", L"show_gpu_temp", s.showGpuTemp);
    PutInt(L"hud", L"show_vram", s.showVram);
    PutInt(L"memory", L"auto_clean", s.autoClean);
    PutInt(L"memory", L"standby_mb", s.standbyMB);
    PutInt(L"memory", L"free_mb", s.freeMB);
}

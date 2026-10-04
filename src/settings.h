#pragma once
#include <windows.h>

enum Corner { CornerTopLeft = 0, CornerTopRight, CornerBottomLeft, CornerBottomRight, CornerCustom };
enum Layout { LayoutColumn = 0, LayoutRow };

struct Settings {
    bool visible = true;
    int corner = CornerTopRight;
    int posX = 16, posY = 16; // used when corner == CornerCustom (screen pixels)
    int layout = LayoutColumn;
    bool onlyInGames = false; // hide the HUD while the active window is not presenting frames
    bool colorByFps = false;  // green / yellow / red depending on FPS
    int monitor = 0;          // 0 = primary, 1 = monitor of the active window
    int margin = 16;          // px at 96 DPI
    int fontSize = 20;        // px at 96 DPI
    int opacity = 80;         // percent
    COLORREF color = RGB(0, 255, 127);
    int intervalMs = 1000;
    bool showFrametime = false;
    bool showLow = false;
    bool showRam = false;
    // Standby list cleaner (ISLC-style)
    bool autoClean = false;
    int standbyMB = 1024;     // clean when standby list >= this ...
    int freeMB = 1024;        // ... and free memory < this
};

void SettingsLoad(Settings& s);
void SettingsSave(const Settings& s);
const wchar_t* SettingsPath();

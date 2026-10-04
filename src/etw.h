#pragma once
#include <windows.h>

struct FpsResult {
    bool valid = false;
    bool desktop = false;  // true when the active window is not presenting: fps holds the display refresh rate (Hz)
    DWORD pid = 0;
    double fps = 0;
    double frameMs = 0;
    double low1 = 0;       // 1% low FPS, 0 if not enough samples
};

// Real-time ETW session on Microsoft-Windows-DxgKrnl (Present events). Needs admin.
bool EtwStart();
void EtwStop();
bool EtwRunning();

// Called once per HUD tick from the UI thread.
void FpsPoll(DWORD foregroundPid, bool wantLow, FpsResult& out);

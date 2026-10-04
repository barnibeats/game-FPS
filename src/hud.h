#pragma once
#include <windows.h>
#include "etw.h"
#include "memory.h"
#include "sensors.h"
#include "settings.h"

bool HudCreate(HINSTANCE inst);
void HudDestroy();
// Re-apply font, colour, opacity and position after a settings change.
void HudApply(const Settings& s);
void HudShow(bool show);
void HudUpdate(const Settings& s, const FpsResult& r, const MemStatus* mem, const SensorData* sd);
// Re-assert topmost z-order and position (monitor/resolution changes, active-window monitor).
void HudReposition(const Settings& s);

// Edit mode: the HUD becomes draggable with the mouse (free placement).
void HudSetEdit(bool edit);
bool HudGetPos(int* x, int* y);

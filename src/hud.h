#pragma once
#include <windows.h>
#include "etw.h"
#include "memory.h"
#include "settings.h"

bool HudCreate(HINSTANCE inst);
void HudDestroy();
// Re-apply font, colour, opacity and position after a settings change.
void HudApply(const Settings& s);
void HudShow(bool show);
void HudUpdate(const Settings& s, const FpsResult& r, const MemStatus* mem);
// Re-assert topmost z-order and position (monitor/resolution changes, active-window monitor).
void HudReposition(const Settings& s);

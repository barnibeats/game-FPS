#pragma once

// Autostart is a Task Scheduler logon task with highest privileges, so the
// elevated app starts at logon without a UAC prompt.
bool AutostartEnabled();
bool AutostartSet(bool enable);

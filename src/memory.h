#pragma once
#include <windows.h>

struct MemStatus {
    ULONGLONG totalMB = 0;
    ULONGLONG availMB = 0;    // free + standby
    ULONGLONG freeMB = 0;     // zero + free pages
    ULONGLONG standbyMB = 0;  // cached, reclaimable pages
    int loadPercent = 0;
};

// Query physical memory lists. Returns false if the standby breakdown is unavailable.
bool MemQuery(MemStatus& out);
// ISLC-style cleaning. Both need SeProfileSingleProcessPrivilege (run as admin).
bool MemPurgeStandby();
bool MemEmptyWorkingSets();
// Called from the auto-clean timer: purges when standby >= standbyMB and free < freeMB.
// Returns true if a purge was performed.
bool MemAutoClean(int standbyMB, int freeMB);

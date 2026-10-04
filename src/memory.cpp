#include "memory.h"

namespace {
constexpr ULONG kSystemMemoryListInformation = 80;
enum MemoryCommand : ULONG {
    MemoryEmptyWorkingSets = 2,
    MemoryFlushModifiedList = 3,
    MemoryPurgeStandbyList = 4,
};

struct SystemMemoryListInfo {
    ULONG_PTR ZeroPageCount;
    ULONG_PTR FreePageCount;
    ULONG_PTR ModifiedPageCount;
    ULONG_PTR ModifiedNoWritePageCount;
    ULONG_PTR BindCount;
    ULONG_PTR PageCountByPriority[8];
    ULONG_PTR RepurposedPagesByPriority[8];
    ULONG_PTR ModifiedPageCountPageFile;
};

using NtSetFn = LONG(NTAPI*)(ULONG, PVOID, ULONG);
using NtQueryFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

NtSetFn g_set;
NtQueryFn g_query;
bool g_init;
bool g_priv;

void Init() {
    if (g_init) return;
    g_init = true;
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        g_set = (NtSetFn)(void*)GetProcAddress(nt, "NtSetSystemInformation");
        g_query = (NtQueryFn)(void*)GetProcAddress(nt, "NtQuerySystemInformation");
    }
    HANDLE tok;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        const wchar_t* names[] = {L"SeProfileSingleProcessPrivilege", L"SeIncreaseQuotaPrivilege"};
        g_priv = true;
        for (auto n : names) {
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!LookupPrivilegeValueW(nullptr, n, &tp.Privileges[0].Luid) ||
                !AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) || GetLastError() != ERROR_SUCCESS)
                g_priv = false;
        }
        CloseHandle(tok);
    }
}

bool Command(MemoryCommand c) {
    Init();
    if (!g_set || !g_priv) return false;
    ULONG cmd = c;
    return g_set(kSystemMemoryListInformation, &cmd, sizeof(cmd)) >= 0;
}
}  // namespace

bool MemQuery(MemStatus& out) {
    Init();
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return false;
    out.totalMB = ms.ullTotalPhys >> 20;
    out.availMB = ms.ullAvailPhys >> 20;
    out.loadPercent = (int)ms.dwMemoryLoad;

    SystemMemoryListInfo info{};
    if (!g_query || g_query(kSystemMemoryListInformation, &info, sizeof(info), nullptr) < 0) {
        out.freeMB = out.standbyMB = 0;
        return false;
    }
    ULONGLONG standby = 0;
    for (auto p : info.PageCountByPriority) standby += p;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    out.standbyMB = (standby * si.dwPageSize) >> 20;
    out.freeMB = ((ULONGLONG)(info.ZeroPageCount + info.FreePageCount) * si.dwPageSize) >> 20;
    return true;
}

bool MemPurgeStandby() { return Command(MemoryPurgeStandbyList); }

bool MemEmptyWorkingSets() {
    bool a = Command(MemoryEmptyWorkingSets);
    bool b = Command(MemoryFlushModifiedList);
    return a || b;
}

bool MemAutoClean(int standbyMB, int freeMB) {
    MemStatus m;
    if (!MemQuery(m)) return false;
    if (m.standbyMB >= (ULONGLONG)standbyMB && m.freeMB < (ULONGLONG)freeMB) return MemPurgeStandby();
    return false;
}

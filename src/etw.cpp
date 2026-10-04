#include "etw.h"
#include <evntcons.h>
#include <evntrace.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <functional>

// Microsoft-Windows-DxgKrnl: event 0xB8 (Present) is emitted for every swap-chain present
// of any graphics API (D3D9/10/11/12, OpenGL, Vulkan); keyword 0x8000000 selects only
// present-related events so the session stays very cheap.
static const GUID kDxgKrnl = {0x802EC45A, 0x1E99, 0x4B83, {0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D}};

namespace {
constexpr USHORT kPresentEventId = 0xB8;
constexpr ULONGLONG kPresentKeyword = 0x8000000;
constexpr wchar_t kSession[] = L"game-fps-etw";
constexpr unsigned kSlots = 512;
constexpr int kRing = 512;

struct Slot {
    std::atomic<DWORD> pid;
    std::atomic<uint32_t> count;
    std::atomic<int64_t> last;
};
Slot g_slots[kSlots];

struct SlotState {
    DWORD pid;
    int64_t prev;
    double fps;
    int idle;
};
SlotState g_state[kSlots];

std::atomic<DWORD> g_track{0};
SRWLOCK g_ringLock = SRWLOCK_INIT;
float g_ring[kRing];
int g_ringN, g_ringHead;
int64_t g_ringLast;

int64_t g_freq = 1;
TRACEHANDLE g_session, g_consumer;
HANDLE g_thread;
EVENT_TRACE_PROPERTIES* g_props;
ULONG g_propsSize;
std::atomic<bool> g_running{false};

Slot* FindSlot(DWORD pid) {
    unsigned i = (pid >> 2) % kSlots;
    for (unsigned k = 0; k < kSlots; ++k, i = (i + 1) % kSlots) {
        DWORD p = g_slots[i].pid.load(std::memory_order_relaxed);
        if (p == pid) return &g_slots[i];
        if (p == 0) {
            DWORD e = 0;
            if (g_slots[i].pid.compare_exchange_strong(e, pid) || e == pid) return &g_slots[i];
        }
    }
    return nullptr;
}

void RingReset() {
    AcquireSRWLockExclusive(&g_ringLock);
    g_ringN = g_ringHead = 0;
    g_ringLast = 0;
    ReleaseSRWLockExclusive(&g_ringLock);
}

void RingPush(int64_t ts) {
    AcquireSRWLockExclusive(&g_ringLock);
    if (g_ringLast) {
        double ms = (double)(ts - g_ringLast) * 1000.0 / (double)g_freq;
        if (ms > 0 && ms < 1000) {
            g_ring[g_ringHead] = (float)ms;
            g_ringHead = (g_ringHead + 1) % kRing;
            if (g_ringN < kRing) ++g_ringN;
        }
    }
    g_ringLast = ts;
    ReleaseSRWLockExclusive(&g_ringLock);
}

void WINAPI OnEvent(PEVENT_RECORD r) {
    if (r->EventHeader.EventDescriptor.Id != kPresentEventId) return;
    DWORD pid = r->EventHeader.ProcessId;
    if (pid == 0 || pid == 4) return;
    int64_t ts = r->EventHeader.TimeStamp.QuadPart;
    if (Slot* s = FindSlot(pid)) {
        s->count.fetch_add(1, std::memory_order_relaxed);
        s->last.store(ts, std::memory_order_relaxed);
    }
    if (pid == g_track.load(std::memory_order_relaxed)) RingPush(ts);
}

DWORD WINAPI ConsumerThread(LPVOID) {
    ProcessTrace(&g_consumer, 1, nullptr, nullptr);
    return 0;
}

// Refresh rate the DWM compositor runs at (what the desktop effectively updates at).
double DesktopHz() {
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti)) || !ti.rateRefresh.uiDenominator) return 0;
    return (double)ti.rateRefresh.uiNumerator / (double)ti.rateRefresh.uiDenominator;
}

double Low1Percent() {
    float tmp[kRing];
    int n;
    AcquireSRWLockShared(&g_ringLock);
    n = g_ringN;
    std::copy(g_ring, g_ring + n, tmp);
    ReleaseSRWLockShared(&g_ringLock);
    if (n < 100) return 0;
    int k = std::max(1, n / 100);
    std::nth_element(tmp, tmp + k - 1, tmp + n, std::greater<float>());
    double sum = 0;
    for (int i = 0; i < k; ++i) sum += tmp[i];
    return sum > 0 ? 1000.0 / (sum / k) : 0;
}

void InitProps() {
    ZeroMemory(g_props, g_propsSize);
    g_props->Wnode.BufferSize = g_propsSize;
    g_props->Wnode.ClientContext = 1;  // QPC timestamps
    g_props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    g_props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    g_props->BufferSize = 8;  // KB: small buffers -> low latency and low memory
    g_props->MinimumBuffers = 4;
    g_props->MaximumBuffers = 32;
    g_props->FlushTimer = 1;
    g_props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
}

void Cleanup() {
    free(g_props);
    g_props = nullptr;
}
}  // namespace

bool EtwRunning() { return g_running.load(); }

bool EtwStart() {
    if (g_running) return true;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;

    g_propsSize = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(kSession) + 1) * sizeof(wchar_t));
    g_props = (EVENT_TRACE_PROPERTIES*)calloc(1, g_propsSize);
    if (!g_props) return false;

    InitProps();
    ULONG st = StartTraceW(&g_session, kSession, g_props);
    if (st == ERROR_ALREADY_EXISTS) {  // leftover session from a crashed instance
        InitProps();
        ControlTraceW(0, kSession, g_props, EVENT_TRACE_CONTROL_STOP);
        InitProps();
        st = StartTraceW(&g_session, kSession, g_props);
    }
    if (st != ERROR_SUCCESS) {
        Cleanup();
        return false;
    }
    st = EnableTraceEx2(g_session, &kDxgKrnl, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                        kPresentKeyword, 0, 0, nullptr);

    EVENT_TRACE_LOGFILEW lf{};
    lf.LoggerName = const_cast<LPWSTR>(kSession);
    lf.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    lf.EventRecordCallback = OnEvent;
    g_consumer = OpenTraceW(&lf);
    if (st != ERROR_SUCCESS || g_consumer == INVALID_PROCESSTRACE_HANDLE) {
        ControlTraceW(g_session, nullptr, g_props, EVENT_TRACE_CONTROL_STOP);
        Cleanup();
        return false;
    }
    for (auto& s : g_slots) {
        s.pid = 0;
        s.count = 0;
        s.last = 0;
    }
    ZeroMemory(g_state, sizeof(g_state));
    g_track = 0;
    RingReset();

    g_thread = CreateThread(nullptr, 64 * 1024, ConsumerThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!g_thread) {
        CloseTrace(g_consumer);
        ControlTraceW(g_session, nullptr, g_props, EVENT_TRACE_CONTROL_STOP);
        Cleanup();
        return false;
    }
    g_running = true;
    return true;
}

void EtwStop() {
    if (!g_running.exchange(false)) return;
    ControlTraceW(g_session, nullptr, g_props, EVENT_TRACE_CONTROL_STOP);  // makes ProcessTrace return
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    CloseTrace(g_consumer);
    Cleanup();
}

void FpsPoll(DWORD fgPid, bool wantLow, FpsResult& out) {
    out = FpsResult{};
    double fgFps = 0;

    if (g_running) {
        for (unsigned i = 0; i < kSlots; ++i) {
            DWORD pid = g_slots[i].pid.load(std::memory_order_relaxed);
            if (!pid) continue;
            SlotState& st = g_state[i];
            if (st.pid != pid) {
                st = SlotState{};
                st.pid = pid;
            }
            uint32_t n = g_slots[i].count.exchange(0, std::memory_order_relaxed);
            int64_t last = g_slots[i].last.load(std::memory_order_relaxed);
            if (n > 0) {
                if (st.prev && last > st.prev)
                    st.fps = (double)n * (double)g_freq / (double)(last - st.prev);
                st.prev = last;
                st.idle = 0;
            } else if (++st.idle >= 2) {
                st.fps = 0;
                st.prev = 0;
            }
            if (pid == fgPid) fgFps = st.fps;
        }
    }

    if (fgFps >= 1.0) {
        out.valid = true;
        out.pid = fgPid;
        out.fps = fgFps;
        out.frameMs = 1000.0 / fgFps;
        g_track = fgPid;
        if (wantLow) out.low1 = Low1Percent();
        return;
    }
    if (g_track.exchange(0)) RingReset();
    // Foreground window is not presenting (idle desktop): show the compositor refresh rate.
    double hz = DesktopHz();
    out.desktop = true;
    out.valid = hz > 0;
    out.fps = hz;
    out.frameMs = hz > 0 ? 1000.0 / hz : 0;
}

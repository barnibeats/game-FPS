#include "sensors.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace {
// ---------------------------------------------------------------- CPU load
ULONGLONG g_prevIdle, g_prevKernel, g_prevUser;

ULONGLONG Q(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

double CpuLoad() {
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return -1;
    ULONGLONG I = Q(i), K = Q(k), U = Q(u);
    double r = -1;  // first sample has no baseline yet
    if (g_prevKernel || g_prevUser) {
        ULONGLONG total = (K - g_prevKernel) + (U - g_prevUser);  // kernel time includes idle
        if (total) r = std::clamp(100.0 * (1.0 - (double)(I - g_prevIdle) / (double)total), 0.0, 100.0);
    }
    g_prevIdle = I;
    g_prevKernel = K;
    g_prevUser = U;
    return r;
}

// ---------------------------------------------------------------- PDH helper
struct PdhCounter {
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER c = nullptr;
    bool failed = false;

    bool Open(const wchar_t* path) {
        if (q) return true;
        if (failed) return false;
        if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS || PdhAddEnglishCounterW(q, path, 0, &c) != ERROR_SUCCESS) {
            Close();
            failed = true;  // e.g. no such counter on this PC; do not retry every tick
            return false;
        }
        PdhCollectQueryData(q);
        return true;
    }
    void Close() {
        if (q) PdhCloseQuery(q);
        q = nullptr;
        c = nullptr;
    }
    // Collects and returns all formatted instances (name, value).
    bool Read(std::vector<std::pair<std::wstring, double>>& out) {
        out.clear();
        if (!q || PdhCollectQueryData(q) != ERROR_SUCCESS) return false;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != (PDH_STATUS)PDH_MORE_DATA) return false;
        std::vector<BYTE> buf(bytes);
        auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
        if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return false;
        for (DWORD i = 0; i < count; ++i)
            if (items[i].FmtValue.CStatus == ERROR_SUCCESS || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
                out.emplace_back(items[i].szName, items[i].FmtValue.doubleValue);
        return true;
    }
};

PdhCounter g_thermal, g_gpuEngine;
int g_gpuEngineAge;

// ACPI thermal zones (Kelvin). Many PCs expose none or a meaningless value; "not available" then.
double CpuTemp() {
    static std::vector<std::pair<std::wstring, double>> v;
    if (!g_thermal.Open(L"\\Thermal Zone Information(*)\\Temperature") || !g_thermal.Read(v) || v.empty()) return -1;
    double best = -1;
    for (auto& p : v) best = std::max(best, p.second - 273.15);
    return best > 0 && best < 150 ? best : -1;
}

// GPU load for any vendor: sum per engine type (3D, Compute, Copy...), take the busiest type.
double GpuLoadPdh() {
    static std::vector<std::pair<std::wstring, double>> v;
    if (g_gpuEngine.q && ++g_gpuEngineAge >= 30) {  // new processes add new engine instances
        g_gpuEngine.Close();
        g_gpuEngineAge = 0;
    }
    if (!g_gpuEngine.Open(L"\\GPU Engine(*)\\Utilization Percentage")) return -1;
    if (!g_gpuEngine.Read(v)) return -1;
    std::vector<std::pair<std::wstring, double>> types;
    for (auto& p : v) {
        size_t pos = p.first.rfind(L"engtype_");
        if (pos == std::wstring::npos) continue;
        std::wstring t = p.first.substr(pos);
        auto it = std::find_if(types.begin(), types.end(), [&](auto& e) { return e.first == t; });
        if (it == types.end()) types.emplace_back(t, p.second);
        else it->second += p.second;
    }
    double best = 0;
    for (auto& t : types) best = std::max(best, t.second);
    return std::min(best, 100.0);
}

// ---------------------------------------------------------------- NVML (NVIDIA)
struct NvmlUtil { unsigned gpu, memory; };
struct NvmlMem { unsigned long long total, free, used; };
struct Nvml {
    HMODULE lib = nullptr;
    bool tried = false, ok = false;
    void* dev = nullptr;
    int (*init)() = nullptr;
    int (*shutdown)() = nullptr;
    int (*byIndex)(unsigned, void**) = nullptr;
    int (*temp)(void*, int, unsigned*) = nullptr;
    int (*util)(void*, NvmlUtil*) = nullptr;
    int (*mem)(void*, NvmlMem*) = nullptr;
} g_nv;

template <class T>
bool Sym(HMODULE m, const char* name, T& fn) {
    fn = (T)(void*)GetProcAddress(m, name);
    return fn != nullptr;
}

void NvmlOpen() {
    if (g_nv.tried) return;
    g_nv.tried = true;
    g_nv.lib = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_nv.lib) g_nv.lib = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!g_nv.lib) return;
    HMODULE m = g_nv.lib;
    if (Sym(m, "nvmlInit_v2", g_nv.init) && Sym(m, "nvmlShutdown", g_nv.shutdown) &&
        Sym(m, "nvmlDeviceGetHandleByIndex_v2", g_nv.byIndex) && Sym(m, "nvmlDeviceGetTemperature", g_nv.temp) &&
        Sym(m, "nvmlDeviceGetUtilizationRates", g_nv.util) && Sym(m, "nvmlDeviceGetMemoryInfo", g_nv.mem) &&
        g_nv.init() == 0 && g_nv.byIndex(0, &g_nv.dev) == 0)
        g_nv.ok = true;
}

void NvmlClose() {
    if (g_nv.ok && g_nv.shutdown) g_nv.shutdown();
    if (g_nv.lib) FreeLibrary(g_nv.lib);
    g_nv = Nvml{};
}

// ---------------------------------------------------------------- DXGI (VRAM on any vendor)
IDXGIAdapter3* g_adapter;
double g_vramTotalMB;
UINT g_vendor;
bool g_dxgiTried;

void DxgiOpen() {
    if (g_dxgiTried) return;
    g_dxgiTried = true;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return;
    SIZE_T best = 0;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        IDXGIAdapter3* a3 = nullptr;
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory >= best &&
            SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))) {
            if (g_adapter) g_adapter->Release();
            g_adapter = a3;
            best = d.DedicatedVideoMemory;
            g_vramTotalMB = (double)d.DedicatedVideoMemory / (1024.0 * 1024.0);
            g_vendor = d.VendorId;
        }
        a->Release();
    }
    f->Release();
}

void DxgiClose() {
    if (g_adapter) g_adapter->Release();
    g_adapter = nullptr;
    g_dxgiTried = false;
}

void GpuPoll(const SensorWant& w, SensorData& o) {
    DxgiOpen();
    if (g_vendor == 0x10DE) NvmlOpen();  // NVIDIA: NVML gives temperature, load and memory in one cheap call
    if (g_nv.ok) {
        unsigned t;
        NvmlUtil u;
        NvmlMem m;
        if (w.gpuTemp && g_nv.temp(g_nv.dev, 0, &t) == 0) o.gpuTemp = t;
        if (w.gpuLoad && g_nv.util(g_nv.dev, &u) == 0) o.gpuLoad = u.gpu;
        if (w.vram && g_nv.mem(g_nv.dev, &m) == 0) {
            o.vramUsedMB = (double)m.used / (1024.0 * 1024.0);
            o.vramTotalMB = (double)m.total / (1024.0 * 1024.0);
        }
    }
    if (w.vram && o.vramUsedMB < 0 && g_adapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(g_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
            o.vramUsedMB = (double)info.CurrentUsage / (1024.0 * 1024.0);
            o.vramTotalMB = g_vramTotalMB;
        }
    }
    if (w.gpuLoad && o.gpuLoad < 0) o.gpuLoad = GpuLoadPdh();
    // GPU temperature on AMD/Intel needs vendor SDKs; not available there (shown as "--").
}

void GpuShutdown() {
    NvmlClose();
    DxgiClose();
    g_gpuEngine.Close();
    g_gpuEngine.failed = false;
    g_gpuEngineAge = 0;
}
}  // namespace

void SensorsPoll(const SensorWant& w, SensorData& o) {
    o = SensorData{};
    if (w.cpuLoad) o.cpuLoad = CpuLoad();
    else g_prevKernel = g_prevUser = 0;
    if (w.cpuTemp) o.cpuTemp = CpuTemp();
    else g_thermal.Close();
    if (w.gpuLoad || w.gpuTemp || w.vram) GpuPoll(w, o);
    else GpuShutdown();
}

void SensorsShutdown() {
    g_prevKernel = g_prevUser = 0;
    g_thermal.Close();
    g_thermal.failed = false;
    GpuShutdown();
}

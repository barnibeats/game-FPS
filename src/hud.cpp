#include "hud.h"
#include "compat.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClass[] = L"GameFpsHud";
const wchar_t kDeg[] = L"°C";

// One HUD entry: dim label + bright value. minChars reserves width so the HUD does not jitter.
struct Item {
    std::wstring label, value;
    int minChars = 0;
    bool fps = false;  // value coloured by the user colour / FPS level
    bool operator==(const Item& o) const { return label == o.label && value == o.value; }
};

HWND g_hwnd;
HFONT g_font, g_labelFont;
Settings g_cfg;
std::vector<Item> g_items;
SIZE g_size;
int g_dpi = 96;
bool g_edit;
double g_fps = -1;  // FPS of a presenting app, -1 on the desktop

const COLORREF kBg = RGB(14, 16, 20);
const COLORREF kLabel = RGB(140, 148, 160);
const COLORREF kValue = RGB(235, 238, 242);
const COLORREF kDivider = RGB(60, 66, 76);

int Scale(int v) { return MulDiv(v, g_dpi, 96); }
bool Row() { return g_cfg.layout == LayoutRow; }

void MakeFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_labelFont) DeleteObject(g_labelFont);
    g_font = CreateFontW(-Scale(g_cfg.fontSize), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                         L"Consolas");
    g_labelFont = CreateFontW(-std::max(8, Scale(g_cfg.fontSize) * 62 / 100), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
}

int TextWidth(HDC dc, HFONT f, const std::wstring& s) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE z{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &z);
    SelectObject(dc, old);
    return z.cx;
}

struct Geometry {
    int lineH = 0, ascent = 0, labelW = 0, valueW = 0;  // column mode widths
    std::vector<int> itemLabelW, itemValueW;
};

Geometry Measure(HDC dc) {
    Geometry g;
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    g.lineH = tm.tmHeight;
    g.ascent = tm.tmAscent;
    int charW = TextWidth(dc, g_font, L"0");
    for (auto& it : g_items) {
        int lw = TextWidth(dc, g_labelFont, it.label);
        int vw = std::max(TextWidth(dc, g_font, it.value), charW * it.minChars);
        g.itemLabelW.push_back(lw);
        g.itemValueW.push_back(vw);
        g.labelW = std::max(g.labelW, lw);
        g.valueW = std::max(g.valueW, vw);
    }
    return g;
}

SIZE ContentSize(const Geometry& g) {
    int pad = Scale(10), labelGap = Scale(7), itemGap = Scale(12);
    SIZE s{};
    size_t n = g_items.size();
    if (Row()) {
        int w = 0;
        for (size_t i = 0; i < n; ++i) w += g.itemLabelW[i] + labelGap + g.itemValueW[i] + (i ? itemGap * 2 + 1 : 0);
        s.cx = w;
        s.cy = g.lineH;
    } else {
        s.cx = g.labelW + Scale(14) + g.valueW;
        s.cy = g.lineH * (LONG)n;
    }
    s.cx += pad * 2;
    s.cy += pad * 2;
    return s;
}

void Place() {
    if (!g_hwnd) return;
    int x, y;
    if (g_edit) {  // keep wherever the user dragged it
        RECT cur;
        GetWindowRect(g_hwnd, &cur);
        x = cur.left;
        y = cur.top;
    } else if (g_cfg.corner == CornerCustom) {
        POINT p{g_cfg.posX, g_cfg.posY};
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& r = mi.rcMonitor;  // keep it fully on a monitor, even after resolution changes
        x = std::clamp((int)p.x, (int)r.left, std::max((int)r.left, (int)r.right - (int)g_size.cx));
        y = std::clamp((int)p.y, (int)r.top, std::max((int)r.top, (int)r.bottom - (int)g_size.cy));
    } else {
        HMONITOR mon = g_cfg.monitor == 1 ? MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY)
                                          : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        const RECT& r = mi.rcMonitor;
        int m = Scale(g_cfg.margin);
        bool left = g_cfg.corner == CornerTopLeft || g_cfg.corner == CornerBottomLeft;
        bool top = g_cfg.corner == CornerTopLeft || g_cfg.corner == CornerTopRight;
        x = left ? r.left + m : r.right - g_size.cx - m;
        y = top ? r.top + m : r.bottom - g_size.cy - m;
    }
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, g_size.cx, g_size.cy, SWP_NOACTIVATE);
}

void Relayout() {
    HDC dc = GetDC(g_hwnd);
    SIZE s = ContentSize(Measure(dc));
    ReleaseDC(g_hwnd, dc);
    bool changed = s.cx != g_size.cx || s.cy != g_size.cy;
    g_size = s;
    Place();
    if (changed) {
        int rad = Scale(10);
        SetWindowRgn(g_hwnd, CreateRoundRectRgn(0, 0, g_size.cx + 1, g_size.cy + 1, rad, rad), TRUE);
    }
}

COLORREF FpsColor() {
    if (!g_cfg.colorByFps || g_fps < 0) return g_cfg.color;
    if (g_fps >= 60) return RGB(0, 255, 127);
    if (g_fps >= 30) return RGB(255, 221, 0);
    return RGB(255, 70, 70);
}

void Text(HDC dc, HFONT f, COLORREF c, UINT align, int x, int baseline, const std::wstring& s) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetTextAlign(dc, align | TA_BASELINE);
    TextOutW(dc, x, baseline, s.c_str(), (int)s.size());
}

void Paint(HDC dc) {
    RECT rc{0, 0, g_size.cx, g_size.cy};
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    if (g_edit) {  // visible frame while the HUD can be dragged
        HBRUSH fr = CreateSolidBrush(RGB(255, 190, 0));
        for (int i = 0; i < 2; ++i) {
            FrameRect(dc, &rc, fr);
            InflateRect(&rc, -1, -1);
        }
        DeleteObject(fr);
    }
    SetBkMode(dc, TRANSPARENT);
    Geometry g = Measure(dc);
    int pad = Scale(10), labelGap = Scale(7), itemGap = Scale(12);
    int x = pad, y = pad;
    HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    for (size_t i = 0; i < g_items.size(); ++i) {
        const Item& it = g_items[i];
        COLORREF vc = it.fps ? FpsColor() : kValue;
        int base = y + g.ascent;
        if (Row()) {
            if (i) {  // thin divider between items
                x += itemGap;
                MoveToEx(dc, x, y + g.lineH / 6, nullptr);
                LineTo(dc, x, y + g.lineH - g.lineH / 6);
                x += 1 + itemGap;
            }
            Text(dc, g_labelFont, kLabel, TA_LEFT, x, base, it.label);
            x += g.itemLabelW[i] + labelGap;
            Text(dc, g_font, vc, TA_LEFT, x, base, it.value);
            x += g.itemValueW[i];
        } else {  // label column on the left, values right-aligned
            Text(dc, g_labelFont, kLabel, TA_LEFT, pad, base, it.label);
            Text(dc, g_font, vc, TA_RIGHT, g_size.cx - pad, base, it.value);
            y += g.lineH;
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK HudProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            Paint(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:  // lets screenshot tools (PrintWindow) capture the layered window
            Paint((HDC)w);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST: return g_edit ? HTCAPTION : HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_SETCURSOR:
            if (g_edit) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            break;
    }
    return DefWindowProcW(h, m, w, l);
}

std::wstring Fmt(const wchar_t* f, double a) {
    wchar_t b[48];
    swprintf_s(b, f, a);
    return b;
}

std::wstring Pct(double v) { return v >= 0 ? Fmt(L"%.0f%%", v) : L"--%"; }
std::wstring Temp(double v) { return v >= 0 ? Fmt(L"%.0f", v) + kDeg : std::wstring(L"--") + kDeg; }
}  // namespace

bool HudCreate(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HudProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return false;
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kClass, L"game-fps hud", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return false;
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return true;
}

void HudDestroy() {
    if (g_hwnd) DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    if (g_font) DeleteObject(g_font);
    if (g_labelFont) DeleteObject(g_labelFont);
    g_font = g_labelFont = nullptr;
}

void HudApply(const Settings& s) {
    g_cfg = s;
    if (!g_hwnd) return;
    MakeFonts();
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)(255 * s.opacity / 100), LWA_ALPHA);
    if (g_items.empty()) g_items.push_back({L"FPS", L"--", 3, true});
    g_size = SIZE{};  // force region rebuild
    Relayout();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void HudShow(bool show) {
    if (!g_hwnd) return;
    ShowWindow(g_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show) Place();
}

void HudReposition(const Settings& s) {
    g_cfg = s;
    Place();
}

void HudSetEdit(bool edit) {
    if (!g_hwnd || g_edit == edit) return;
    g_edit = edit;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, edit ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT));
    if (edit) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

bool HudGetPos(int* x, int* y) {
    RECT r;
    if (!g_hwnd || !GetWindowRect(g_hwnd, &r)) return false;
    *x = r.left;
    *y = r.top;
    return true;
}

void HudUpdate(const Settings& s, const FpsResult& r, const MemStatus* mem, const SensorData* sd) {
    if (!g_hwnd) return;
    g_cfg = s;
    g_fps = (r.valid && !r.desktop) ? r.fps : -1;

    std::vector<Item> items;
    // FPS while an app presents, display refresh rate (Hz) on the idle desktop.
    items.push_back({r.desktop ? L"HZ" : L"FPS", r.valid ? Fmt(L"%.0f", r.fps) : L"--", 3, true});
    if (s.showFrametime) items.push_back({L"FRAME", r.valid ? Fmt(L"%.1f ms", r.frameMs) : L"-- ms", 7, false});
    if (s.showLow) items.push_back({L"1% LOW", r.low1 > 0 ? Fmt(L"%.0f", r.low1) : L"--", 3, false});
    if (s.showRam && mem)
        items.push_back({L"RAM",
                         Pct(mem->loadPercent) + L"  " + Fmt(L"%.1f GB", (double)(mem->totalMB - mem->availMB) / 1024.0),
                         13, false});
    if (sd) {
        if (s.showCpuLoad || s.showCpuTemp) {
            std::wstring v = s.showCpuLoad ? Pct(sd->cpuLoad) : L"";
            if (s.showCpuTemp) v += (v.empty() ? L"" : L"  ") + Temp(sd->cpuTemp);
            items.push_back({L"CPU", v, s.showCpuLoad && s.showCpuTemp ? 9 : 4, false});
        }
        if (s.showGpuLoad || s.showGpuTemp) {
            std::wstring v = s.showGpuLoad ? Pct(sd->gpuLoad) : L"";
            if (s.showGpuTemp) v += (v.empty() ? L"" : L"  ") + Temp(sd->gpuTemp);
            items.push_back({L"GPU", v, s.showGpuLoad && s.showGpuTemp ? 9 : 4, false});
        }
        if (s.showVram) {
            std::wstring v = sd->vramUsedMB >= 0
                                 ? Fmt(L"%.1f", sd->vramUsedMB / 1024.0) + Fmt(L"/%.0f GB", sd->vramTotalMB / 1024.0)
                                 : L"--";
            items.push_back({L"VRAM", v, 11, false});
        }
    }

    if (items != g_items) {
        g_items = std::move(items);
        Relayout();
    }
    // "Only in games": hide while the active window is not presenting frames.
    bool want = g_edit || !(s.onlyInGames && r.desktop);
    if (want != (IsWindowVisible(g_hwnd) != FALSE)) ShowWindow(g_hwnd, want ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (want) {
        InvalidateRect(g_hwnd, nullptr, FALSE);  // colour may change with FPS
        // Games can steal the topmost slot; re-assert cheaply once per tick.
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (s.monitor == 1 && s.corner != CornerCustom) Place();
    }
}

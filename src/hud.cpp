#include "hud.h"
#include "compat.h"
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClass[] = L"GameFpsHud";

HWND g_hwnd;
HFONT g_font;
Settings g_cfg;
std::vector<std::wstring> g_lines;
SIZE g_size;
int g_dpi = 96;

int Scale(int v) { return MulDiv(v, g_dpi, 96); }

void MakeFont() {
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-Scale(g_cfg.fontSize), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                         L"Consolas");
}

// Window size = widest line (never narrower than a 4-digit FPS line, so it does not jitter).
SIZE Measure(HDC dc) {
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE base{};
    GetTextExtentPoint32W(dc, L"0000 FPS", 8, &base);
    SIZE out = base;
    for (auto& l : g_lines) {
        SIZE s{};
        GetTextExtentPoint32W(dc, l.c_str(), (int)l.size(), &s);
        if (s.cx > out.cx) out.cx = s.cx;
    }
    SelectObject(dc, old);
    int pad = Scale(8);
    out.cx += pad * 2;
    out.cy = base.cy * (LONG)(g_lines.empty() ? 1 : g_lines.size()) + pad * 2;
    return out;
}

void Place() {
    if (!g_hwnd) return;
    HMONITOR mon = g_cfg.monitor == 1 ? MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY)
                                      : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    const RECT& r = mi.rcMonitor;
    int m = Scale(g_cfg.margin);
    int x = (g_cfg.corner == CornerTopLeft || g_cfg.corner == CornerBottomLeft) ? r.left + m : r.right - g_size.cx - m;
    int y = (g_cfg.corner == CornerTopLeft || g_cfg.corner == CornerTopRight) ? r.top + m : r.bottom - g_size.cy - m;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, g_size.cx, g_size.cy, SWP_NOACTIVATE);
}

void Relayout() {
    HDC dc = GetDC(g_hwnd);
    SIZE s = Measure(dc);
    ReleaseDC(g_hwnd, dc);
    bool changed = s.cx != g_size.cx || s.cy != g_size.cy;
    g_size = s;
    Place();
    if (changed) {
        int rad = Scale(10);
        SetWindowRgn(g_hwnd, CreateRoundRectRgn(0, 0, g_size.cx + 1, g_size.cy + 1, rad, rad), TRUE);
    }
}

void Paint(HDC dc) {
    RECT rc{0, 0, g_size.cx, g_size.cy};
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 20));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_cfg.color);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    int pad = Scale(8);
    bool right = g_cfg.corner == CornerTopRight || g_cfg.corner == CornerBottomRight;
    int y = pad;
    for (size_t i = 0; i < g_lines.size(); ++i, y += tm.tmHeight) {
        RECT lr{pad, y, g_size.cx - pad, y + tm.tmHeight};
        // First line (FPS) in the user colour, secondary lines dimmed.
        SetTextColor(dc, i == 0 ? g_cfg.color : RGB(200, 205, 210));
        DrawTextW(dc, g_lines[i].c_str(), -1, &lr, (right ? DT_RIGHT : DT_LEFT) | DT_NOPREFIX | DT_SINGLELINE | DT_NOCLIP);
    }
    SelectObject(dc, old);
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
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, m, w, l);
}
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
    g_font = nullptr;
}

void HudApply(const Settings& s) {
    g_cfg = s;
    if (!g_hwnd) return;
    MakeFont();
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)(255 * s.opacity / 100), LWA_ALPHA);
    if (g_lines.empty()) g_lines.push_back(L"-- FPS");
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

void HudUpdate(const Settings& s, const FpsResult& r, const MemStatus* mem) {
    if (!g_hwnd) return;
    g_cfg = s;
    std::vector<std::wstring> lines;
    wchar_t b[64];
    if (r.valid) swprintf_s(b, r.desktop ? L"%d FPS *" : L"%d FPS", (int)(r.fps + 0.5));
    else wcscpy_s(b, L"-- FPS");
    lines.emplace_back(b);
    if (s.showFrametime) {
        if (r.valid) swprintf_s(b, L"%.1f ms", r.frameMs);
        else wcscpy_s(b, L"-- ms");
        lines.emplace_back(b);
    }
    if (s.showLow) {
        if (r.low1 > 0) swprintf_s(b, L"1%% low %d", (int)(r.low1 + 0.5));
        else wcscpy_s(b, L"1% low --");
        lines.emplace_back(b);
    }
    if (s.showRam && mem) {
        swprintf_s(b, L"RAM %d%% %.1fG", mem->loadPercent, (double)(mem->totalMB - mem->availMB) / 1024.0);
        lines.emplace_back(b);
    }
    if (lines != g_lines) {
        g_lines = std::move(lines);
        Relayout();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
    // Games can steal the topmost slot; re-assert cheaply once per tick.
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (s.monitor == 1) Place();
}

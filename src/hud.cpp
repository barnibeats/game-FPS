#include "hud.h"
#include "compat.h"
#include <algorithm>
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
bool g_edit;
double g_fps = -1;  // FPS of a presenting app, -1 on the desktop

int Scale(int v) { return MulDiv(v, g_dpi, 96); }
bool Row() { return g_cfg.layout == LayoutRow; }

void MakeFont() {
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-Scale(g_cfg.fontSize), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                         L"Consolas");
}

int TextWidth(HDC dc, const std::wstring& s) {
    SIZE z{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &z);
    return z.cx;
}

// Width of one item; the first (FPS) item never gets narrower than a 4-digit value so the HUD does not jitter.
int ItemWidth(HDC dc, size_t i) {
    int w = TextWidth(dc, g_lines[i]);
    if (i == 0) w = std::max(w, TextWidth(dc, L"0000 FPS"));
    return w;
}

SIZE Measure(HDC dc, int* lineH) {
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    *lineH = tm.tmHeight;
    int pad = Scale(8), gap = Scale(18);
    SIZE out{};
    if (g_lines.empty()) g_lines.push_back(L"-- FPS");
    if (Row()) {
        int w = 0;
        for (size_t i = 0; i < g_lines.size(); ++i) w += ItemWidth(dc, i) + (i ? gap : 0);
        out.cx = w;
        out.cy = tm.tmHeight;
    } else {
        int w = 0;
        for (size_t i = 0; i < g_lines.size(); ++i) w = std::max(w, ItemWidth(dc, i));
        out.cx = w;
        out.cy = tm.tmHeight * (LONG)g_lines.size();
    }
    SelectObject(dc, old);
    out.cx += pad * 2;
    out.cy += pad * 2;
    return out;
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
    int lh;
    SIZE s = Measure(dc, &lh);
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

void Paint(HDC dc) {
    RECT rc{0, 0, g_size.cx, g_size.cy};
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 20));
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
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    int pad = Scale(8), gap = Scale(18);
    bool right = !Row() && (g_cfg.corner == CornerTopRight || g_cfg.corner == CornerBottomRight);
    int x = pad, y = pad;
    for (size_t i = 0; i < g_lines.size(); ++i) {
        int w = Row() ? ItemWidth(dc, i) : g_size.cx - pad * 2;
        RECT lr{x, y, x + w, y + tm.tmHeight};
        // First item (FPS) in the user colour, secondary items dimmed.
        SetTextColor(dc, i == 0 ? FpsColor() : RGB(200, 205, 210));
        DrawTextW(dc, g_lines[i].c_str(), -1, &lr,
                  (right ? DT_RIGHT : DT_LEFT) | DT_NOPREFIX | DT_SINGLELINE | DT_NOCLIP);
        if (Row()) x += w + gap;
        else y += tm.tmHeight;
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

void HudUpdate(const Settings& s, const FpsResult& r, const MemStatus* mem) {
    if (!g_hwnd) return;
    g_cfg = s;
    g_fps = (r.valid && !r.desktop) ? r.fps : -1;
    std::vector<std::wstring> lines;
    wchar_t b[64];
    if (r.valid) swprintf_s(b, r.desktop ? L"%d Hz" : L"%d FPS", (int)(r.fps + 0.5));
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
    bool changed = lines != g_lines;
    if (changed) {
        g_lines = std::move(lines);
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

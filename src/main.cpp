// game-fps: lightweight FPS HUD + standby list cleaner living in the tray.
#include "compat.h"
#include <shellapi.h>

#include <cwchar>
#include "autostart.h"
#include "etw.h"
#include "hud.h"
#include "memory.h"
#include "settings.h"

#define STR2(x) L##x
#define STR(x) STR2(x)

namespace {
constexpr wchar_t kWndClass[] = L"GameFpsMain";
constexpr wchar_t kRepoUrl[] = L"https://github.com/barnibeats/game-FPS";
constexpr wchar_t kReleasesUrl[] = L"https://github.com/barnibeats/game-FPS/releases/latest";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT_PTR TIMER_HUD = 1, TIMER_CLEAN = 2, TIMER_TRIM = 3;
constexpr int HOTKEY_TOGGLE = 1, HOTKEY_CLEAN = 2;
constexpr int IDI_APP = 101;

enum Cmd {
    IDM_TOGGLE = 100,
    IDM_CORNER = 110,    // +0..3
    IDM_MONITOR = 120,   // +0..1
    IDM_FONT = 130,      // +index
    IDM_OPACITY = 150,   // +index
    IDM_COLOR = 170,     // +index
    IDM_INTERVAL = 190,  // +0..1
    IDM_FRAMETIME = 200,
    IDM_LOW = 201,
    IDM_RAM = 202,
    IDM_CLEAN_NOW = 210,
    IDM_CLEAN_FULL = 211,
    IDM_AUTOCLEAN = 212,
    IDM_STANDBY = 220,   // +index
    IDM_FREE = 240,      // +index
    IDM_AUTOSTART = 260,
    IDM_OPEN_INI = 261,
    IDM_UPDATES = 262,
    IDM_ABOUT = 263,
    IDM_EXIT = 264,
};

const int kFonts[] = {14, 16, 18, 20, 24, 28, 32, 40};
const int kOpacities[] = {30, 50, 70, 80, 90, 100};
struct NamedColor { const wchar_t* name; COLORREF c; };
const NamedColor kColors[] = {
    {L"Зелёный", RGB(0, 255, 127)}, {L"Белый", RGB(255, 255, 255)}, {L"Жёлтый", RGB(255, 221, 0)},
    {L"Голубой", RGB(0, 200, 255)}, {L"Оранжевый", RGB(255, 140, 0)}, {L"Красный", RGB(255, 70, 70)},
};
const int kThresholds[] = {512, 1024, 2048, 4096};

HINSTANCE g_inst;
HWND g_wnd;
Settings g_cfg;
NOTIFYICONDATAW g_nid;
UINT g_taskbarCreated;
bool g_etwFailed;

void Balloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(n.szInfoTitle, title);
    wcscpy_s(n.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void TrimMyMemory() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

void AddTrayIcon() {
    g_nid = NOTIFYICONDATAW{};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wcscpy_s(g_nid.szTip, L"game-fps — FPS HUD");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

DWORD ForegroundPid() {
    DWORD pid = 0;
    if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &pid);
    return pid;
}

void Tick() {
    FpsResult r;
    FpsPoll(ForegroundPid(), g_cfg.showLow, r);
    MemStatus mem;
    bool needMem = g_cfg.showRam;
    if (needMem) MemQuery(mem);
    HudUpdate(g_cfg, r, needMem ? &mem : nullptr);
}

// Start/stop everything that costs CPU depending on HUD visibility.
void ApplyRuntimeState() {
    KillTimer(g_wnd, TIMER_HUD);
    if (g_cfg.visible) {
        if (!EtwRunning() && !EtwStart() && !g_etwFailed) {
            g_etwFailed = true;
            Balloon(L"game-fps", L"Не удалось запустить трассировку. Запустите приложение от администратора.");
        }
        HudShow(true);
        Tick();
        SetTimer(g_wnd, TIMER_HUD, g_cfg.intervalMs, nullptr);
    } else {
        HudShow(false);
        EtwStop();
    }
    KillTimer(g_wnd, TIMER_CLEAN);
    if (g_cfg.autoClean) SetTimer(g_wnd, TIMER_CLEAN, 2000, nullptr);
    TrimMyMemory();
}

void Commit(bool reapplyHud = true) {
    SettingsSave(g_cfg);
    if (reapplyHud) HudApply(g_cfg);
    ApplyRuntimeState();
}

void ToggleHud() {
    g_cfg.visible = !g_cfg.visible;
    Commit(false);
}

void CleanNow(bool full) {
    MemStatus before, after;
    MemQuery(before);
    bool ok = MemPurgeStandby();
    if (full) ok = MemEmptyWorkingSets() || ok;
    if (!ok) {
        Balloon(L"Очистка памяти", L"Не удалось (нужны права администратора).");
        return;
    }
    MemQuery(after);
    wchar_t t[160];
    swprintf_s(t, L"Standby: %llu → %llu МБ. Свободно: %llu → %llu МБ.", before.standbyMB, after.standbyMB,
               before.freeMB, after.freeMB);
    Balloon(L"Память очищена", t);
}

void AppendCheck(HMENU m, UINT id, const wchar_t* text, bool checked) {
    AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
}

void AppendRadio(HMENU m, UINT id, const wchar_t* text, bool on) {
    AppendMenuW(m, MF_STRING, id, text);
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_FTYPE | MIIM_STATE;
    mi.fType = MFT_STRING | MFT_RADIOCHECK;
    mi.fState = on ? MFS_CHECKED : MFS_UNCHECKED;
    SetMenuItemInfoW(m, id, FALSE, &mi);
}

void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    wchar_t b[64];

    AppendCheck(menu, IDM_TOGGLE, L"Показывать HUD\tCtrl+Alt+F", g_cfg.visible);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU pos = CreatePopupMenu();
    const wchar_t* corners[] = {L"Левый верхний", L"Правый верхний", L"Левый нижний", L"Правый нижний"};
    for (int i = 0; i < 4; ++i) AppendRadio(pos, IDM_CORNER + i, corners[i], g_cfg.corner == i);
    AppendMenuW(pos, MF_SEPARATOR, 0, nullptr);
    AppendRadio(pos, IDM_MONITOR, L"Основной монитор", g_cfg.monitor == 0);
    AppendRadio(pos, IDM_MONITOR + 1, L"Монитор активного окна", g_cfg.monitor == 1);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)pos, L"Позиция");

    HMENU look = CreatePopupMenu();
    HMENU font = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(kFonts) / sizeof(*kFonts)); ++i) {
        swprintf_s(b, L"%d px", kFonts[i]);
        AppendRadio(font, IDM_FONT + i, b, g_cfg.fontSize == kFonts[i]);
    }
    AppendMenuW(look, MF_POPUP, (UINT_PTR)font, L"Размер шрифта");
    HMENU op = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(kOpacities) / sizeof(*kOpacities)); ++i) {
        swprintf_s(b, L"%d%%", kOpacities[i]);
        AppendRadio(op, IDM_OPACITY + i, b, g_cfg.opacity == kOpacities[i]);
    }
    AppendMenuW(look, MF_POPUP, (UINT_PTR)op, L"Прозрачность");
    HMENU col = CreatePopupMenu();
    for (int i = 0; i < (int)(sizeof(kColors) / sizeof(*kColors)); ++i)
        AppendRadio(col, IDM_COLOR + i, kColors[i].name, g_cfg.color == kColors[i].c);
    AppendMenuW(look, MF_POPUP, (UINT_PTR)col, L"Цвет");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)look, L"Внешний вид");

    HMENU info = CreatePopupMenu();
    AppendCheck(info, IDM_FRAMETIME, L"Время кадра (мс)", g_cfg.showFrametime);
    AppendCheck(info, IDM_LOW, L"1% low FPS", g_cfg.showLow);
    AppendCheck(info, IDM_RAM, L"Загрузка RAM", g_cfg.showRam);
    AppendMenuW(info, MF_SEPARATOR, 0, nullptr);
    AppendRadio(info, IDM_INTERVAL, L"Обновление: 1 с", g_cfg.intervalMs == 1000);
    AppendRadio(info, IDM_INTERVAL + 1, L"Обновление: 2 с (экономнее)", g_cfg.intervalMs == 2000);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)info, L"Метрики");

    HMENU mem = CreatePopupMenu();
    MemStatus ms;
    if (MemQuery(ms)) {
        swprintf_s(b, L"RAM: свободно %llu МБ, standby %llu МБ", ms.freeMB, ms.standbyMB);
        AppendMenuW(mem, MF_STRING | MF_GRAYED, 0, b);
        AppendMenuW(mem, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(mem, MF_STRING, IDM_CLEAN_NOW, L"Очистить standby-список\tCtrl+Alt+M");
    AppendMenuW(mem, MF_STRING, IDM_CLEAN_FULL, L"Очистить всё (standby + working sets)");
    AppendMenuW(mem, MF_SEPARATOR, 0, nullptr);
    AppendCheck(mem, IDM_AUTOCLEAN, L"Автоочистка", g_cfg.autoClean);
    HMENU sb = CreatePopupMenu();
    HMENU fr = CreatePopupMenu();
    for (int i = 0; i < 4; ++i) {
        swprintf_s(b, L"%d МБ", kThresholds[i]);
        AppendRadio(sb, IDM_STANDBY + i, b, g_cfg.standbyMB == kThresholds[i]);
        AppendRadio(fr, IDM_FREE + i, b, g_cfg.freeMB == kThresholds[i]);
    }
    AppendMenuW(mem, MF_POPUP, (UINT_PTR)sb, L"Порог: standby ≥");
    AppendMenuW(mem, MF_POPUP, (UINT_PTR)fr, L"Порог: свободно <");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)mem, L"Очистка памяти");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendCheck(menu, IDM_AUTOSTART, L"Автозапуск с Windows", AutostartEnabled());
    AppendMenuW(menu, MF_STRING, IDM_OPEN_INI, L"Открыть файл настроек");
    AppendMenuW(menu, MF_STRING, IDM_UPDATES, L"Проверить обновления (GitHub)");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"О программе");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_wnd, nullptr);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void OnCommand(int id) {
    auto in = [&](int base, int count) { return id >= base && id < base + count; };
    if (id == IDM_TOGGLE) ToggleHud();
    else if (in(IDM_CORNER, 4)) { g_cfg.corner = id - IDM_CORNER; Commit(); }
    else if (in(IDM_MONITOR, 2)) { g_cfg.monitor = id - IDM_MONITOR; Commit(); }
    else if (in(IDM_FONT, 8)) { g_cfg.fontSize = kFonts[id - IDM_FONT]; Commit(); }
    else if (in(IDM_OPACITY, 6)) { g_cfg.opacity = kOpacities[id - IDM_OPACITY]; Commit(); }
    else if (in(IDM_COLOR, 6)) { g_cfg.color = kColors[id - IDM_COLOR].c; Commit(); }
    else if (in(IDM_INTERVAL, 2)) { g_cfg.intervalMs = id == IDM_INTERVAL ? 1000 : 2000; Commit(); }
    else if (id == IDM_FRAMETIME) { g_cfg.showFrametime = !g_cfg.showFrametime; Commit(); }
    else if (id == IDM_LOW) { g_cfg.showLow = !g_cfg.showLow; Commit(); }
    else if (id == IDM_RAM) { g_cfg.showRam = !g_cfg.showRam; Commit(); }
    else if (id == IDM_CLEAN_NOW) CleanNow(false);
    else if (id == IDM_CLEAN_FULL) CleanNow(true);
    else if (id == IDM_AUTOCLEAN) { g_cfg.autoClean = !g_cfg.autoClean; Commit(false); }
    else if (in(IDM_STANDBY, 4)) { g_cfg.standbyMB = kThresholds[id - IDM_STANDBY]; Commit(false); }
    else if (in(IDM_FREE, 4)) { g_cfg.freeMB = kThresholds[id - IDM_FREE]; Commit(false); }
    else if (id == IDM_AUTOSTART) {
        bool want = !AutostartEnabled();
        if (!AutostartSet(want)) Balloon(L"game-fps", L"Не удалось изменить автозапуск.");
    }
    else if (id == IDM_OPEN_INI) {
        SettingsSave(g_cfg);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", SettingsPath(), nullptr, SW_SHOWNORMAL);
    }
    else if (id == IDM_UPDATES) ShellExecuteW(nullptr, L"open", kReleasesUrl, nullptr, nullptr, SW_SHOWNORMAL);
    else if (id == IDM_ABOUT) {
        wchar_t t[256];
        swprintf_s(t, L"game-fps v%s\nЛёгкий FPS HUD и очистка standby-списка.\n\n%s\n\nОбновления — только через GitHub Releases.",
                   STR(APP_VERSION), kRepoUrl);
        MessageBoxW(nullptr, t, L"О программе", MB_OK | MB_ICONINFORMATION);
    }
    else if (id == IDM_EXIT) DestroyWindow(g_wnd);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRAY:
            if (LOWORD(l) == WM_LBUTTONUP) ToggleHud();
            else if (LOWORD(l) == WM_RBUTTONUP) ShowMenu();
            return 0;
        case WM_COMMAND: OnCommand(LOWORD(w)); return 0;
        case WM_TIMER:
            if (w == TIMER_HUD) Tick();
            else if (w == TIMER_CLEAN) MemAutoClean(g_cfg.standbyMB, g_cfg.freeMB);
            else if (w == TIMER_TRIM) TrimMyMemory();
            return 0;
        case WM_HOTKEY:
            if (w == HOTKEY_TOGGLE) ToggleHud();
            else if (w == HOTKEY_CLEAN) CleanNow(false);
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED: HudApply(g_cfg); return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_HUD);
            KillTimer(h, TIMER_CLEAN);
            UnregisterHotKey(h, HOTKEY_TOGGLE);
            UnregisterHotKey(h, HOTKEY_CLEAN);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            EtwStop();
            HudDestroy();
            PostQuitMessage(0);
            return 0;
        default:
            if (m == g_taskbarCreated) AddTrayIcon();  // explorer restarted
    }
    return DefWindowProcW(h, m, w, l);
}

// Run as light as possible: below-normal priority and EcoQoS (efficiency cores) where supported.
void LowerPriority() {
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    struct PowerThrottling { ULONG Version, ControlMask, StateMask; } pt{1, 1, 1};  // EXECUTION_SPEED
    using SetInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    auto fn = (SetInfoFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
    if (fn) fn(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &pt, sizeof(pt));
}
}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\game-fps-single-instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_inst = inst;
    LowerPriority();
    SettingsLoad(g_cfg);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(0, kWndClass, L"game-fps", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_wnd || !HudCreate(inst)) return 1;

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    RegisterHotKey(g_wnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'F');
    RegisterHotKey(g_wnd, HOTKEY_CLEAN, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M');
    HudApply(g_cfg);
    ApplyRuntimeState();
    SetTimer(g_wnd, TIMER_TRIM, 60000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        DispatchMessageW(&msg);
    }
    CloseHandle(once);
    return 0;
}

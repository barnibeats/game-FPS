#include "autostart.h"
#include <windows.h>
#include <string>

namespace {
constexpr wchar_t kTask[] = L"game-fps";

DWORD RunHidden(std::wstring cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

std::wstring Env(const wchar_t* name) {
    wchar_t b[256];
    DWORD n = GetEnvironmentVariableW(name, b, 256);
    return n && n < 256 ? std::wstring(b, n) : std::wstring();
}

std::wstring XmlEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': o += L"&amp;"; break;
            case L'<': o += L"&lt;"; break;
            case L'>': o += L"&gt;"; break;
            default: o += c;
        }
    }
    return o;
}
}  // namespace

bool AutostartEnabled() { return RunHidden(std::wstring(L"schtasks.exe /Query /TN ") + kTask) == 0; }

bool AutostartSet(bool enable) {
    if (!enable) return RunHidden(std::wstring(L"schtasks.exe /Delete /F /TN ") + kTask) == 0;

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring user = Env(L"USERDOMAIN") + L"\\" + Env(L"USERNAME");

    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Description>game-fps FPS HUD autostart</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + XmlEscape(user) + L"</UserId>"
        L"<Delay>PT5S</Delay></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"A\"><UserId>" + XmlEscape(user) + L"</UserId>"
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><AllowHardTerminate>true</AllowHardTerminate>"
        L"<StartWhenAvailable>true</StartWhenAvailable><Enabled>true</Enabled></Settings>"
        L"<Actions Context=\"A\"><Exec><Command>" + XmlEscape(exe) + L"</Command></Exec></Actions></Task>";

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"game-fps-task.xml";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w;
    const WORD bom = 0xFEFF;
    WriteFile(f, &bom, 2, &w, nullptr);
    WriteFile(f, xml.data(), (DWORD)(xml.size() * sizeof(wchar_t)), &w, nullptr);
    CloseHandle(f);

    DWORD rc = RunHidden(L"schtasks.exe /Create /F /TN " + std::wstring(kTask) + L" /XML \"" + path + L"\"");
    DeleteFileW(path.c_str());
    return rc == 0;
}

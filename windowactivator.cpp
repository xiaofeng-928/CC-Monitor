#include "windowactivator.h"
#include <QString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>

static qint64 getParentPid(qint64 pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    qint64 ppid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == (DWORD)pid) {
                ppid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return ppid;
}

static QString getProcessName(qint64 pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    QString name;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == (DWORD)pid) {
                name = QString::fromWCharArray(pe.szExeFile);
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}

struct EnumData {
    DWORD targetPid;
    HWND  resultHwnd;
};

struct EnumAllData {
    QString processName;
    QList<HWND> hwnds;
};

static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM param)
{
    auto *data = reinterpret_cast<EnumData*>(param);
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->targetPid) {
        // Check if window is visible or minimized (can be restored)
        bool isVisible = IsWindowVisible(hwnd);
        bool isMinimized = IsIconic(hwnd);

        if (isVisible || isMinimized) {
            // prefer top-level windows (no owner)
            if (GetWindow(hwnd, GW_OWNER) == nullptr) {
                data->resultHwnd = hwnd;
                return FALSE;
            }
            // accept owned windows if no top-level found yet
            if (data->resultHwnd == nullptr)
                data->resultHwnd = hwnd;
        }
    }
    return TRUE;
}

static BOOL CALLBACK enumAllProc(HWND hwnd, LPARAM param)
{
    auto *data = reinterpret_cast<EnumAllData*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    QString name = getProcessName(pid);

    if (name.compare(data->processName, Qt::CaseInsensitive) == 0) {
        // only top-level windows
        if (GetWindow(hwnd, GW_OWNER) == nullptr) {
            data->hwnds.append(hwnd);
        }
    }
    return TRUE;
}

static HWND findWindowByPid(qint64 pid)
{
    EnumData data{(DWORD)pid, nullptr};
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.resultHwnd;
}

static bool isTerminalProcess(const QString &name)
{
    static const QStringList terminals = {
        "WindowsTerminal.exe", "cmd.exe", "powershell.exe", "pwsh.exe",
        "ConEmu64.exe", "ConEmuC64.exe", "mintty.exe",
        "alacritty.exe", "wezterm-gui.exe", "fluentterminal.exe",
        "hyper.exe", "Tabby.exe", "rio.exe", "conhost.exe"
    };
    return terminals.contains(name, Qt::CaseInsensitive);
}

static bool isVSCodeProcess(const QString &name)
{
    return name.compare("Code.exe", Qt::CaseInsensitive) == 0;
}

// Find VSCode window that contains the given workspace path
static HWND findVSCodeWindowForPath(const QString &cwd)
{
    EnumAllData data{"Code.exe", {}};
    EnumWindows(enumAllProc, reinterpret_cast<LPARAM>(&data));

    if (data.hwnds.isEmpty()) return nullptr;

    // If only one VSCode window, use it
    if (data.hwnds.size() == 1) return data.hwnds.first();

    // Try to find window title containing the workspace folder name
    QDir dir(cwd);
    QString folderName = dir.dirName();

    for (HWND hwnd : data.hwnds) {
        wchar_t title[512];
        GetWindowTextW(hwnd, title, 512);
        QString titleStr = QString::fromWCharArray(title);
        if (titleStr.contains(folderName, Qt::CaseInsensitive)) {
            return hwnd;
        }
    }

    // Fallback: return the foreground VSCode window or first one
    HWND fg = GetForegroundWindow();
    for (HWND hwnd : data.hwnds) {
        if (hwnd == fg) return hwnd;
    }
    return data.hwnds.first();
}

// Get cwd from session file
static QString getSessionCwd(qint64 pid)
{
    QString sessionsDir = QDir::homePath() + "/.claude/sessions";
    QDir dir(sessionsDir);
    if (!dir.exists()) return {};

    for (const auto &fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        if (obj["pid"].toVariant().toLongLong() == pid) {
            return obj["cwd"].toString();
        }
    }
    return {};
}

void activateProcessWindow(qint64 pid)
{
    // Get the session's cwd for VSCode window matching
    QString cwd = getSessionCwd(pid);

    // Walk up the process tree to find VSCode or terminal
    qint64 current = pid;
    HWND targetHwnd = nullptr;
    qint64 terminalPid = 0;

    for (int i = 0; i < 10 && current != 0; ++i) {
        QString name = getProcessName(current);

        // Found VSCode - use path-aware window finding
        if (isVSCodeProcess(name)) {
            if (!cwd.isEmpty()) {
                targetHwnd = findVSCodeWindowForPath(cwd);
            } else {
                targetHwnd = findWindowByPid(current);
            }
            break;
        }

        // Found terminal process
        if (isTerminalProcess(name)) {
            terminalPid = current;
            // Try to find window for this terminal process
            targetHwnd = findWindowByPid(current);
            if (targetHwnd) break;

            // For cmd.exe/powershell.exe, window may be in conhost.exe
            // Walk up to find conhost.exe or terminal emulator
            qint64 parent = getParentPid(current);
            for (int j = 0; j < 5 && parent != 0; ++j) {
                QString parentName = getProcessName(parent);
                if (parentName.compare("conhost.exe", Qt::CaseInsensitive) == 0) {
                    targetHwnd = findWindowByPid(parent);
                    if (targetHwnd) break;
                }
                if (isTerminalProcess(parentName) || parentName.compare("WindowsTerminal.exe", Qt::CaseInsensitive) == 0) {
                    targetHwnd = findWindowByPid(parent);
                    if (targetHwnd) break;
                }
                parent = getParentPid(parent);
            }
            if (targetHwnd) break;
        }

        current = getParentPid(current);
    }

    // If we found a terminal but no window, try walking up to find the terminal emulator
    if (!targetHwnd && terminalPid > 0) {
        current = getParentPid(terminalPid);
        for (int i = 0; i < 5 && current != 0; ++i) {
            QString name = getProcessName(current);
            // Check if this is a terminal emulator (Windows Terminal, ConEmu, etc.)
            if (name.compare("WindowsTerminal.exe", Qt::CaseInsensitive) == 0 ||
                name.compare("ConEmu64.exe", Qt::CaseInsensitive) == 0 ||
                name.compare("Tabby.exe", Qt::CaseInsensitive) == 0) {
                targetHwnd = findWindowByPid(current);
                if (targetHwnd) break;
            }
            current = getParentPid(current);
        }
    }

    // Fallback: try the pid itself
    if (!targetHwnd)
        targetHwnd = findWindowByPid(pid);

    // Fallback: try to find any VSCode window
    if (!targetHwnd && !cwd.isEmpty()) {
        targetHwnd = findVSCodeWindowForPath(cwd);
    }

    if (!targetHwnd) return;

    // Bring window to foreground
    // Windows restricts SetForegroundWindow - use the Alt key trick to bypass
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
        DWORD myThread = GetCurrentThreadId();
        AttachThreadInput(myThread, fgThread, TRUE);
    }

    // Restore minimized window first
    if (IsIconic(targetHwnd))
        ShowWindow(targetHwnd, SW_RESTORE);
    else
        ShowWindow(targetHwnd, SW_SHOW);

    // Simulate Alt key press to bypass SetForegroundWindow restriction
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);

    SetForegroundWindow(targetHwnd);

    if (fg) {
        DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
        DWORD myThread = GetCurrentThreadId();
        AttachThreadInput(myThread, fgThread, FALSE);
    }
}

#else

void activateProcessWindow(qint64) {}

#endif

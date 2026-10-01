#include "lilith_timer.h"

/* 全局变量定义 */
HINSTANCE g_hInst = NULL;
HWND      g_hwndMsg = NULL;
HWND      g_hwndTimer = NULL;
AppConfig g_cfg;
WCHAR     g_iniPath[MAX_PATH] = L"";
BOOL      g_bEditMode = FALSE;
BOOL      g_bExiting = FALSE;
BOOL      g_bDesktopPlain = FALSE;

#define MSG_WND_CLASS     L"LilithTimer.MsgWnd"
#define SINGLE_INSTANCE_MUTEX L"Local\\LilithTimer.SingleInstance"

/* 外部函数声明（tray.c 中定义） */
extern UINT GetTaskbarCreatedMsg(void);
extern void SetTaskbarCreatedMsg(UINT msg);

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == GetTaskbarCreatedMsg() && GetTaskbarCreatedMsg() != 0) {
        TrayAdd();
        if (!g_bEditMode && g_cfg.mode == MODE_DESKTOP &&
            (!g_hwndTimer || !IsWindow(g_hwndTimer)))
            RecreateTimerWindow();
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wParam == IDT_COUNTDOWN)  RefreshContent(FALSE);
        else if (wParam == IDT_WATCHDOG) {
            /* 看门狗：检查 Desktop 模式窗口是否存活 */
            if (!g_bExiting && !g_bEditMode && g_cfg.mode == MODE_DESKTOP) {
                if (!g_hwndTimer || !IsWindow(g_hwndTimer))
                    RecreateTimerWindow();
            }
        }
        return 0;

    case WM_APP_TRAYICON:
        /* 左键或右键都弹出菜单；NIN_SELECT/NIN_POPUPMENU 覆盖触摸/键盘触发 */
        {
            UINT trayMsg = LOWORD(lParam);
            if (trayMsg == WM_LBUTTONUP || trayMsg == WM_RBUTTONUP ||
                trayMsg == WM_CONTEXTMENU || trayMsg == NIN_SELECT ||
                trayMsg == NIN_POPUPMENU) {
                DbgLog(L"托盘事件: 0x%04X -> 弹出菜单", trayMsg);
                TrayShowMenu();
            } else if (trayMsg == WM_LBUTTONDOWN || trayMsg == WM_RBUTTONDOWN ||
                       trayMsg == WM_LBUTTONDBLCLK || trayMsg == WM_RBUTTONDBLCLK) {
                DbgLog(L"托盘事件: 0x%04X", trayMsg);
            }
        }
        return 0;

    case WM_HOTKEY:
        /* 托盘不可用时的兜底入口（IoT/kiosk 系统可能隐藏通知区域） */
        if (wParam == IDH_TRAY_MENU) {
            DbgLog(L"全局热键触发弹出菜单");
            TrayShowMenu();
        }
        return 0;

    case WM_DISPLAYCHANGE:
        RefreshContent(TRUE);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) {
            g_bExiting = TRUE;
            KillTimer(hwnd, IDT_COUNTDOWN);
            KillTimer(hwnd, IDT_WATCHDOG);
            SaveWindowPlacement();
            DestroyTimerWindow();
            TrayRemove();
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        g_bExiting = TRUE;
        KillTimer(hwnd, IDT_COUNTDOWN);
        KillTimer(hwnd, IDT_WATCHDOG);
        UnregisterHotKey(hwnd, IDH_TRAY_MENU);
        SaveWindowPlacement();
        DestroyTimerWindow();
        TrayRemove();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    HANDLE mutex;
    WNDCLASSEXW wc;
    MSG msg;
    RECT probe;

    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    /* 单实例 */
    mutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    g_hInst = hInstance;
    SetTaskbarCreatedMsg(RegisterWindowMessageW(L"TaskbarCreated"));

    GetIniPath();
    if (!FileExists(g_iniPath)) CreateDefaultConfig();
    LoadConfig();

    /* 诊断日志：记录系统版本与初始配置，便于排查贴桌面模式问题 */
    {
        typedef LONG (WINAPI* PFNRTLGETVERSION)(PRTL_OSVERSIONINFOW);
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        PFNRTLGETVERSION pRtlGetVersion = hNtdll ?
            (PFNRTLGETVERSION)GetProcAddress(hNtdll, "RtlGetVersion") : NULL;
        RTL_OSVERSIONINFOW osvi;
        ZeroMemory(&osvi, sizeof(osvi));
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        if (pRtlGetVersion) pRtlGetVersion(&osvi);
        DbgLog(L"启动 | OS=%lu.%lu.%lu | ini=%s | Mode=%d | pos=(%d,%d) size=(%d,%d)",
               osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber,
               g_iniPath, (int)g_cfg.mode, g_cfg.x, g_cfg.y, g_cfg.w, g_cfg.h);
    }

    /* 位置有效性检查 */
    SetRect(&probe, g_cfg.x, g_cfg.y, g_cfg.x + 32, g_cfg.y + 32);
    if (MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) == NULL) {
        g_cfg.x = 200;
        g_cfg.y = 200;
    }

    /* 注册窗口类 */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    /* 编辑模式拉伸窗口时必须整个客户区重绘，
       否则只有新露出的细条区域被重绘，边框/文字会留下"拖影"残线 */
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TimerWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"LilithTimer.TimerWnd";
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassExW(&wc)) goto fail;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = MSG_WND_CLASS;
    if (!RegisterClassExW(&wc)) goto fail;

    /* 创建隐藏消息窗口 */
    g_hwndMsg = CreateWindowExW(WS_EX_TOOLWINDOW, MSG_WND_CLASS, APP_NAME,
                                WS_OVERLAPPED, 0, 0, 0, 0,
                                NULL, NULL, hInstance, NULL);
    if (!g_hwndMsg) goto fail;

    TrayAdd();
    /* 全局热键兜底：托盘图标不可用时（通知区域被隐藏的系统）仍能弹出菜单 */
    if (!RegisterHotKey(g_hwndMsg, IDH_TRAY_MENU, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L'))
        DbgLog(L"全局热键注册失败: err=%lu", (unsigned long)GetLastError());
    else
        DbgLog(L"全局热键已注册: Ctrl+Alt+L 弹出菜单");
    CreateTimerWindow();

    SetTimer(g_hwndMsg, IDT_COUNTDOWN, 500, NULL);
    SetTimer(g_hwndMsg, IDT_WATCHDOG, 2000, NULL);

    /* 消息循环 */
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;

fail:
    if (mutex) CloseHandle(mutex);
    return 1;
}

#include "lilith_timer.h"

static UINT g_wmTaskbarCreated = 0;

void TrayAdd(void)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndMsg;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    nid.hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                  IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON),
                                  LR_DEFAULTCOLOR);
    if (!nid.hIcon) nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    StringCchCopyW(nid.szTip, COUNT_OF(nid.szTip), APP_NAME);
    if (!Shell_NotifyIconW(NIM_ADD, &nid))
        DbgLog(L"托盘图标添加失败: err=%lu", (unsigned long)GetLastError());
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &nid))
        DbgLog(L"托盘图标版本设置失败: err=%lu", (unsigned long)GetLastError());
    TrayLogStatus();
}

/* 诊断：托盘图标是否真的被系统通知区域托管。
   某些 IoT/kiosk 系统通过策略隐藏通知区域，图标虽添加成功但不可见、不可点击 */
void TrayLogStatus(void)
{
    NOTIFYICONIDENTIFIER nii;
    RECT rc;
    HRESULT hr;
    ZeroMemory(&nii, sizeof(nii));
    nii.cbSize = sizeof(nii);
    nii.hWnd = g_hwndMsg;
    nii.uID = 1;
    hr = Shell_NotifyIconGetRect(&nii, &rc);
    if (SUCCEEDED(hr))
        DbgLog(L"托盘图标位于通知区域: (%ld,%ld)-(%ld,%ld)",
               (long)rc.left, (long)rc.top, (long)rc.right, (long)rc.bottom);
    else
        DbgLog(L"托盘图标未出现在通知区域（可能被系统策略隐藏，可用热键 Ctrl+Alt+L 弹出菜单）: hr=0x%08lX",
               (long)hr);
}

void TrayRemove(void)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndMsg;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayShowMenu(void)
{
    HMENU menu;
    POINT pt;
    UINT cmd;

    menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"关闭");
    AppendMenuW(menu, MF_STRING | (g_bEditMode ? MF_CHECKED : 0), CMD_EDIT, L"编辑");
    AppendMenuW(menu, MF_STRING, CMD_SETTINGS, L"设置");
    AppendMenuW(menu, MF_STRING, CMD_ABOUT, L"关于");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwndMsg);
    DbgLog(L"弹出托盘菜单 @(%ld,%ld)", (long)pt.x, (long)pt.y);
    cmd = TrackPopupMenu(menu,
                         TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                         pt.x, pt.y, 0, g_hwndMsg, NULL);
    DbgLog(L"托盘菜单关闭: cmd=%u err=%lu", cmd, (unsigned long)GetLastError());
    PostMessageW(g_hwndMsg, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case CMD_EXIT:
        PostMessageW(g_hwndMsg, WM_CLOSE, 0, 0);
        break;
    case CMD_EDIT:
        if (g_bEditMode) {
            g_bEditMode = FALSE;
            SaveWindowPlacement();
            RecreateTimerWindow();
        } else {
            g_bEditMode = TRUE;
            RecreateTimerWindow();
        }
        break;
    case CMD_SETTINGS:
        ShowSettingsDialog(g_hwndMsg);
        break;
    case CMD_ABOUT:
        MessageBoxW(g_hwndMsg, ABOUT_TEXT, L"关于 " APP_NAME,
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        break;
    default:
        break;
    }
}

/* 供 main.c 调用 */
UINT GetTaskbarCreatedMsg(void) { return g_wmTaskbarCreated; }
void SetTaskbarCreatedMsg(UINT msg) { g_wmTaskbarCreated = msg; }

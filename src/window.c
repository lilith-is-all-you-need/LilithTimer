#include "lilith_timer.h"

/* 经典形态：含图标层(SHELLDLL_DefView)的顶层窗口，其下一个顶层窗口是壁纸层 WorkerW */
static BOOL CALLBACK FindWorkerWProc(HWND hwnd, LPARAM lParam)
{
    HWND* result = (HWND*)lParam;
    if (*result) return FALSE;
    if (FindWindowExW(hwnd, NULL, L"SHELLDLL_DefView", NULL) != NULL) {
        HWND next = GetWindow(hwnd, GW_HWNDNEXT);
        WCHAR cls[32] = L"";
        if (next) GetClassNameW(next, cls, COUNT_OF(cls));
        if (next && wcscmp(cls, L"WorkerW") == 0)
            *result = next;
    }
    return TRUE;
}

/*
 * 查找桌面壁纸层宿主窗口（Desktop 模式挂载目标）：
 *  - Win11 需要带 (0xD,1) 参数的 0x052C 消息才能分裂出 WorkerW，老系统用 (0,0)，
 *    两种参数都发一遍以兼容各版本；
 *  - Win11 下 WorkerW 常作为 Progman 的【子窗口】出现，老系统多为顶层窗口，两种都找；
 *  - 壁纸层 WorkerW 的特征：类名 WorkerW 且【不含】SHELLDLL_DefView 图标层；
 *  - 都找不到时退回 Progman 本体（创建后再把本窗口显式压到图标层之下）。
 */
/* 记录宿主窗口关键信息到诊断日志 */
static void LogHostInfo(const WCHAR* tag, HWND host)
{
    WCHAR cls[64] = L"?";
    RECT rc = {0, 0, 0, 0};
    LONG exstyle = 0;
    if (host) {
        GetClassNameW(host, cls, COUNT_OF(cls));
        GetWindowRect(host, &rc);
        exstyle = GetWindowLongW(host, GWL_EXSTYLE);
    }
    DbgLog(L"贴桌面宿主[%s]: hwnd=%p class=%s rect=(%ld,%ld)-(%ld,%ld) exstyle=0x%08lX%s",
           tag, (void*)host, cls, (long)rc.left, (long)rc.top,
           (long)rc.right, (long)rc.bottom, (unsigned long)exstyle,
           (exstyle & WS_EX_NOREDIRECTIONBITMAP) ? L" [RaisedDesktop]" : L"");
}

static HWND FindDesktopHost(void)
{
    HWND progman = FindWindowW(L"Progman", NULL);
    HWND host;

    if (!progman) {
        DbgLog(L"贴桌面宿主：找不到 Progman!");
        return NULL;
    }

    SendMessageTimeoutW(progman, 0x052C, 0xD, 1, SMTO_NORMAL, 200, NULL);
    SendMessageTimeoutW(progman, 0x052C, 0,   0, SMTO_NORMAL, 200, NULL);

    /* Win11 常见形态：WorkerW 是 Progman 的子窗口 */
    host = FindWindowExW(progman, NULL, L"WorkerW", NULL);
    while (host) {
        if (FindWindowExW(host, NULL, L"SHELLDLL_DefView", NULL) == NULL) {
            LogHostInfo(L"Progman的子WorkerW", host);
            return host;   /* 不含图标层的那个才是壁纸层 */
        }
        host = FindWindowExW(progman, host, L"WorkerW", NULL);
    }

    /* 经典形态：WorkerW 是顶层窗口 */
    host = NULL;
    EnumWindows(FindWorkerWProc, (LPARAM)&host);
    if (host && IsWindow(host)) {
        LogHostInfo(L"顶层WorkerW", host);
        return host;
    }

    /* 兜底：Progman 本体 */
    LogHostInfo(L"兜底Progman", progman);
    return progman;
}

void CreateTimerWindow(void)
{
    DWORD style, exstyle;
    HWND parent = NULL;
    WCHAR cd[128];
    HDC hdc;
    HFONT f1, f2;
    int W, H;

    if (g_hwndTimer) return;

    if (g_bEditMode) {
        style   = WS_POPUP | WS_THICKFRAME;
        exstyle = WS_EX_TOOLWINDOW;
        parent  = NULL;
        
        if (g_cfg.w > 0 && g_cfg.h > 0) {
            W = g_cfg.w;
            H = g_cfg.h;
        } else {
            BuildCountdownText(cd, COUNT_OF(cd));
            hdc = GetDC(NULL);
            f1 = CreateAppFont(g_cfg.fontSize);
            f2 = CreateAppFont(g_cfg.cdFontSize);
            {
                int w1, h1, w2, h2;
                RECT r = {0,0,0,0};
                HGDIOBJ old;
                old = SelectObject(hdc, f1);
                if (g_cfg.text[0] != L'\0')
                    DrawTextW(hdc, g_cfg.text, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                w1 = r.right; h1 = r.bottom;
                SelectObject(hdc, f2);
                SetRect(&r, 0, 0, 0, 0);
                DrawTextW(hdc, cd, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                w2 = r.right; h2 = r.bottom;
                SelectObject(hdc, old);
                W = (w1 > w2 ? w1 : w2) + g_cfg.padding * 2 + 40;
                H = h1 + g_cfg.lineSpacing + h2 + g_cfg.padding * 2 + 40;
            }
            DeleteObject(f1); DeleteObject(f2);
            ReleaseDC(NULL, hdc);
            W = ClampInt(W, 120, 2000);
            H = ClampInt(H, 80, 1000);
        }
    } else {
        switch (g_cfg.mode) {
        case MODE_DESKTOP:
            parent = FindDesktopHost();
            /* 一律先创建为本进程顶层窗口，成功后再 SetParent 挂到桌面宿主。
               直接以跨进程窗口(explorer 的 WorkerW/Progman)为父创建子窗口
               在部分 Win10/11 上会失败（CreateWindowExW 返回 NULL 且错误码为 0） */
            style   = WS_POPUP;
            exstyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
            /* g_bDesktopPlain=FALSE 时先尝试分层渲染（透明效果好）；
               UpdateLayeredWindow 在子窗口上失败的系统会降级为普通不透明窗口 */
            if (!g_bDesktopPlain)
                exstyle |= WS_EX_LAYERED;
            break;
        case MODE_PASSTHROUGH:
            style   = WS_POPUP;
            exstyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                      WS_EX_TRANSPARENT | WS_EX_TOPMOST;
            break;
        default:
            style   = WS_POPUP;
            exstyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            break;
        }
        
        BuildCountdownText(cd, COUNT_OF(cd));
        hdc = GetDC(NULL);
        f1 = CreateAppFont(g_cfg.fontSize);
        f2 = CreateAppFont(g_cfg.cdFontSize);
        {
            int w1, h1, w2, h2;
            RECT r = {0,0,0,0};
            HGDIOBJ old;
            old = SelectObject(hdc, f1);
            if (g_cfg.text[0] != L'\0')
                DrawTextW(hdc, g_cfg.text, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            w1 = r.right; h1 = r.bottom;
            SelectObject(hdc, f2);
            SetRect(&r, 0, 0, 0, 0);
            DrawTextW(hdc, cd, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            w2 = r.right; h2 = r.bottom;
            SelectObject(hdc, old);
            
            if (g_cfg.w > 0 && g_cfg.h > 0) {
                W = g_cfg.w;
                H = g_cfg.h;
            } else {
                W = (w1 > w2 ? w1 : w2) + g_cfg.padding * 2;
                H = h1 + g_cfg.lineSpacing + h2 + g_cfg.padding * 2;
            }
            W = ClampInt(W, 16, 8192);
            H = ClampInt(H, 16, 8192);
        }
        DeleteObject(f1); DeleteObject(f2);
        ReleaseDC(NULL, hdc);
    }

    /* 所有模式统一以顶层窗口创建（贴桌面模式随后再 SetParent 挂到桌面宿主），
       避免跨进程父窗口直接创建失败 */
    g_hwndTimer = CreateWindowExW(exstyle, L"LilithTimer.TimerWnd", APP_NAME, style,
                                  g_cfg.x, g_cfg.y, W, H, NULL, NULL, g_hInst, NULL);
    if (g_cfg.mode == MODE_DESKTOP && !g_bEditMode) {
        DbgLog(L"贴桌面窗口创建(顶层): %s | 屏幕(%d,%d) | size=(%d,%d) | exstyle=0x%08lX | err=%lu",
               g_hwndTimer ? L"成功" : L"失败",
               g_cfg.x, g_cfg.y, W, H,
               (unsigned long)exstyle,
               g_hwndTimer ? 0UL : (unsigned long)GetLastError());
    }
    if (!g_hwndTimer) return;

    if (!g_bEditMode && g_cfg.mode == MODE_PASSTHROUGH)
        SetWindowPos(g_hwndTimer, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else if (!g_bEditMode && g_cfg.mode == MODE_DESKTOP) {
      if (parent) {
        /* 跨进程挂载到桌面宿主：先改成子窗口样式再 SetParent（壁纸引擎通用做法） */
        LONG_PTR st = GetWindowLongPtrW(g_hwndTimer, GWL_STYLE);
        SetWindowLongPtrW(g_hwndTimer, GWL_STYLE, (st & ~WS_POPUP) | WS_CHILD);
        if (SetParent(g_hwndTimer, parent)) {
            POINT pt2;
            HWND defView;
            pt2.x = g_cfg.x; pt2.y = g_cfg.y;
            ScreenToClient(parent, &pt2);   /* 子窗口改用宿主客户区坐标 */
            DbgLog(L"SetParent 挂载成功 | 宿主客户区坐标=(%ld,%ld)", (long)pt2.x, (long)pt2.y);
            SetWindowPos(g_hwndTimer, NULL, pt2.x, pt2.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            /* 挂到 Progman 时需压到图标层(DefView)之下；挂到壁纸 WorkerW 时置底即可 */
            defView = FindWindowExW(parent, NULL, L"SHELLDLL_DefView", NULL);
            if (defView)
                SetWindowPos(g_hwndTimer, defView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            else
                SetWindowPos(g_hwndTimer, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            DbgLog(L"SetParent 挂载失败: err=%lu，退化为普通底层窗口", (unsigned long)GetLastError());
            SetWindowPos(g_hwndTimer, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
      } else {
        /* 连 Progman 都找不到：保持顶层窗口并置底（等价于降级显示） */
        SetWindowPos(g_hwndTimer, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
    }

    ShowWindow(g_hwndTimer, SW_SHOWNOACTIVATE);

    if (!g_bEditMode) {
        /* 贴桌面模式降级逻辑：UpdateLayeredWindow 在部分系统的子窗口上会失败，
           失败后自动重建为普通不透明子窗口，保证贴桌面模式至少可见 */
        if (g_cfg.mode == MODE_DESKTOP && g_bDesktopPlain) {
            InvalidateRect(g_hwndTimer, NULL, TRUE);   /* 普通子窗口走 WM_PAINT */
        } else {
            int rr = RenderLayered();
            if (rr == RENDER_ULW_FAILED && g_cfg.mode == MODE_DESKTOP && !g_bDesktopPlain) {
                DbgLog(L"分层渲染在桌面子窗口上失败，降级为普通不透明子窗口");
                g_bDesktopPlain = TRUE;
                RecreateTimerWindow();
                return;
            }
        }
    } else {
        InvalidateRect(g_hwndTimer, NULL, TRUE);
    }
}

/* 区分“主动重建”与“被宿主连带销毁”，供诊断日志判断 */
static BOOL s_bRecreating = FALSE;

void DestroyTimerWindow(void)
{
    if (g_hwndTimer && IsWindow(g_hwndTimer)) {
        s_bRecreating = TRUE;
        DestroyWindow(g_hwndTimer);
        s_bRecreating = FALSE;
    }
    g_hwndTimer = NULL;
}

void RecreateTimerWindow(void)
{
    DestroyTimerWindow();
    CreateTimerWindow();
}

void RefreshContent(BOOL force)
{
    static WCHAR lastCountdown[128] = L"";
    WCHAR cd[128];

    /* 窗口不在（重建中途/被 Explorer 销毁）就直接返回，
     否则 InvalidateRect(NULL, ...) 会把整个桌面标脏，引起闪烁 */
    if (!g_hwndTimer || !IsWindow(g_hwndTimer)) return;

    BuildCountdownText(cd, COUNT_OF(cd));
    if (!force && wcscmp(cd, lastCountdown) == 0) return;
    StringCchCopyW(lastCountdown, COUNT_OF(lastCountdown), cd);
    if (!g_bEditMode && !(g_cfg.mode == MODE_DESKTOP && g_bDesktopPlain))
        RenderLayered();
    else
        InvalidateRect(g_hwndTimer, NULL, TRUE);
}

LRESULT CALLBACK TimerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        /* 编辑模式：绘制简洁边框 */
        if (g_bEditMode) {
            /* 背景 */
            HBRUSH brush = CreateSolidBrush(RGB(
                GetRValue(g_cfg.backColor) + 20,
                GetGValue(g_cfg.backColor) + 20,
                GetBValue(g_cfg.backColor) + 20));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            
            /* 边框 - 简单实线 */
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 100, 100));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 4, 4, rc.right - 4, rc.bottom - 4);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            
            /* 四角小方块 */
            HBRUSH hbrush = CreateSolidBrush(RGB(255, 100, 100));
            int sz = 6;
            RECT r1 = {4, 4, 4+sz, 4+sz};
            RECT r2 = {rc.right-4-sz, 4, rc.right-4, 4+sz};
            RECT r3 = {4, rc.bottom-4-sz, 4+sz, rc.bottom-4};
            RECT r4 = {rc.right-4-sz, rc.bottom-4-sz, rc.right-4, rc.bottom-4};
            FillRect(hdc, &r1, hbrush);
            FillRect(hdc, &r2, hbrush);
            FillRect(hdc, &r3, hbrush);
            FillRect(hdc, &r4, hbrush);
            DeleteObject(hbrush);
        } else {
            /* 非编辑模式：透明背景，由 RenderLayered 处理 */
            HBRUSH brush = CreateSolidBrush(g_cfg.backColor);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
        }
        
        /* 绘制文字 */
        WCHAR cd[128];
        HFONT f1 = CreateAppFont(g_cfg.fontSize);
        HFONT f2 = CreateAppFont(g_cfg.cdFontSize);
        BuildCountdownText(cd, COUNT_OF(cd));
        
        SetBkMode(hdc, TRANSPARENT);
        
        int w1, h1, w2, h2;
        RECT r = {0,0,0,0};
        HGDIOBJ old = SelectObject(hdc, f1);
        if (g_cfg.text[0] != L'\0')
            DrawTextW(hdc, g_cfg.text, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        w1 = r.right; h1 = r.bottom;
        
        SelectObject(hdc, f2);
        SetRect(&r, 0, 0, 0, 0);
        DrawTextW(hdc, cd, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        w2 = r.right; h2 = r.bottom;
        SelectObject(hdc, old);
        
        int contentH = h1 + g_cfg.lineSpacing + h2;
        int startY = (rc.bottom - contentH) / 2;
        if (startY < 0) startY = 0;
        
        old = SelectObject(hdc, f1);
        SetTextColor(hdc, g_cfg.textColor);
        if (h1 > 0) {
            RECT r1 = { 0, startY, rc.right, startY + h1 };
            DrawTextW(hdc, g_cfg.text, -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        
        SelectObject(hdc, f2);
        SetTextColor(hdc, g_cfg.cdColor);
        RECT r2 = { 0, startY + h1 + g_cfg.lineSpacing, rc.right, startY + h1 + g_cfg.lineSpacing + h2 };
        DrawTextW(hdc, cd, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        
        SelectObject(hdc, old);
        DeleteObject(f1);
        DeleteObject(f2);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    
    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        if (g_bEditMode || g_cfg.mode == MODE_FLOAT) {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT) hit = HTCAPTION;
            return hit;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_MOVE:
        if (g_hwndTimer == hwnd) {
            RECT rc;
            if (GetWindowRect(hwnd, &rc)) {
                g_cfg.x = rc.left;
                g_cfg.y = rc.top;
            }
        }
        return 0;

    case WM_EXITSIZEMOVE:
        SaveWindowPlacement();
        return 0;

    case WM_DISPLAYCHANGE:
        RefreshContent(TRUE);
        return 0;

    case WM_DESTROY:
        if (hwnd == g_hwndTimer) {
            if (!s_bRecreating && !g_bExiting && g_cfg.mode == MODE_DESKTOP)
                DbgLog(L"贴桌面窗口被意外销毁（桌面宿主可能被 Explorer 重建），看门狗将重新挂载");
            g_hwndTimer = NULL;
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

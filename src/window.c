#include "lilith_timer.h"

/* 判断一个窗口是否为“壁纸层” WorkerW：
   类名必须是 WorkerW，且不含 SHELLDLL_DefView（图标层）子窗口。
   这是识别壁纸层的核心特征，Wallpaper Engine / Fences 环境下也成立。 */
static BOOL IsWallpaperWorkerW(HWND hwnd)
{
    WCHAR cls[32];
    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    GetClassNameW(hwnd, cls, COUNT_OF(cls));
    if (wcscmp(cls, L"WorkerW") != 0) return FALSE;
    if (FindWindowExW(hwnd, NULL, L"SHELLDLL_DefView", NULL) != NULL) return FALSE;
    return TRUE;
}

/* 回调：递归枚举 Progman 子孙，找到第一个壁纸层 WorkerW */
static BOOL CALLBACK EnumWallpaperWwProc(HWND hwnd, LPARAM lParam)
{
    HWND* result = (HWND*)lParam;
    if (*result) return FALSE;
    if (IsWallpaperWorkerW(hwnd)) { *result = hwnd; return FALSE; }
    return TRUE;
}

/* 回调：枚举顶层窗口，找“含 DefView 的窗口的下一个兄弟 WorkerW” */
static BOOL CALLBACK EnumTopNextWwProc(HWND hwnd, LPARAM lParam)
{
    HWND* result = (HWND*)lParam;
    if (*result) return FALSE;
    if (FindWindowExW(hwnd, NULL, L"SHELLDLL_DefView", NULL) != NULL) {
        HWND next = GetWindow(hwnd, GW_HWNDNEXT);
        if (IsWallpaperWorkerW(next)) { *result = next; return FALSE; }
    }
    return TRUE;
}

/* 回调：枚举所有顶层 WorkerW，返回第一个不含 DefView 的（兜底） */
static BOOL CALLBACK EnumAnyWallpaperWwProc(HWND hwnd, LPARAM lParam)
{
    HWND* result = (HWND*)lParam;
    if (*result) return FALSE;
    if (IsWallpaperWorkerW(hwnd)) { *result = hwnd; return FALSE; }
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
    HWND host = NULL;
    int attempt;

    if (!progman) {
        DbgLog(L"贴桌面宿主：找不到 Progman!");
        return NULL;
    }

    /* 触发 WorkerW 分裂：Win11 需要 (0xD,1)，老系统用 (0,0)，两个都发；
       超时从 200ms 提高到 1000ms，复杂环境下消息处理可能更慢 */
    SendMessageTimeoutW(progman, 0x052C, 0xD, 1, SMTO_NORMAL, 1000, NULL);
    SendMessageTimeoutW(progman, 0x052C, 0x0, 0, SMTO_NORMAL, 1000, NULL);

    /* 关键：给 Explorer / Wallpaper Engine / Fences 时间完成窗口树的创建和重排，
       没有这个等待，紧接着的查找往往看到的是「半成品」的窗口层级 */
    Sleep(500);

    /* 策略 1：Progman 子孙中递归找不含 DefView 的 WorkerW
       （Win11 24H2+ 和 Wallpaper Engine 环境下，壁纸层常常是 Progman 的后代） */
    host = NULL;
    EnumChildWindows(progman, EnumWallpaperWwProc, (LPARAM)&host);
    if (host) { LogHostInfo(L"Progman子孙WorkerW", host); return host; }

    /* 策略 2：经典顶层形态——含 DefView 的窗口的下一个兄弟 WorkerW，
       重试 3 次，覆盖 Explorer 异步重建窗口的情况 */
    for (attempt = 0; attempt < 3 && !host; attempt++) {
        EnumWindows(EnumTopNextWwProc, (LPARAM)&host);
        if (!host) Sleep(300);
    }
    if (host) { LogHostInfo(L"顶层WorkerW(DefView兄弟)", host); return host; }

    /* 策略 3：兜底——任意不含 DefView 的顶层 WorkerW */
    EnumWindows(EnumAnyWallpaperWwProc, (LPARAM)&host);
    if (host) { LogHostInfo(L"顶层WorkerW(兜底)", host); return host; }

    /* 最后兜底：Progman 本体（后面挂载时会被压到 DefView 之下） */
    LogHostInfo(L"兜底Progman", progman);
    return progman;
}

/* 把计时器窗口挂到桌面宿主下，成功返回 TRUE。
   包含样式转换、SetParent、坐标换算、Z-order 调整、挂载验证。 */
static BOOL TryMountToDesktop(HWND hwnd, HWND host)
{
    LONG_PTR st;
    POINT pt;
    HWND defView;

    if (!hwnd || !host || !IsWindow(hwnd) || !IsWindow(host)) return FALSE;

    /* 样式彻底转为子窗口：清掉一切顶层样式，加上 WS_CHILD。
       原来的做法只清 WS_POPUP 不够，残留的 WS_THICKFRAME/WS_CAPTION
       可能导致 SetParent 后窗口表现异常 */
    st = GetWindowLongPtrW(hwnd, GWL_STYLE);
    st &= ~(LONG_PTR)(WS_POPUP | WS_CAPTION | WS_THICKFRAME |
        WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    st |= WS_CHILD;
    SetWindowLongPtrW(hwnd, GWL_STYLE, st);

    if (!SetParent(hwnd, host)) {
        DbgLog(L"SetParent 失败: err=%lu", (unsigned long)GetLastError());
        return FALSE;
    }

    /* 屏幕坐标 -> 宿主客户区坐标 */
    pt.x = g_cfg.x;
    pt.y = g_cfg.y;
    ScreenToClient(host, &pt);

    SetWindowPos(hwnd, NULL, pt.x, pt.y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    /* Z-order：
       - 宿主含 DefView（Progman / 图标层 WorkerW）→ 压到 DefView 之下
       - 宿主是壁纸层 WorkerW                     → 直接置底 */
    defView = FindWindowExW(host, NULL, L"SHELLDLL_DefView", NULL);
    if (defView)
        SetWindowPos(hwnd, defView, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* 挂载验证：GetParent 必须等于 host，否则说明 SetParent 表面成功实际失效 */
    if (GetParent(hwnd) != host) {
        DbgLog(L"挂载验证失败：GetParent=%p，期望=%p",
            (void*)GetParent(hwnd), (void*)host);
        return FALSE;
    }
    DbgLog(L"挂载成功 | 宿主客户区坐标=(%ld,%ld)", (long)pt.x, (long)pt.y);
    return TRUE;
}

/*===========================================================================
 * [兼容]贴桌面模式：顶层透明窗口 + Z-Order 动态守护（路线一）
 *
 * 不做 SetParent / WS_CHILD，而是维持一个独立的顶层分层窗口：
 *   - 点击穿透：WS_EX_TRANSPARENT + WM_NCHITTEST 返回 HTTRANSPARENT；
 *   - 永不抢焦点、不出现在任务栏：WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW；
 *   - 层级：SetWindowPos(HWND_BOTTOM, ...) 压在“壁纸之上、图标/普通窗口之下”。
 *
 * 守护策略：只监听 EVENT_SYSTEM_FOREGROUND（前台切换，频率低、有意义），
 * 再加上看门狗定时器兜底；同时做节流 + “已就位则跳过”检测，避免和
 * Wallpaper Engine / Fences 的窗口重排互相纠缠，造成一劲的闪烁。
 *===========================================================================*/

static HWINEVENTHOOK s_hookForeground = NULL;
static volatile LONG  s_guardPending = 0;
static DWORD          s_lastGuardTick = 0;   /* 上次压底时刻（节流） */

/* 判断一个窗口是否为桌面层（Progman 或壁纸 WorkerW），用于“已就位”检测 */
static BOOL IsDesktopLayerWindow(HWND hwnd)
{
    WCHAR cls[32];
    if (!hwnd || !IsWindow(hwnd)) return FALSE;
    GetClassNameW(hwnd, cls, COUNT_OF(cls));
    if (wcscmp(cls, L"Progman") == 0) return TRUE;
    return IsWallpaperWorkerW(hwnd);   /* WorkerW 且不含 SHELLDLL_DefView */
}

/* 把兼容贴桌面窗口固定到正确的层级 */
void CompatZOrderGuard(void)
{
    HWND fence, above, below;
    DWORD now;

    if (g_bEditMode || g_bExiting) return;
    if (g_cfg.mode != MODE_DESKTOP_COMPAT) return;
    if (!g_hwndTimer || !IsWindow(g_hwndTimer)) return;

    /* 节流：至少间隔 1000ms 才允许再压一次，打断与其他贴底应用的拉锯 */
    now = GetTickCount();
    if (now - s_lastGuardTick < 1000) return;

    fence = FindWindowW(L"FenceClass", NULL);
    if (fence && IsWindowVisible(fence)) {
        /* 已经在 Fences 容器正后方就无需再动 */
        above = GetWindow(g_hwndTimer, GW_HWNDPREV);
        if (above == fence) return;
        s_lastGuardTick = now;
        SetWindowPos(g_hwndTimer, fence, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        /* 已经在最底层（下方无窗口，或下方只剩壁纸层）就无需再动 */
        below = GetWindow(g_hwndTimer, GW_HWNDNEXT);
        if (below == NULL || IsDesktopLayerWindow(below)) return;
        s_lastGuardTick = now;
        SetWindowPos(g_hwndTimer, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

/* WinEvent 回调：只投递一条消息，合并突发事件，避免事件风暴灌满消息队列 */
static void CALLBACK CompatWinEventProc(HWINEVENTHOOK hook, DWORD event,
    HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD time)
{
    (void)hook; (void)event; (void)hwnd; (void)idObject; (void)idChild;
    (void)idEventThread; (void)time;

    if (!g_hwndMsg) return;
    if (InterlockedExchange(&s_guardPending, 1) == 0)
        PostMessageW(g_hwndMsg, WM_APP_COMPAT_GUARD, 0, 0);
}

void CompatGuardInstall(void)
{
    if (s_hookForeground) return;
    s_hookForeground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, CompatWinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    DbgLog(L"兼容贴桌面：WinEvent 钩子已挂接 (foreground=%p)",
        (void*)s_hookForeground);
}

void CompatGuardUninstall(void)
{
    if (s_hookForeground) { UnhookWinEvent(s_hookForeground); s_hookForeground = NULL; }
}

void CompatGuardPump(void)
{
    InterlockedExchange(&s_guardPending, 0);
    CompatZOrderGuard();
}

void CreateTimerWindow(void)
{
    DWORD style, exstyle;
    WCHAR cd[128];
    HDC hdc;
    HFONT f1, f2;
    int W, H;

    if (g_hwndTimer) return;

    if (g_bEditMode) {
        style   = WS_POPUP | WS_THICKFRAME;
        exstyle = WS_EX_TOOLWINDOW;
        
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
            /* [原生]贴桌面：一律先创建为本进程顶层窗口，成功后再 SetParent 挂到
               桌面宿主。直接以跨进程窗口(explorer 的 WorkerW/Progman)为父创建子窗口
               在部分 Win10/11 上会失败（CreateWindowExW 返回 NULL 且错误码为 0） */
            style   = WS_POPUP;
            exstyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
            /* g_bDesktopPlain=FALSE 时先尝试分层渲染（透明效果好）；
               UpdateLayeredWindow 在子窗口上失败的系统会降级为普通不透明窗口 */
            if (!g_bDesktopPlain)
                exstyle |= WS_EX_LAYERED;
            break;
        case MODE_DESKTOP_COMPAT:
            /* [兼容]贴桌面（路线一）：顶层分层窗口 + WS_EX_TRANSPARENT，不挂载、
               不置顶，靠 CompatZOrderGuard 动态守护固定在“壁纸之上、图标/普通窗口之下” */
            style = WS_POPUP;
            exstyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
            break;
        case MODE_PASSTHROUGH:
            /* 穿透：置顶 + 点击穿透（依赖 WS_EX_TRANSPARENT 扩展样式） */
            style = WS_POPUP;
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
    else if (!g_bEditMode && g_cfg.mode == MODE_DESKTOP_COMPAT)
        CompatZOrderGuard();   /* [兼容]贴桌面：首次直接压到底层 */
    else if (!g_bEditMode && g_cfg.mode == MODE_DESKTOP) {
        /* 挂载重试 3 次：Wallpaper Engine / Fences 会让 Explorer 异步
           重建窗口树，单次挂载失败很常见，重试一般能成功 */
        BOOL mounted = FALSE;
        int retry;
        for (retry = 0; retry < 3 && !mounted; retry++) {
            HWND h = FindDesktopHost();
            if (h && TryMountToDesktop(g_hwndTimer, h)) {
                mounted = TRUE;
                break;
            }
            DbgLog(L"贴桌面挂载第 %d 次失败，准备重试", retry + 1);
            Sleep(400);
        }
        if (!mounted) {
            DbgLog(L"贴桌面挂载全部失败，退化为普通底层窗口");
            SetWindowPos(g_hwndTimer, HWND_BOTTOM, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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

            /* 顶部提示条：告知用户如何退出编辑模式 */
            {
                int tipH = 22;
                RECT tipRc = { 2, 2, rc.right - 2, 2 + tipH };
                HBRUSH tipBg = CreateSolidBrush(RGB(255, 100, 100));
                HFONT tipFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH, L"微软雅黑");
                HGDIOBJ oldTipFont = SelectObject(hdc, tipFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                FillRect(hdc, &tipRc, tipBg);
                DrawTextW(hdc,
                    L"编辑模式 — 右键托盘图标 → 取消勾选「编辑」即可退出",
                    -1, &tipRc,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(hdc, oldTipFont);
                DeleteObject(tipFont);
                DeleteObject(tipBg);
            }

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
        int topReserve = g_bEditMode ? 26 : 0;   /* 编辑模式给提示条留出空间 */
        int startY = topReserve + ((rc.bottom - topReserve) - contentH) / 2;
        if (startY < topReserve) startY = topReserve;
        
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
        if (!g_bEditMode && g_cfg.mode == MODE_DESKTOP_COMPAT) {
            /* [兼容]贴桌面：告诉 Windows 忽略此窗口点击，将消息投递给下层窗口 */
            return HTTRANSPARENT;
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

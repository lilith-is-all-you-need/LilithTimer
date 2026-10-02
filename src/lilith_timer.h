#ifndef LILITH_TIMER_H
#define LILITH_TIMER_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <commctrl.h>  /* TBM_SETRANGE 等 */
#include <commdlg.h>   /* ChooseColor */

#pragma comment(lib, "comctl32.lib")

/* 编译期可定制 */
#ifndef APP_NAME
#define APP_NAME     L"LilithTimer"
#endif
#ifndef APP_VERSION
#define APP_VERSION  L"1.2.0"
#endif
#ifndef ABOUT_TEXT
#define ABOUT_TEXT L"LilithTimer v" APP_VERSION L"\r\n\r\n桌面倒计时小工具\r\n" L"Dev:LilithIsAllYouNeed\r\n" L"Tool:Kimi K3 Pro"
#endif

#define CONFIG_FILENAME   L"lilith_timer.ini"
#define IDI_APP_ICON      101
#define WM_APP_TRAYICON   101
#define IDT_COUNTDOWN     1
#define IDT_WATCHDOG      2

/* 兼容贴桌面：WinEvent 钩子检测到前台窗口切换后投递的守护消息 */
#define WM_APP_COMPAT_GUARD  (WM_APP + 1)

#define CMD_EXIT          1001
#define CMD_EDIT          1002
#define CMD_ABOUT         1004
#define CMD_SETTINGS      1005

/* 全局热键 ID：托盘不可用（如通知区域被隐藏的 IoT/kiosk 系统）时弹出菜单 */
#define IDH_TRAY_MENU     1

/* NOTIFYICON_VERSION_4 通知事件（部分旧 SDK 未定义时兜底） */
#ifndef NIN_SELECT
#define NIN_SELECT        (WM_USER + 0)
#define NINF_KEY          0x1
#define NIN_KEYSELECT     (NIN_SELECT | NINF_KEY)
#endif
#ifndef NIN_POPUPMENU
#define NIN_POPUPMENU     (WM_USER + 6)
#endif

#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))

typedef enum {
    MODE_DESKTOP = 0,        /* [原生]贴桌面：SetParent 挂载到 WorkerW/Progman */
    MODE_DESKTOP_COMPAT,     /* [兼容]贴桌面：顶层窗口 + Z-Order 动态守护（不挂载） */
    MODE_PASSTHROUGH,        /* 穿透：置顶 + 点击穿透 */
    MODE_FLOAT               /* 浮动 */
} DisplayMode;

typedef struct {
    WCHAR text[256];
    FILETIME targetFt;
    BOOL targetOk;
    DisplayMode mode;
    WCHAR fontName[LF_FACESIZE];
    int fontSize;
    int cdFontSize;
    COLORREF textColor;
    COLORREF cdColor;
    COLORREF backColor;
    int backOpacity;
    int padding;
    int lineSpacing;
    int x, y;
    int w, h;
    BOOL tzUseSystem;      /* TRUE = 跟随系统时区 */
    int  tzOffsetMinutes;  /* 自定义时区偏移，单位分钟，范围 [-720, 840] */
    BOOL autoStartEnabled; /* 开机自启总开关（默认 TRUE） */
    int  autoStartMode;    /* 1=每次开机启动；2=每日定时启动（计划任务） */
    int  autoStartMinutes; /* 每日定时：0..1439，自 00:00 起的分钟数 */
} AppConfig;

/* 全局变量 */
extern HINSTANCE g_hInst;
extern HWND g_hwndMsg;
extern HWND g_hwndTimer;
extern AppConfig g_cfg;
extern WCHAR g_iniPath[MAX_PATH];
extern BOOL g_bEditMode;
extern BOOL g_bExiting;
extern BOOL g_bDesktopPlain;   /* 贴桌面模式分层渲染失败时降级为普通不透明子窗口 */

/* RenderLayered 返回值 */
#define RENDER_OK          1   /* UpdateLayeredWindow 成功 */
#define RENDER_FAILED      0   /* 窗口无效或位图创建失败等 */
#define RENDER_ULW_FAILED  2   /* UpdateLayeredWindow 调用失败（子窗口不兼容等） */

/* config.c */
void GetIniPath(void);
BOOL FileExists(const WCHAR* path);
void CreateDefaultConfig(void);
void LoadConfig(void);
void SaveWindowPlacement(void);
void BuildCountdownText(WCHAR* buf, size_t cch);
BOOL ParseTargetTime(const WCHAR* src, FILETIME* outFt);
BOOL ParseTargetTimeAtTz(const WCHAR* src, BOOL useSystem, int tzMinutes, FILETIME* outFt);
void UtcToWallTime(const FILETIME* utc, BOOL useSystem, int tzMinutes, SYSTEMTIME* outWall);
BOOL WallTimeToUtc(const SYSTEMTIME* st, BOOL useSystem, int tzMinutes, FILETIME* outFt);
COLORREF ParseColor(const WCHAR* s, COLORREF def);
void DbgLog(const WCHAR* fmt, ...);

/* render.c */
int  RenderLayered(void);
HFONT CreateAppFont(int px);

/* window.c */
void CreateTimerWindow(void);
void DestroyTimerWindow(void);
void RecreateTimerWindow(void);
void RefreshContent(BOOL force);
LRESULT CALLBACK TimerWndProc(HWND, UINT, WPARAM, LPARAM);
void CompatZOrderGuard(void);    /* [兼容]贴桌面：把窗口压回壁纸之上、其他窗口之下 */
void CompatGuardInstall(void);   /* 挂接 WinEvent 钩子（EVENT_SYSTEM_FOREGROUND） */
void CompatGuardUninstall(void);
void CompatGuardPump(void);      /* 消息循环收到 WM_APP_COMPAT_GUARD 后调用 */

/* tray.c */
void TrayAdd(void);
void TrayRemove(void);
void TrayShowMenu(void);
void TrayLogStatus(void);   /* 诊断：托盘图标是否真的在通知区域中 */

/* settings.c */
void ShowSettingsDialog(HWND parent);

/* utils */
static __inline int ClampInt(int v, int lo, int hi) { return v<lo?lo:(v>hi?hi:v); }

#endif

#include "lilith_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/*
 * 诊断日志：在 exe 同目录写 lilith_timer_debug.log（UTF-16 LE，记事本可直接打开）。
 * 仅在关键路径记录（启动、模式切换、桌面宿主查找、窗口创建、分层渲染失败），
 * 正常情况下文件很小；排查完成后删除该文件即可。
 */
void DbgLog(const WCHAR* fmt, ...)
{
    WCHAR line[1024];
    WCHAR full[1200];
    WCHAR path[MAX_PATH];
    WCHAR* slash;
    va_list ap;
    HANDLE h;
    DWORD written;
    SYSTEMTIME st;

    /* 与 ini 同目录 */
    StringCchCopyW(path, COUNT_OF(path), g_iniPath);
    slash = wcsrchr(path, L'\\');
    if (slash) StringCchCopyW(slash + 1, (size_t)(COUNT_OF(path) - (slash + 1 - path)),
                              L"lilith_timer_debug.log");
    else       StringCchCopyW(path, COUNT_OF(path), L"lilith_timer_debug.log");

    va_start(ap, fmt);
    StringCchVPrintfW(line, COUNT_OF(line), fmt, ap);
    va_end(ap);

    GetLocalTime(&st);
    StringCchPrintfW(full, COUNT_OF(full),
                     L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\r\n",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);

    h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (GetFileSize(h, NULL) == 0) {
        static const BYTE bom[2] = { 0xFF, 0xFE };
        WriteFile(h, bom, sizeof(bom), &written, NULL);
    }
    WriteFile(h, full, (DWORD)(wcslen(full) * sizeof(WCHAR)), &written, NULL);
    CloseHandle(h);
}

void GetIniPath(void)
{
    WCHAR exePath[MAX_PATH];
    WCHAR* slash;
    GetModuleFileNameW(NULL, exePath, COUNT_OF(exePath));
    slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    else       exePath[0] = L'\0';
    StringCchCopyW(g_iniPath, COUNT_OF(g_iniPath), exePath);
    StringCchCatW(g_iniPath, COUNT_OF(g_iniPath), CONFIG_FILENAME);
}

BOOL FileExists(const WCHAR* path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void TrimSpaces(WCHAR* s);

COLORREF ParseColor(const WCHAR* s, COLORREF def)
{
    WCHAR* end = NULL;
    unsigned long v;
    if (!s || wcslen(s) != 6) return def;
    v = wcstoul(s, &end, 16);
    if (!end || *end != L'\0') return def;
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

BOOL ParseTargetTime(const WCHAR* src, FILETIME* outFt)
{
    WCHAR buf[64];
    WCHAR* p;
    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    SYSTEMTIME st;

    StringCchCopyW(buf, COUNT_OF(buf), src ? src : L"");
    for (p = buf; *p; p++) {
        if (*p == L'/') *p = L'-';
        else if (*p == L'T') *p = L' ';
    }
    TrimSpaces(buf);

    if (swscanf(buf, L"%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) < 6) {
        {
            int n1 = 0, n2 = 0, n3 = 0;
            int consumed = 0;
            int r = swscanf(buf, L"%d-%d-%d %d:%d:%d%n", &Y, &M, &D, &h, &m, &s, &n1);
            if (r >= 6) {
                consumed = n1;
            }
            else {
                h = m = s = 0;
                r = swscanf(buf, L"%d-%d-%d %d:%d%n", &Y, &M, &D, &h, &m, &n2);
                if (r >= 5) {
                    consumed = n2;
                }
                else {
                    h = m = s = 0;
                    r = swscanf(buf, L"%d-%d-%d%n", &Y, &M, &D, &n3);
                    if (r < 3) return FALSE;
                    consumed = n3;
                }
            }
            /* 检查尾部：只允许空白，防止 "2027-01-01 00:00:00abc" 被接受 */
            {
                const WCHAR* tail = buf + consumed;
                while (*tail == L' ' || *tail == L'\t' ||
                    *tail == L'\r' || *tail == L'\n') tail++;
                if (*tail != L'\0') return FALSE;
            }
        }
    }
    if (Y < 1970 || Y > 9999 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
        return FALSE;

    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)Y; st.wMonth = (WORD)M;  st.wDay = (WORD)D;
    st.wHour = (WORD)h; st.wMinute = (WORD)m; st.wSecond = (WORD)s;

    FILETIME ftLocal;
    if (!SystemTimeToFileTime(&st, &ftLocal))
        return FALSE;

    return LocalFileTimeToFileTime(&ftLocal, outFt);
}

static void TrimSpaces(WCHAR* s)
{
    WCHAR* p = s;
    WCHAR* end;
    while (*p == L' ' || *p == L'\t') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(WCHAR));
    end = s + wcslen(s);
    while (end > s && (end[-1] == L' ' || end[-1] == L'\t' ||
                       end[-1] == L'\r' || end[-1] == L'\n'))
        *--end = L'\0';
}

static DisplayMode ParseMode(const WCHAR* s, DisplayMode def)
{
    WCHAR buf[32];
    size_t i;
    StringCchCopyW(buf, COUNT_OF(buf), s ? s : L"");
    TrimSpaces(buf);
    for (i = 0; buf[i]; i++)
        if (buf[i] >= L'A' && buf[i] <= L'Z') buf[i] += L'a' - L'A';
    if (wcscmp(buf, L"desktop") == 0 || wcscmp(buf, L"1") == 0)     return MODE_DESKTOP;
    if (wcscmp(buf, L"passthrough") == 0 || wcscmp(buf, L"2") == 0) return MODE_PASSTHROUGH;
    if (wcscmp(buf, L"float") == 0 || wcscmp(buf, L"3") == 0)       return MODE_FLOAT;
    return def;
}

static const WCHAR CONFIG_TEMPLATE[] =
    L";============================================================================\r\n"
    L"; LilithTimer 配置文件\r\n"
    L"; 本文件与 LilithTimer.exe 放在同一目录。\r\n"
    L"; 修改并保存后约 2 秒内自动生效，无需重启程序。\r\n"
    L";============================================================================\r\n"
    L"\r\n"
    L"[Timer]\r\n"
    L"; 第一行显示的文字\r\n"
    L"Text=距离目标还有\r\n"
    L"\r\n"
    L"; 倒计时目标时间。格式：年-月-日 时:分:秒\r\n"
    L"TargetTime=2027-01-01 00:00:00\r\n"
    L"\r\n"
    L"[Display]\r\n"
    L"; 显示模式：Desktop(1) / PassThrough(2) / Float(3)\r\n"
    L"Mode=Float\r\n"
    L"\r\n"
    L"[Style]\r\n"
    L"FontName=微软雅黑\r\n"
    L"FontSize=24\r\n"
    L"CountdownFontSize=38\r\n"
    L"TextColor=FFFFFF\r\n"
    L"CountdownColor=FFC94D\r\n"
    L"BackColor=101418\r\n"
    L"BackOpacity=150\r\n"
    L"Padding=16\r\n"
    L"LineSpacing=6\r\n"
    L"\r\n"
    L"[Window]\r\n"
    L"X=200\r\n"
    L"Y=200\r\n"
    L"Width=0\r\n"
    L"Height=0\r\n";

void CreateDefaultConfig(void)
{
    HANDLE hFile;
    DWORD  written;
    static const BYTE bom[2] = { 0xFF, 0xFE };

    if (FileExists(g_iniPath)) return;
    hFile = CreateFileW(g_iniPath, GENERIC_WRITE, 0, NULL,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    WriteFile(hFile, bom, sizeof(bom), &written, NULL);
    WriteFile(hFile, CONFIG_TEMPLATE,
              (DWORD)(wcslen(CONFIG_TEMPLATE) * sizeof(WCHAR)), &written, NULL);
    CloseHandle(hFile);
}

void LoadConfig(void)
{
    WCHAR buf[256];
    WCHAR defTarget[64] = L"2027-01-01 00:00:00";

    GetPrivateProfileStringW(L"Timer", L"Text", L"距离目标还有",
                             g_cfg.text, COUNT_OF(g_cfg.text), g_iniPath);

    GetPrivateProfileStringW(L"Timer", L"TargetTime", defTarget,
                             buf, COUNT_OF(buf), g_iniPath);
    g_cfg.targetOk = ParseTargetTime(buf, &g_cfg.targetFt);

    GetPrivateProfileStringW(L"Display", L"Mode", L"Float",
                             buf, COUNT_OF(buf), g_iniPath);
    g_cfg.mode = ParseMode(buf, MODE_FLOAT);

    GetPrivateProfileStringW(L"Style", L"FontName", L"微软雅黑",
                             g_cfg.fontName, COUNT_OF(g_cfg.fontName), g_iniPath);
    g_cfg.fontSize   = ClampInt(GetPrivateProfileIntW(L"Style", L"FontSize", 24, g_iniPath), 8, 200);
    g_cfg.cdFontSize = ClampInt(GetPrivateProfileIntW(L"Style", L"CountdownFontSize", 38, g_iniPath), 8, 300);

    GetPrivateProfileStringW(L"Style", L"TextColor", L"FFFFFF", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.textColor = ParseColor(buf, RGB(0xFF, 0xFF, 0xFF));
    GetPrivateProfileStringW(L"Style", L"CountdownColor", L"FFC94D", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.cdColor = ParseColor(buf, RGB(0xFF, 0xC9, 0x4D));
    GetPrivateProfileStringW(L"Style", L"BackColor", L"101418", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.backColor = ParseColor(buf, RGB(0x10, 0x14, 0x18));

    g_cfg.backOpacity = ClampInt(GetPrivateProfileIntW(L"Style", L"BackOpacity", 150, g_iniPath), 0, 255);
    g_cfg.padding     = ClampInt(GetPrivateProfileIntW(L"Style", L"Padding", 16, g_iniPath), 0, 200);
    g_cfg.lineSpacing = ClampInt(GetPrivateProfileIntW(L"Style", L"LineSpacing", 6, g_iniPath), 0, 200);

    g_cfg.x = GetPrivateProfileIntW(L"Window", L"X", 200, g_iniPath);
    g_cfg.y = GetPrivateProfileIntW(L"Window", L"Y", 200, g_iniPath);
    g_cfg.w = ClampInt(GetPrivateProfileIntW(L"Window", L"Width", 0, g_iniPath), 0, 8192);
    g_cfg.h = ClampInt(GetPrivateProfileIntW(L"Window", L"Height", 0, g_iniPath), 0, 8192);
}

void SaveWindowPlacement(void)
{
    WCHAR num[32];
    RECT rc;

    if (g_hwndTimer && IsWindow(g_hwndTimer)) {
        if (GetWindowRect(g_hwndTimer, &rc)) {
            g_cfg.x = rc.left;
            g_cfg.y = rc.top;

            /* 编辑模式的窗口带 WS_THICKFRAME（可拉伸）。
               用户拖动窗口边框后 WM_EXITSIZEMOVE 会调用这里，
               此时把实际尺寸写回配置，下次启动就按这个尺寸显示。
               非编辑模式下没有 WS_THICKFRAME，w/h 保持 0 表示自动大小。*/
            {
                LONG style = GetWindowLongW(g_hwndTimer, GWL_STYLE);
                if (style & WS_THICKFRAME) {
                    g_cfg.w = rc.right - rc.left;
                    g_cfg.h = rc.bottom - rc.top;
                }
            }
        }
    }
    StringCchPrintfW(num, COUNT_OF(num), L"%d", g_cfg.x);
    WritePrivateProfileStringW(L"Window", L"X", num, g_iniPath);
    StringCchPrintfW(num, COUNT_OF(num), L"%d", g_cfg.y);
    WritePrivateProfileStringW(L"Window", L"Y", num, g_iniPath);
    if (g_cfg.w > 0 && g_cfg.h > 0) {
        StringCchPrintfW(num, COUNT_OF(num), L"%d", g_cfg.w);
        WritePrivateProfileStringW(L"Window", L"Width", num, g_iniPath);
        StringCchPrintfW(num, COUNT_OF(num), L"%d", g_cfg.h);
        WritePrivateProfileStringW(L"Window", L"Height", num, g_iniPath);
    }
}

void BuildCountdownText(WCHAR* buf, size_t cch)
{
    FILETIME nowFt;
    ULONGLONG target, now, sec, d, h, m;

    if (!g_cfg.targetOk) {
        StringCchCopyW(buf, cch, L"目标时间无效");
        return;
    }
    GetSystemTimeAsFileTime(&nowFt);
    target = ((ULONGLONG)g_cfg.targetFt.dwHighDateTime << 32) | g_cfg.targetFt.dwLowDateTime;
    now    = ((ULONGLONG)nowFt.dwHighDateTime << 32) | nowFt.dwLowDateTime;
    sec = (target > now) ? (target - now) / 10000000ULL : 0ULL;
    d = sec / 86400ULL;
    h = (sec % 86400ULL) / 3600ULL;
    m = (sec % 3600ULL) / 60ULL;
    {
        ULONGLONG s = sec % 60ULL;
        StringCchPrintfW(buf, cch, L"%llu天%llu小时%llu分钟%llu秒", d, h, m, s);
    }
}

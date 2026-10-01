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

static void TrimSpaces(WCHAR* s)
{
    WCHAR* p = s;
    WCHAR* end;
    if (!s || !*s) return;

    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(WCHAR));

    end = s + wcslen(s) - 1;
    while (end >= s && (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n')) {
        *end = L'\0';
        end--;
    }
}

COLORREF ParseColor(const WCHAR* s, COLORREF def)
{
    WCHAR* end = NULL;
    unsigned long v;
    if (!s || wcslen(s) != 6) return def;
    v = wcstoul(s, &end, 16);
    if (!end || *end != L'\0') return def;
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

/* 返回某年某月的天数（含闰年判断），月份非法返回 0 */
static int DaysInMonth(int year, int month)
{
    static const int dim[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        int leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[month];
}

/* 把指定时区下的墙上时间（已解析成 SYSTEMTIME st）转换成 UTC FILETIME。
   返回 FALSE 表示输入非法（空指针 / SystemTimeToFileTime 转换失败） */
static BOOL WallToUtc(const SYSTEMTIME* st, BOOL useSystem, int tzMinutes, FILETIME* outFt)
{
    FILETIME ft;

    if (!st || !outFt) return FALSE;
    if (!SystemTimeToFileTime(st, &ft)) return FALSE;

    if (useSystem) {
        if (!LocalFileTimeToFileTime(&ft, outFt)) return FALSE;
    }
    else {
        ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        LONGLONG delta = (LONGLONG)tzMinutes * 60LL * 10000000LL;
        t -= delta;
        outFt->dwLowDateTime = (DWORD)t;
        outFt->dwHighDateTime = (DWORD)(t >> 32);
    }
    return TRUE;
}

/* 公开封装：把指定时区下的墙上时间（SYSTEMTIME）转换成 UTC FILETIME，
   供设置对话框的日期时间选择器直接调用 */
BOOL WallTimeToUtc(const SYSTEMTIME* st, BOOL useSystem, int tzMinutes, FILETIME* outFt)
{
    return WallToUtc(st, useSystem, tzMinutes, outFt);
}

/* UTC FILETIME -> 指定时区的墙上 SYSTEMTIME。
   转换失败时把输出清零，避免调用方读到未初始化数据 */
void UtcToWallTime(const FILETIME* utc, BOOL useSystem, int tzMinutes, SYSTEMTIME* outWall)
{
    FILETIME ft;

    if (!outWall) return;
    ZeroMemory(outWall, sizeof(*outWall));
    if (!utc) return;

    ft = *utc;
    if (useSystem) {
        FILETIME local;
        if (!FileTimeToLocalFileTime(&ft, &local)) return;
        ft = local;
    }
    else {
        ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        LONGLONG delta = (LONGLONG)tzMinutes * 60LL * 10000000LL;
        t += delta;
        ft.dwLowDateTime = (DWORD)t;
        ft.dwHighDateTime = (DWORD)(t >> 32);
    }
    if (!FileTimeToSystemTime(&ft, outWall))
        ZeroMemory(outWall, sizeof(*outWall));
}

BOOL ParseTargetTimeAtTz(const WCHAR* src, BOOL useSystem, int tzMinutes, FILETIME* outFt)
{
    WCHAR buf[64];
    WCHAR* p;
    const WCHAR* tail;
    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    int n = 0;
    int fields;
    SYSTEMTIME st;

    if (!outFt) return FALSE;
    if (!src || !*src) return FALSE;

    StringCchCopyW(buf, COUNT_OF(buf), src);
    for (p = buf; *p; p++) {
        if (*p == L'/') *p = L'-';
        else if (*p == L'T') *p = L' ';
    }
    TrimSpaces(buf);
    if (buf[0] == L'\0') return FALSE;

    /* 依次尝试三种格式：完整 / 无秒 / 仅日期；%n 记录实际消费长度 */
    fields = swscanf(buf, L"%d-%d-%d %d:%d:%d%n", &Y, &M, &D, &h, &m, &s, &n);
    if (fields < 6) {
        h = m = s = 0;
        n = 0;
        fields = swscanf(buf, L"%d-%d-%d %d:%d%n", &Y, &M, &D, &h, &m, &n);
        if (fields < 5) {
            h = m = s = 0;
            n = 0;
            fields = swscanf(buf, L"%d-%d-%d%n", &Y, &M, &D, &n);
            if (fields < 3) return FALSE;
        }
    }

    /* 防御：%n 之后只允许空白，拒绝 "2027-06-07 09:00:00xxx" 之类输入 */
    tail = buf + n;
    while (*tail == L' ' || *tail == L'\t' || *tail == L'\r' || *tail == L'\n') tail++;
    if (*tail != L'\0') return FALSE;

    /* 范围校验（含大小月 / 闰年） */
    if (Y < 1970 || Y > 9999) return FALSE;
    if (M < 1 || M > 12) return FALSE;
    if (D < 1 || D > DaysInMonth(Y, M)) return FALSE;
    if (h < 0 || h > 23) return FALSE;
    if (m < 0 || m > 59) return FALSE;
    if (s < 0 || s > 59) return FALSE;

    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)Y;
    st.wMonth = (WORD)M;
    st.wDay = (WORD)D;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)m;
    st.wSecond = (WORD)s;

    return WallToUtc(&st, useSystem, tzMinutes, outFt);
}

BOOL ParseTargetTime(const WCHAR* src, FILETIME* outFt)
{
    return ParseTargetTimeAtTz(src, g_cfg.tzUseSystem, g_cfg.tzOffsetMinutes, outFt);
}

void CreateDefaultConfig(void)
{
    WritePrivateProfileStringW(L"Timer", L"Text", L"高考倒计时", g_iniPath);
    WritePrivateProfileStringW(L"Timer", L"TargetTime", L"2027-06-07 09:00:00", g_iniPath);
    WritePrivateProfileStringW(L"Timer", L"UseSystemTZ", L"1", g_iniPath);
    WritePrivateProfileStringW(L"Timer", L"TZOffsetMinutes", L"480", g_iniPath);

    WritePrivateProfileStringW(L"Display", L"Mode", L"Desktop", g_iniPath);

    WritePrivateProfileStringW(L"Window", L"X", L"200", g_iniPath);
    WritePrivateProfileStringW(L"Window", L"Y", L"200", g_iniPath);
    WritePrivateProfileStringW(L"Window", L"Width", L"0", g_iniPath);
    WritePrivateProfileStringW(L"Window", L"Height", L"0", g_iniPath);

    WritePrivateProfileStringW(L"Style", L"FontName", L"Microsoft YaHei UI", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"FontSize", L"24", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"CountdownFontSize", L"36", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"TextColor", L"FFFFFF", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"CountdownColor", L"FFD700", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"BackColor", L"000000", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"BackOpacity", L"150", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"Padding", L"20", g_iniPath);
    WritePrivateProfileStringW(L"Style", L"LineSpacing", L"10", g_iniPath);
}

void LoadConfig(void)
{
    WCHAR buf[256];

    GetPrivateProfileStringW(L"Timer", L"Text", L"高考倒计时", g_cfg.text, COUNT_OF(g_cfg.text), g_iniPath);
    GetPrivateProfileStringW(L"Timer", L"TargetTime", L"2027-06-07 09:00:00", buf, COUNT_OF(buf), g_iniPath);

    g_cfg.tzUseSystem = GetPrivateProfileIntW(L"Timer", L"UseSystemTZ", 1, g_iniPath) != 0;
    g_cfg.tzOffsetMinutes = ClampInt(GetPrivateProfileIntW(L"Timer", L"TZOffsetMinutes", 480, g_iniPath), -720, 840);

    g_cfg.targetOk = ParseTargetTime(buf, &g_cfg.targetFt);

    GetPrivateProfileStringW(L"Display", L"Mode", L"Desktop", buf, COUNT_OF(buf), g_iniPath);
    if (_wcsicmp(buf, L"Desktop") == 0) g_cfg.mode = MODE_DESKTOP;
    else if (_wcsicmp(buf, L"CompatDesktop") == 0) g_cfg.mode = MODE_DESKTOP_COMPAT;
    else if (_wcsicmp(buf, L"PassThrough") == 0) g_cfg.mode = MODE_PASSTHROUGH;
    else g_cfg.mode = MODE_FLOAT;

    g_cfg.x = GetPrivateProfileIntW(L"Window", L"X", 200, g_iniPath);
    g_cfg.y = GetPrivateProfileIntW(L"Window", L"Y", 200, g_iniPath);
    g_cfg.w = ClampInt(GetPrivateProfileIntW(L"Window", L"Width", 0, g_iniPath), 0, 8192);
    g_cfg.h = ClampInt(GetPrivateProfileIntW(L"Window", L"Height", 0, g_iniPath), 0, 8192);

    GetPrivateProfileStringW(L"Style", L"FontName", L"Microsoft YaHei UI", g_cfg.fontName, COUNT_OF(g_cfg.fontName), g_iniPath);
    g_cfg.fontSize = ClampInt(GetPrivateProfileIntW(L"Style", L"FontSize", 24, g_iniPath), 8, 200);
    g_cfg.cdFontSize = ClampInt(GetPrivateProfileIntW(L"Style", L"CountdownFontSize", 36, g_iniPath), 8, 300);

    GetPrivateProfileStringW(L"Style", L"TextColor", L"FFFFFF", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.textColor = ParseColor(buf, RGB(255, 255, 255));

    GetPrivateProfileStringW(L"Style", L"CountdownColor", L"FFD700", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.cdColor = ParseColor(buf, RGB(255, 215, 0));

    GetPrivateProfileStringW(L"Style", L"BackColor", L"000000", buf, COUNT_OF(buf), g_iniPath);
    g_cfg.backColor = ParseColor(buf, RGB(0, 0, 0));

    g_cfg.backOpacity = ClampInt(GetPrivateProfileIntW(L"Style", L"BackOpacity", 150, g_iniPath), 0, 255);
    g_cfg.padding = ClampInt(GetPrivateProfileIntW(L"Style", L"Padding", 20, g_iniPath), 0, 200);
    g_cfg.lineSpacing = ClampInt(GetPrivateProfileIntW(L"Style", L"LineSpacing", 10, g_iniPath), 0, 200);
}

void SaveWindowPlacement(void)
{
    WCHAR val[32];
    RECT rc;

    if (!g_hwndTimer || !IsWindow(g_hwndTimer)) return;

    if (GetWindowRect(g_hwndTimer, &rc)) {
        g_cfg.x = rc.left;
        g_cfg.y = rc.top;

        if (g_bEditMode) {
            g_cfg.w = rc.right - rc.left;
            g_cfg.h = rc.bottom - rc.top;
        }

        StringCchPrintfW(val, COUNT_OF(val), L"%d", g_cfg.x);
        WritePrivateProfileStringW(L"Window", L"X", val, g_iniPath);

        StringCchPrintfW(val, COUNT_OF(val), L"%d", g_cfg.y);
        WritePrivateProfileStringW(L"Window", L"Y", val, g_iniPath);

        if (g_bEditMode) {
            StringCchPrintfW(val, COUNT_OF(val), L"%d", g_cfg.w);
            WritePrivateProfileStringW(L"Window", L"Width", val, g_iniPath);

            StringCchPrintfW(val, COUNT_OF(val), L"%d", g_cfg.h);
            WritePrivateProfileStringW(L"Window", L"Height", val, g_iniPath);
        }
    }
}

void BuildCountdownText(WCHAR* buf, size_t cch)
{
    FILETIME nowFt;
    ULONGLONG target, now, sec, d, h, m;

    if (!buf || cch == 0) return;

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
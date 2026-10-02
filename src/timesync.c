#include "timesync.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")

/* NTP 时间起点（1900-01-01）到 FILETIME 起点（1601-01-01）的 100ns 差值 */
#define NTP_FT_DELTA  94354848000000000ULL
#define NTP_TIMEOUT_MS 2500

static const WCHAR* const s_ntpServers[] = {
    L"ntp.aliyun.com",
    L"time.windows.com",
    L"ntp.tencent.com",
    L"pool.ntp.org",
    L"cn.pool.ntp.org",
};

/* ------------------------------------------------------------------ */
/* SNTP 查询                                                           */
/* ------------------------------------------------------------------ */

/* 从单个服务器取 NTP 时间（48 字节协议包），成功返回 UTC FILETIME */
static BOOL SntpQueryOne(const WCHAR* host, FILETIME* outFt)
{
    ADDRINFOW hints, *res = NULL, *ai;
    BYTE pkt[48];
    BOOL ok = FALSE;
    int round;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (GetAddrInfoW(host, L"123", &hints, &res) != 0) {
        DbgLog(L"校时: DNS解析失败 %s", host);
        return FALSE;
    }

    ZeroMemory(pkt, sizeof(pkt));
    pkt[0] = 0x1B;   /* LI=0, VN=3, Mode=3 (client) */

    /* 逐地址尝试；同一地址发两次请求（UDP 丢包容忍），每次最多等 NTP_TIMEOUT_MS */
    for (ai = res; ai && !ok; ai = ai->ai_next) {
        /* 本工具只支持 IPv4 NTP（且所列服务器均有 A 记录），IPv6 直接跳过 */
        if (ai->ai_family != AF_INET) continue;
        for (round = 0; round < 2 && !ok; round++) {
            SOCKET sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            int sent, rcvd;
            DWORD secs;
            fd_set rfds;
            struct timeval tv;

            if (sock == INVALID_SOCKET) break;

            sent = sendto(sock, (const char*)pkt, sizeof(pkt), 0,
                          ai->ai_addr, (int)ai->ai_addrlen);
            if (sent != (int)sizeof(pkt)) {
                DbgLog(L"校时: %s sendto 返回 %d wsaerr=%d",
                       host, sent, WSAGetLastError());
                closesocket(sock);
                continue;
            }

            /* 用 select 等待响应（实测部分系统上 SO_RCVTIMEO 对 UDP recv
               会误触发 WSAETIMEDOUT，select 更可靠） */
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            tv.tv_sec = NTP_TIMEOUT_MS / 1000;
            tv.tv_usec = (NTP_TIMEOUT_MS % 1000) * 1000;
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                rcvd = recv(sock, (char*)pkt, sizeof(pkt), 0);
                if (rcvd >= 44) {
                    ULONGLONG ft;
                    secs = ((DWORD)pkt[40] << 24) | ((DWORD)pkt[41] << 16) |
                           ((DWORD)pkt[42] << 8) | (DWORD)pkt[43];
                    if (secs != 0) {   /* 0 = Kiss-o'-Death */
                        ft = ((ULONGLONG)secs * 10000000ULL) + NTP_FT_DELTA;
                        outFt->dwLowDateTime = (DWORD)ft;
                        outFt->dwHighDateTime = (DWORD)(ft >> 32);
                        ok = TRUE;
                    }
                }
            }
            closesocket(sock);
        }
    }

    FreeAddrInfoW(res);
    if (!ok) DbgLog(L"校时: %s 未响应", host);
    return ok;
}

BOOL TimeSyncQuery(TimeSyncResult* out)
{
    WSADATA wsa;
    FILETIME sysFt;
    ULONGLONG net, sys;
    size_t i;
    BOOL got = FALSE;
    int err;

    if (!out) return FALSE;
    ZeroMemory(out, sizeof(*out));

    err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) {
        DbgLog(L"校时: WSAStartup 失败 err=%d", err);
        return FALSE;
    }

    for (i = 0; i < COUNT_OF(s_ntpServers) && !got; i++)
        got = SntpQueryOne(s_ntpServers[i], &out->netUtcFt);

    WSACleanup();

    if (!got) {
        DbgLog(L"校时: 所有NTP服务器均超时，静默跳过");
        return FALSE;
    }

    GetSystemTimeAsFileTime(&sysFt);
    net = ((ULONGLONG)out->netUtcFt.dwHighDateTime << 32) | out->netUtcFt.dwLowDateTime;
    sys = ((ULONGLONG)sysFt.dwHighDateTime << 32) | sysFt.dwLowDateTime;
    out->diffSec = ((LONGLONG)(net - sys)) / 10000000LL;
    out->netOk = TRUE;
    DbgLog(L"校时: 网络时间与系统时间偏差 %lld 秒", out->diffSec);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 系统级校时（需要管理员权限）                                         */
/* ------------------------------------------------------------------ */

/* 用 w32tm /resync /force 让 Windows 时间服务直接从配置的时间源同步。
   返回 TRUE 表示命令成功执行（不代表偏差已归零，但通常已同步）。 */
static BOOL ResyncViaW32tm(void)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = L"open";
    sei.lpFile = L"w32tm.exe";
    sei.lpParameters = L"/resync /force";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return FALSE;
    if (sei.hProcess) {
        DWORD code = 1;
        WaitForSingleObject(sei.hProcess, 30000);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        return code == 0;
    }
    return FALSE;
}

static BOOL SetSystemFromFileTime(const FILETIME* ft)
{
    SYSTEMTIME st;
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES tp;
    BOOL ok = FALSE;

    if (!FileTimeToSystemTime(ft, &st)) return FALSE;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (LookupPrivilegeValueW(NULL, SE_SYSTEMTIME_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
            ok = (GetLastError() == ERROR_SUCCESS);
        }
        CloseHandle(hToken);
    }
    if (!ok) return FALSE;

    ok = SetSystemTime(&st);
    if (ok) {
        /* 通知系统时间已变更，防止依赖系统时间的组件出现误判 */
        SendMessageTimeoutW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0,
                            SMTO_ABORTIFHUNG, 2000, NULL);
    }
    return ok;
}

/* --settime <UTC毫秒>：提权后的子进程入口 */
int TimeSyncElevatedMain(const WCHAR* arg)
{
    FILETIME ft;
    ULONGLONG ms;
    ULONGLONG ftVal;
    WCHAR* end = NULL;

    if (!arg || !*arg) return 2;
    ms = _wcstoui64(arg, &end, 10);
    if (!end || *end != L'\0') return 2;

    ftVal = ms * 10000ULL;
    ft.dwLowDateTime = (DWORD)ftVal;
    ft.dwHighDateTime = (DWORD)(ftVal >> 32);

    /* 首选 w32tm（走 Windows 时间服务的正常通道），失败则直接 SetSystemTime */
    if (ResyncViaW32tm()) return 0;
    if (SetSystemFromFileTime(&ft)) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* 自定义提示框（TaskDialogIndirect，图标+自定义按钮）                  */
/* ------------------------------------------------------------------ */

static void FormatWallTimeW(const FILETIME* utcFt, WCHAR* buf, size_t cch)
{
    FILETIME local;
    SYSTEMTIME st;
    if (FileTimeToLocalFileTime(utcFt, &local) && FileTimeToSystemTime(&local, &st)) {
        StringCchPrintfW(buf, cch, L"%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    } else {
        StringCchCopyW(buf, cch, L"(无法解析)");
    }
}

/* 弹窗；用户点「立即校时」返回 TRUE */
static BOOL TimeSyncPrompt(HWND parent, const TimeSyncResult* r,
                           const WCHAR* netStr, const WCHAR* sysStr)
{
    TASKDIALOGCONFIG tdc;
    TASKDIALOG_BUTTON buttons[2];
    WCHAR content[512];
    WCHAR diffStr[64];
    int pressed = 0;
    HRESULT hr;
    LONGLONG d = r->diffSec;
    LONGLONG ad = (d >= 0) ? d : -d;

    StringCchPrintfW(diffStr, COUNT_OF(diffStr),
        L"%lld 秒（%s %lld 分 %lld 秒）",
        (long long)(d), (d >= 0) ? L"系统时间慢" : L"系统时间快",
        (long long)(ad / 60), (long long)(ad % 60));

    StringCchPrintfW(content, COUNT_OF(content),
        L"网络时间：%s\r\n系统时间：%s\r\n偏差：%s\r\n\r\n"
        L"是否将系统时间校准为网络时间？\r\n"
        L"（需要管理员权限，会弹出 UAC 确认框；\r\n"
        L"选择「暂不校时」则本次继续使用本地时间）",
        netStr, sysStr, diffStr);

    buttons[0].nButtonID = IDYES;
    buttons[0].pszButtonText = L"立即校时(&Y)";
    buttons[1].nButtonID = IDNO;
    buttons[1].pszButtonText = L"暂不校时(&N)";

    ZeroMemory(&tdc, sizeof(tdc));
    tdc.cbSize = sizeof(tdc);
    tdc.hwndParent = parent;
    tdc.hInstance = g_hInst;
    tdc.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                  TDF_POSITION_RELATIVE_TO_WINDOW;
    tdc.dwCommonButtons = 0;               /* 只用自定义按钮 */
    tdc.pszWindowTitle = L"LilithTimer 网络校时";
    tdc.hMainIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                      IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    if (!tdc.hMainIcon) tdc.hMainIcon = LoadIconW(NULL, IDI_WARNING);
    tdc.pszMainInstruction = L"检测到系统时间与网络时间存在偏差";
    tdc.pszContent = content;
    tdc.cButtons = COUNT_OF(buttons);
    tdc.pButtons = buttons;
    tdc.nDefaultButton = IDNO;             /* 默认不校时，避免误触 UAC */

    hr = TaskDialogIndirect(&tdc, &pressed, NULL, NULL);
    if (FAILED(hr)) {
        /* 兜底：普通 MessageBox */
        int rc = MessageBoxW(parent, content, L"LilithTimer 网络校时",
                             MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND |
                             MB_DEFBUTTON2);
        return rc == IDYES;
    }
    return pressed == IDYES;
}

BOOL TimeSyncPromptAndApply(HWND parent, const TimeSyncResult* r)
{
    WCHAR netStr[64], sysStr[64];
    WCHAR argStr[64];
    WCHAR exe[MAX_PATH];
    FILETIME sysFt;
    ULONGLONG netMs;
    SHELLEXECUTEINFOW sei;

    if (!r || !r->netOk) return FALSE;

    FormatWallTimeW(&r->netUtcFt, netStr, COUNT_OF(netStr));
    GetSystemTimeAsFileTime(&sysFt);
    FormatWallTimeW(&sysFt, sysStr, COUNT_OF(sysStr));

    if (!TimeSyncPrompt(parent, r, netStr, sysStr)) {
        DbgLog(L"校时: 用户选择暂不校时，继续使用本地时间");
        return FALSE;
    }

    /* 用户同意 -> 提权重启自身执行校时 */
    netMs = (((ULONGLONG)r->netUtcFt.dwHighDateTime << 32) |
             r->netUtcFt.dwLowDateTime) / 10000ULL;
    GetModuleFileNameW(NULL, exe, COUNT_OF(exe));
    StringCchPrintfW(argStr, COUNT_OF(argStr), L"--settime %llu", netMs);

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = parent;
    sei.lpVerb = L"runas";                 /* 触发 UAC */
    sei.lpFile = exe;
    sei.lpParameters = argStr;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        DbgLog(L"校时: 提权启动失败（用户可能取消了UAC）err=%lu",
               (unsigned long)GetLastError());
        return FALSE;
    }
    if (sei.hProcess) {
        DWORD code = 1;
        WaitForSingleObject(sei.hProcess, 45000);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        if (code == 0) {
            DbgLog(L"校时: 系统时间已校准");
            MessageBoxW(parent, L"系统时间已校准为网络时间。",
                        L"LilithTimer 网络校时",
                        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        } else {
            DbgLog(L"校时: 校时进程返回失败 code=%lu", (unsigned long)code);
            MessageBoxW(parent,
                        L"校时未成功（可能被系统策略限制）。\r\n程序将继续使用本地时间。",
                        L"LilithTimer 网络校时",
                        MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        }
    }
    return TRUE;
}

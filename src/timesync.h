#ifndef LILITH_TIMESYNC_H
#define LILITH_TIMESYNC_H

#include "lilith_timer.h"

/*
 * 网络校时模块
 *
 * 流程（每次程序启动时调用 TimeSyncRunOnce）：
 *   1. 通过 SNTP(UDP/123) 依次查询多个公共时间服务器，取 UTC 网络时间；
 *      网络不可达 / 全部超时 -> 静默失败（仅写诊断日志）。
 *   2. 与系统时间（UTC）比较，偏差 >= 阈值（默认 120 秒）时返回结果，
 *      由调用方弹出自定义提示框询问用户是否校时。
 *   3. 用户同意 -> ShellExecuteEx("runas") 重新拉起自身
 *      （--settime <UTC毫秒> 参数，触发 UAC），提权进程调用
 *      w32tm /resync /force 校时，失败则回退 SetSystemTime。
 *   4. 用户拒绝 -> 什么都不做，程序照常走本地时间。
 */

#define TIMESYNC_THRESHOLD_SEC  120   /* 偏差阈值：>=2 分钟才提示 */

typedef struct {
    BOOL      netOk;          /* 是否成功取到网络时间 */
    FILETIME  netUtcFt;       /* 网络 UTC 时间 */
    LONGLONG  diffSec;        /* 网络时间 - 系统时间（秒，可能为负） */
} TimeSyncResult;

/*
 * 取网络 UTC 时间并比较系统时间。
 * 仅做网络与计算，无任何 UI；成功返回 TRUE，超时/失败返回 FALSE（静默）。
 * 本函数会阻塞数秒（多服务器重试），请在工作线程里调用。
 */
BOOL TimeSyncQuery(TimeSyncResult* out);

/*
 * 在已登录用户桌面弹出「自定义提示框」询问是否校时；
 * 用户点「立即校时」时以 runas 提权重启自身完成系统级校时。
 * parent 可为 NULL。返回 TRUE 表示用户选择了校时（不代表校时成功）。
 */
BOOL TimeSyncPromptAndApply(HWND parent, const TimeSyncResult* r);

/* --settime 子进程入口：wWinMain 解析到该参数时调用，完成后退出。
   arg 为 10 进制 UTC 毫秒（1601 纪元，FILETIME/10000）。 */
int  TimeSyncElevatedMain(const WCHAR* arg);

#endif

#include "autorun.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <taskschd.h>
#include <lmcons.h>      /* UNLEN */
#include <stdlib.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "taskschd.lib")

#define RUN_KEY_PATH   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE_NAME L"LilithTimer"
#define LINK_FILENAME  L"LilithTimer.lnk"

/* ------------------------------------------------------------------ */
/* 工具函数                                                            */
/* ------------------------------------------------------------------ */

/* 取当前 exe 全路径 */
static void GetExePath(WCHAR* buf, DWORD cch)
{
    GetModuleFileNameW(NULL, buf, cch);
}

/* 注册表 Run 键中期望写入的值："C:\path\LilithTimer.exe"（带引号防空格路径） */
static void BuildRunValue(WCHAR* buf, size_t cch)
{
    WCHAR exe[MAX_PATH];
    GetExePath(exe, COUNT_OF(exe));
    StringCchPrintfW(buf, cch, L"\"%s\"", exe);
}

/* 校验注册表值是否就是当前 exe；容忍 "path" 与 path、首尾空白等差异 */
static BOOL RunValueMatchesCurrentExe(const WCHAR* val)
{
    WCHAR exe[MAX_PATH];
    WCHAR tmp[MAX_PATH + 8];
    const WCHAR* p;
    size_t len;

    if (!val || !*val) return FALSE;
    GetExePath(exe, COUNT_OF(exe));

    /* 去首尾空白 */
    p = val;
    while (*p == L' ' || *p == L'\t') p++;
    StringCchCopyW(tmp, COUNT_OF(tmp), p);
    len = wcslen(tmp);
    while (len > 0 && (tmp[len - 1] == L' ' || tmp[len - 1] == L'\t'))
        tmp[--len] = L'\0';

    /* 去首尾引号 */
    if (tmp[0] == L'"') {
        size_t l2;
        memmove(tmp, tmp + 1, (wcslen(tmp + 1) + 1) * sizeof(WCHAR));
        l2 = wcslen(tmp);
        if (l2 > 0 && tmp[l2 - 1] == L'"') tmp[l2 - 1] = L'\0';
    }
    /* 去掉可能跟在后面的一串参数（本程序不接受参数，有参数即视为旧值） */
    {
        WCHAR* sp = wcschr(tmp, L' ');
        if (sp) *sp = L'\0';
    }
    return _wcsicmp(tmp, exe) == 0;
}

/* ------------------------------------------------------------------ */
/* 通道 1：注册表 HKCU\...\Run                                         */
/* ------------------------------------------------------------------ */

static BOOL RegRunWrite(void)
{
    HKEY hKey;
    WCHAR val[MAX_PATH + 8];
    LONG rc;

    rc = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, NULL, 0,
                         KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS) {
        DbgLog(L"自启[注册表]: 打开Run键失败 rc=%ld", (long)rc);
        return FALSE;
    }
    BuildRunValue(val, COUNT_OF(val));
    rc = RegSetValueExW(hKey, RUN_VALUE_NAME, 0, REG_SZ,
                        (const BYTE*)val,
                        (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        DbgLog(L"自启[注册表]: 写入失败 rc=%ld", (long)rc);
        return FALSE;
    }
    return TRUE;
}

static BOOL RegRunQueryMatches(void)
{
    HKEY hKey;
    WCHAR val[MAX_PATH + 8];
    DWORD type = 0, size = sizeof(val);
    LONG rc;
    BOOL match;

    rc = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_QUERY_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) return FALSE;
    rc = RegQueryValueExW(hKey, RUN_VALUE_NAME, NULL, &type, (LPBYTE)val, &size);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return FALSE;
    val[COUNT_OF(val) - 1] = L'\0';
    match = RunValueMatchesCurrentExe(val);
    if (!match) DbgLog(L"自启[注册表]: 值与当前exe不一致: %s", val);
    return match;
}

static void RegRunDelete(void)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0,
                      KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, RUN_VALUE_NAME);
        RegCloseKey(hKey);
    }
}

/* ------------------------------------------------------------------ */
/* 通道 2：启动文件夹快捷方式                                           */
/* ------------------------------------------------------------------ */

static BOOL GetStartupLinkPath(WCHAR* buf, size_t cch)
{
    WCHAR dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, dir)))
        return FALSE;
    StringCchPrintfW(buf, cch, L"%s\\%s", dir, LINK_FILENAME);
    return TRUE;
}

static BOOL StartupLinkWrite(void)
{
    WCHAR link[MAX_PATH];
    WCHAR exe[MAX_PATH];
    IShellLinkW* psl = NULL;
    IPersistFile* ppf = NULL;
    HRESULT hr;
    BOOL ok = FALSE;
    BOOL comInited = FALSE;

    if (!GetStartupLinkPath(link, COUNT_OF(link))) return FALSE;
    GetExePath(exe, COUNT_OF(exe));

    /* 本进程未初始化 COM（托盘/窗口都是纯 Win32），ShellLink 需要 STA */
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) comInited = TRUE;
    else if (hr != RPC_E_CHANGED_MODE) {
        DbgLog(L"自启[快捷方式]: CoInitialize失败 hr=0x%08lX", (long)hr);
        return FALSE;
    }

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) {
        DbgLog(L"自启[快捷方式]: 创建ShellLink失败 hr=0x%08lX", (long)hr);
        return FALSE;
    }
    psl->lpVtbl->SetPath(psl, exe);
    {
        /* 工作目录 = exe 所在目录 */
        WCHAR dir[MAX_PATH];
        WCHAR* slash;
        StringCchCopyW(dir, COUNT_OF(dir), exe);
        slash = wcsrchr(dir, L'\\');
        if (slash) *slash = L'\0';
        else       dir[0] = L'\0';
        psl->lpVtbl->SetWorkingDirectory(psl, dir);
    }
    psl->lpVtbl->SetDescription(psl, L"LilithTimer 开机自启");

    hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->lpVtbl->Save(ppf, link, TRUE);
        ok = SUCCEEDED(hr);
        ppf->lpVtbl->Release(ppf);
    }
    psl->lpVtbl->Release(psl);
    if (comInited) CoUninitialize();
    if (!ok) DbgLog(L"自启[快捷方式]: 写入失败 hr=0x%08lX", (long)hr);
    return ok;
}

static BOOL StartupLinkQueryMatches(void)
{
    WCHAR link[MAX_PATH];
    WCHAR exe[MAX_PATH];
    WCHAR target[MAX_PATH];
    IShellLinkW* psl = NULL;
    IPersistFile* ppf = NULL;
    HRESULT hr;
    BOOL ok = FALSE;
    BOOL comInited = FALSE;

    if (!GetStartupLinkPath(link, COUNT_OF(link))) return FALSE;
    if (!FileExists(link)) return FALSE;
    GetExePath(exe, COUNT_OF(exe));

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) comInited = TRUE;
    else if (hr != RPC_E_CHANGED_MODE) return FALSE;

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void**)&psl);
    if (FAILED(hr)) { if (comInited) CoUninitialize(); return FALSE; }
    hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (void**)&ppf);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(ppf->lpVtbl->Load(ppf, link, STGM_READ)) &&
            SUCCEEDED(psl->lpVtbl->GetPath(psl, target, COUNT_OF(target), NULL, 0))) {
            ok = (_wcsicmp(target, exe) == 0);
        }
        ppf->lpVtbl->Release(ppf);
    }
    psl->lpVtbl->Release(psl);
    if (comInited) CoUninitialize();
    if (!ok) DbgLog(L"自启[快捷方式]: 目标与当前exe不一致");
    return ok;
}

static void StartupLinkDelete(void)
{
    WCHAR link[MAX_PATH];
    if (GetStartupLinkPath(link, COUNT_OF(link)) && FileExists(link))
        DeleteFileW(link);
}

/* ------------------------------------------------------------------ */
/* 通道 3：计划任务（登录触发 / 每日定时触发）                           */
/* ------------------------------------------------------------------ */

/*
 * mode: 1=登录触发（每次开机）；2=每日 startMinutes 触发
 * startMinutes: 0..1439，仅 mode=2 时有效
 */
static BOOL TaskWrite(int mode, int startMinutes)
{
    ITaskService* svc = NULL;
    ITaskFolder* folder = NULL;
    ITaskDefinition* def = NULL;
    IRegistrationInfo* regInfo = NULL;
    ITriggerCollection* triggers = NULL;
    ITrigger* trigger = NULL;
    IActionCollection* actions = NULL;
    IAction* action = NULL;
    IExecAction* execAction = NULL;
    IRegisteredTask* regTask = NULL;
    ITaskSettings* settings = NULL;
    IDailyTrigger* dailyTrigger = NULL;
    ILogonTrigger* logonTrigger = NULL;
    BSTR bstrName = NULL, bstrExe = NULL, bstrDir = NULL, bstrUser = NULL;
    BSTR bstrStart = NULL, bstrId = NULL, bstrTaskName = NULL;
    VARIANT vEmpty;
    HRESULT hr = E_FAIL;
    BOOL comInited = FALSE;
    WCHAR exe[MAX_PATH];
    WCHAR dir[MAX_PATH];
    WCHAR startStr[32];
    WCHAR user[UNLEN + 1];
    DWORD userLen = COUNT_OF(user);
    WCHAR* slash;

    VariantInit(&vEmpty);
    GetExePath(exe, COUNT_OF(exe));
    StringCchCopyW(dir, COUNT_OF(dir), exe);
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = L'\0';

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) comInited = TRUE;
    else if (hr != RPC_E_CHANGED_MODE) goto cleanup;

    /* 任务计划高版本接口要求先设安全选项（进程内才需要，防御性调用） */
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
                              RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              NULL, 0, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        DbgLog(L"自启[计划任务]: CoInitializeSecurity hr=0x%08lX", (long)hr);
    }

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskService, (void**)&svc);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->Connect(svc, vEmpty, vEmpty, vEmpty, vEmpty);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->GetFolder(svc, L"\\", &folder);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->NewTask(svc, 0, &def);
    if (FAILED(hr)) goto cleanup;

    /* 注册信息 */
    hr = def->lpVtbl->get_RegistrationInfo(def, &regInfo);
    if (SUCCEEDED(hr)) {
        regInfo->lpVtbl->put_Author(regInfo, L"LilithTimer");
        regInfo->lpVtbl->put_Description(regInfo, L"LilithTimer 自动启动任务");
    }

    /* 设置：允许按需运行、不限制执行时间、电池可用 */
    hr = def->lpVtbl->get_Settings(def, &settings);
    if (SUCCEEDED(hr)) {
        settings->lpVtbl->put_StopIfGoingOnBatteries(settings, VARIANT_FALSE);
        settings->lpVtbl->put_DisallowStartIfOnBatteries(settings, VARIANT_FALSE);
        settings->lpVtbl->put_AllowDemandStart(settings, VARIANT_TRUE);
        settings->lpVtbl->put_StartWhenAvailable(settings, VARIANT_TRUE);
        settings->lpVtbl->put_MultipleInstances(settings, TASK_INSTANCES_IGNORE_NEW);
    }

    /* 触发器 */
    hr = def->lpVtbl->get_Triggers(def, &triggers);
    if (FAILED(hr)) goto cleanup;

    if (mode == 2) {
        /* 每日定时：startMinutes -> HH:MM */
        int hh = (startMinutes % 1440) / 60;
        int mm = (startMinutes % 1440) % 60;
        StringCchPrintfW(startStr, COUNT_OF(startStr),
                         L"2000-01-01T%02d:%02d:00", hh, mm);
        hr = triggers->lpVtbl->Create(triggers, TASK_TRIGGER_DAILY, &trigger);
        if (FAILED(hr)) goto cleanup;
        hr = trigger->lpVtbl->QueryInterface(trigger, &IID_IDailyTrigger,
                                             (void**)&dailyTrigger);
        if (FAILED(hr)) goto cleanup;
        bstrStart = SysAllocString(startStr);
        bstrId = SysAllocString(L"DailyTrigger");
        if (bstrStart) dailyTrigger->lpVtbl->put_StartBoundary(dailyTrigger, bstrStart);
        if (bstrId)   dailyTrigger->lpVtbl->put_Id(dailyTrigger, bstrId);
        dailyTrigger->lpVtbl->put_DaysInterval(dailyTrigger, 1);
    } else {
        hr = triggers->lpVtbl->Create(triggers, TASK_TRIGGER_LOGON, &trigger);
        if (FAILED(hr)) goto cleanup;
        hr = trigger->lpVtbl->QueryInterface(trigger, &IID_ILogonTrigger,
                                             (void**)&logonTrigger);
        if (FAILED(hr)) goto cleanup;
        bstrId = SysAllocString(L"LogonTrigger");
        if (bstrId) logonTrigger->lpVtbl->put_Id(logonTrigger, bstrId);
        if (GetUserNameW(user, &userLen)) {
            bstrUser = SysAllocString(user);
            if (bstrUser) logonTrigger->lpVtbl->put_UserId(logonTrigger, bstrUser);
        }
    }

    /* 动作：启动 exe */
    hr = def->lpVtbl->get_Actions(def, &actions);
    if (FAILED(hr)) goto cleanup;
    hr = actions->lpVtbl->Create(actions, TASK_ACTION_EXEC, &action);
    if (FAILED(hr)) goto cleanup;
    hr = action->lpVtbl->QueryInterface(action, &IID_IExecAction, (void**)&execAction);
    if (FAILED(hr)) goto cleanup;
    bstrExe = SysAllocString(exe);
    bstrDir = SysAllocString(dir);
    if (bstrExe) execAction->lpVtbl->put_Path(execAction, bstrExe);
    if (bstrDir) execAction->lpVtbl->put_WorkingDirectory(execAction, bstrDir);

    /* 注册（覆盖同名任务）：当前用户，交互式登录，最低权限（程序不需要管理员） */
    bstrTaskName = SysAllocString(AUTORUN_TASK_NAME);
    hr = folder->lpVtbl->RegisterTaskDefinition(
        folder, bstrTaskName, def,
        TASK_CREATE_OR_UPDATE,
        vEmpty,                    /* userId: NULL = 当前用户 */
        vEmpty,                    /* password */
        TASK_LOGON_INTERACTIVE_TOKEN,
        vEmpty,                    /* sddl */
        &regTask);

cleanup:
    if (FAILED(hr))
        DbgLog(L"自启[计划任务]: 写入失败 mode=%d hr=0x%08lX", mode, (long)hr);
    SysFreeString(bstrName); SysFreeString(bstrExe); SysFreeString(bstrDir);
    SysFreeString(bstrUser); SysFreeString(bstrStart); SysFreeString(bstrId);
    SysFreeString(bstrTaskName);
    if (regTask) regTask->lpVtbl->Release(regTask);
    if (dailyTrigger) dailyTrigger->lpVtbl->Release(dailyTrigger);
    if (logonTrigger) logonTrigger->lpVtbl->Release(logonTrigger);
    if (trigger) trigger->lpVtbl->Release(trigger);
    if (triggers) triggers->lpVtbl->Release(triggers);
    if (execAction) execAction->lpVtbl->Release(execAction);
    if (action) action->lpVtbl->Release(action);
    if (actions) actions->lpVtbl->Release(actions);
    if (settings) settings->lpVtbl->Release(settings);
    if (regInfo) regInfo->lpVtbl->Release(regInfo);
    if (def) def->lpVtbl->Release(def);
    if (folder) folder->lpVtbl->Release(folder);
    if (svc) svc->lpVtbl->Release(svc);
    if (comInited) CoUninitialize();
    return SUCCEEDED(hr);
}

static BOOL TaskExists(void)
{
    /* 用 schtasks 查询太啰嗦，直接走 COM */
    ITaskService* svc = NULL;
    ITaskFolder* folder = NULL;
    IRegisteredTask* regTask = NULL;
    BSTR bstrTaskName = NULL;
    VARIANT vEmpty;
    HRESULT hr;
    BOOL comInited = FALSE;
    BOOL exists = FALSE;

    VariantInit(&vEmpty);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) comInited = TRUE;
    else if (hr != RPC_E_CHANGED_MODE) return FALSE;

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskService, (void**)&svc);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->Connect(svc, vEmpty, vEmpty, vEmpty, vEmpty);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->GetFolder(svc, L"\\", &folder);
    if (FAILED(hr)) goto cleanup;
    bstrTaskName = SysAllocString(AUTORUN_TASK_NAME);
    hr = folder->lpVtbl->GetTask(folder, bstrTaskName, &regTask);
    exists = SUCCEEDED(hr);

cleanup:
    SysFreeString(bstrTaskName);
    if (regTask) regTask->lpVtbl->Release(regTask);
    if (folder) folder->lpVtbl->Release(folder);
    if (svc) svc->lpVtbl->Release(svc);
    if (comInited) CoUninitialize();
    return exists;
}

static void TaskDelete(void)
{
    ITaskService* svc = NULL;
    ITaskFolder* folder = NULL;
    BSTR bstrTaskName = NULL;
    VARIANT vEmpty;
    HRESULT hr;
    BOOL comInited = FALSE;

    VariantInit(&vEmpty);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) comInited = TRUE;
    else if (hr != RPC_E_CHANGED_MODE) return;

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskService, (void**)&svc);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->Connect(svc, vEmpty, vEmpty, vEmpty, vEmpty);
    if (FAILED(hr)) goto cleanup;
    hr = svc->lpVtbl->GetFolder(svc, L"\\", &folder);
    if (FAILED(hr)) goto cleanup;
    bstrTaskName = SysAllocString(AUTORUN_TASK_NAME);
    folder->lpVtbl->DeleteTask(folder, bstrTaskName, 0);

cleanup:
    SysFreeString(bstrTaskName);
    if (folder) folder->lpVtbl->Release(folder);
    if (svc) svc->lpVtbl->Release(svc);
    if (comInited) CoUninitialize();
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

void AutoRunSync(BOOL enabled, int mode, int startMinutes)
{
    if (!enabled) {
        RegRunDelete();
        StartupLinkDelete();
        TaskDelete();
        DbgLog(L"自启: 已禁用，三条通道全部清除");
        return;
    }

    /* 三条通道全部（重新）写入，互相冗余 */
    RegRunWrite();
    StartupLinkWrite();
    TaskWrite(mode, startMinutes);
    DbgLog(L"自启: 已启用 mode=%d startMin=%d（注册表+快捷方式+计划任务）",
           mode, startMinutes);
}

void AutoRunVerify(void)
{
    if (!g_cfg.autoStartEnabled) return;

    /* 只校验/修复「登录类」通道；计划任务存在与否也一并检查，
       模式或时间不对时直接重写（代价极低） */
    if (!RegRunQueryMatches()) {
        DbgLog(L"自启校验: 注册表项缺失或失效，已补写");
        RegRunWrite();
    }
    if (!StartupLinkQueryMatches()) {
        DbgLog(L"自启校验: 启动文件夹快捷方式缺失或失效，已补写");
        StartupLinkWrite();
    }
    if (!TaskExists()) {
        DbgLog(L"自启校验: 计划任务缺失，已补写");
        TaskWrite(g_cfg.autoStartMode, g_cfg.autoStartMinutes);
    }
}

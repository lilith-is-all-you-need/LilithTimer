#include "lilith_timer.h"
#include "autorun.h"
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "comctl32.lib")

/* 设置对话框控件 ID */
/* 注意：单选按钮 ID 必须与 DisplayMode 枚举顺序保持连续
   （DESKTOP/DESKTOP_COMPAT/PASSTHROUGH/FLOAT = 0/1/2/3），这样 WM_INITDIALOG 里的
   CheckRadioButton(IDC_RADIO_DESKTOP, IDC_RADIO_FLOAT, IDC_RADIO_DESKTOP + g_cfg.mode)
   才能正确映射四种模式。
   单选按钮使用 2021~2024，避免与 2004~2020 的其他控件 ID 冲突。 */
#define IDC_RADIO_DESKTOP            2021
#define IDC_RADIO_DESKTOP_COMPAT     2022
#define IDC_RADIO_PASSTHROUGH        2023
#define IDC_RADIO_FLOAT              2024
#define IDC_EDIT_TEXT                2004
#define IDC_DTP_TARGET               2005   /* 系统日期时间选择器 */
#define IDC_STATIC_TEXTCOLOR         2006
#define IDC_STATIC_CDCOLOR           2007
#define IDC_STATIC_BACKCOLOR         2008
#define IDC_BTN_TEXTCOLOR            2009
#define IDC_BTN_CDCOLOR              2010
#define IDC_BTN_BACKCOLOR            2011
#define IDC_SLIDER_OPACITY           2012
#define IDC_LABEL_OPACITY            2013
#define IDC_COMBO_FONT               2014   /* 字体名称下拉框 */
#define IDC_EDIT_FONTSIZE            2015
#define IDC_EDIT_CDFONTSIZE          2016
#define IDC_EDIT_PADDING             2017
#define IDC_EDIT_LINESPACING         2018
#define IDC_CHK_SYSTEM_TZ            2019
#define IDC_EDIT_TZ_OFFSET           2020
#define IDC_SLIDER_FONTSIZE          2025
#define IDC_SLIDER_CDFONTSIZE        2026
#define IDC_SLIDER_PADDING           2027
#define IDC_SLIDER_LINESPACING       2028
/* ---- 开机自启 ---- */
#define IDC_CHK_AUTOSTART            2029   /* 总开关：是否启用自启 */
#define IDC_RADIO_AS_BOOT            2030   /* 每次开机启动 */
#define IDC_RADIO_AS_SCHED           2031   /* 每日定时启动 */
#define IDC_DTP_AS_TIME              2032   /* 定时启动的时刻选择器 */

/* 时区下拉框可选项（分钟偏移 + 显示文本） */
typedef struct { int minutes; const WCHAR* label; } TzItem;
static const TzItem s_tzItems[] = {
    {-720, L"UTC-12:00  国际日期变更线西"},
    {-660, L"UTC-11:00  美属萨摩亚"},
    {-600, L"UTC-10:00  夏威夷"},
    {-540, L"UTC-09:00  阿拉斯加"},
    {-480, L"UTC-08:00  美国太平洋时间"},
    {-420, L"UTC-07:00  美国山地时间"},
    {-360, L"UTC-06:00  美国中部时间"},
    {-300, L"UTC-05:00  美国东部时间"},
    {-240, L"UTC-04:00  大西洋时间"},
    {-210, L"UTC-03:30  纽芬兰"},
    {-180, L"UTC-03:00  巴西、阿根廷"},
    {-120, L"UTC-02:00  南乔治亚"},
    {-60,  L"UTC-01:00  亚速尔群岛"},
    {0,    L"UTC±00:00  伦敦、格林尼治"},
    {60,   L"UTC+01:00  巴黎、柏林、罗马"},
    {120,  L"UTC+02:00  雅典、开罗、南非"},
    {180,  L"UTC+03:00  莫斯科、利雅得"},
    {210,  L"UTC+03:30  德黑兰"},
    {240,  L"UTC+04:00  迪拜、巴库"},
    {270,  L"UTC+04:30  喀布尔"},
    {300,  L"UTC+05:00  塔什干、伊斯兰堡"},
    {330,  L"UTC+05:30  新德里、孟买"},
    {345,  L"UTC+05:45  加德满都"},
    {360,  L"UTC+06:00  达卡、阿拉木图"},
    {390,  L"UTC+06:30  仰光"},
    {420,  L"UTC+07:00  曼谷、雅加达、河内"},
    {480,  L"UTC+08:00  北京、上海、香港、新加坡"},
    {540,  L"UTC+09:00  东京、首尔"},
    {570,  L"UTC+09:30  阿德莱德、达尔文"},
    {600,  L"UTC+10:00  悉尼、关岛"},
    {630,  L"UTC+10:30  豪勋爵岛"},
    {660,  L"UTC+11:00  所罗门群岛、新喀里多尼亚"},
    {720,  L"UTC+12:00  奥克兰、斐济"},
    {765,  L"UTC+12:45  查塔姆群岛"},
    {780,  L"UTC+13:00  萨摩亚、汤加"},
    {840,  L"UTC+14:00  基里巴斯·莱恩群岛"},
};
#define TZ_ITEM_COUNT ((int)COUNT_OF(s_tzItems))

/* 根据分钟偏移找到下拉框索引；找不到时取最接近的 */
static int TzMinutesToIndex(int minutes)
{
    int i, best = 0, bestDiff = 0x7FFFFFFF;
    for (i = 0; i < TZ_ITEM_COUNT; i++) {
        int d = s_tzItems[i].minutes - minutes;
        if (d < 0) d = -d;
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

/* 当前编辑的颜色值 */
static COLORREF s_textColor;
static COLORREF s_cdColor;
static COLORREF s_backColor;

/* 实时预览：打开对话框时的配置快照（取消时恢复），s_ready 屏蔽初始化期的 EN_CHANGE */
static AppConfig s_origCfg;
static BOOL      s_ready = FALSE;

/* 自启区域控件联动：总开关控制全部子控件；定时模式才显示时刻选择器 */
static void UpdateAutoStartControls(HWND hdlg)
{
    BOOL enabled = (IsDlgButtonChecked(hdlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
    BOOL sched = enabled &&
                 (IsDlgButtonChecked(hdlg, IDC_RADIO_AS_SCHED) == BST_CHECKED);
    EnableWindow(GetDlgItem(hdlg, IDC_RADIO_AS_BOOT), enabled);
    EnableWindow(GetDlgItem(hdlg, IDC_RADIO_AS_SCHED), enabled);
    ShowWindow(GetDlgItem(hdlg, IDC_DTP_AS_TIME), sched ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(hdlg, IDC_DTP_AS_TIME), sched);
}

/* 打开系统调色板 */
static BOOL PickColor(HWND parent, COLORREF* color)
{
    CHOOSECOLORW cc;
    static COLORREF customColors[16] = {0};

    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = parent;
    cc.rgbResult = *color;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        *color = cc.rgbResult;
        return TRUE;
    }
    return FALSE;
}

/* 更新颜色预览 */
static void UpdateColorPreview(HWND hdlg, int staticId, COLORREF color)
{
    HWND hwndStatic = GetDlgItem(hdlg, staticId);
    HDC hdc;
    RECT rc;
    HBRUSH brush;

    if (!hwndStatic) return;
    hdc = GetDC(hwndStatic);
    if (!hdc) return;
    GetClientRect(hwndStatic, &rc);
    brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    ReleaseDC(hwndStatic, hdc);
}

/* 读取数值编辑框；解析失败时保留旧值，越界时收敛到 [lo, hi] */
static int GetDlgIntClamped(HWND hdlg, int id, int oldVal, int lo, int hi)
{
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(hdlg, id, &ok, FALSE);
    if (!ok) return oldVal;
    if (v > (UINT)hi) return hi;   /* 先按无符号比较，避免 (int) 转换溢出 */
    return ClampInt((int)v, lo, hi);
}

/* 数值编辑框 + 滑块配对，用于「字号/内边距/行距」的滑块同步 */
typedef struct {
    int editId;
    int sliderId;
    int lo, hi;
} NumField;

static const NumField s_numFields[] = {
    { IDC_EDIT_FONTSIZE,    IDC_SLIDER_FONTSIZE,    8, 200 },
    { IDC_EDIT_CDFONTSIZE,  IDC_SLIDER_CDFONTSIZE,  8, 300 },
    { IDC_EDIT_PADDING,     IDC_SLIDER_PADDING,     0, 200 },
    { IDC_EDIT_LINESPACING, IDC_SLIDER_LINESPACING, 0, 200 },
};
#define NUM_FIELD_COUNT ((int)COUNT_OF(s_numFields))

static BOOL s_syncing = FALSE;   /* 程序化更新编辑框时屏蔽 EN_CHANGE，避免与滑块互相触发 */

/* 读取下拉框当前选中的文本（buf 需足够大，≥ LF_FACESIZE） */
static void GetComboSelText(HWND hdlg, int id, WCHAR* buf)
{
    HWND combo = GetDlgItem(hdlg, id);
    LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    buf[0] = L'\0';
    if (sel != CB_ERR)
        SendMessageW(combo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
}

/* 字体枚举回调：把系统字体的 face name 加入下拉框（去重） */
static int CALLBACK EnumFontProc(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM lParam)
{
    HWND combo = (HWND)lParam;
    (void)tm; (void)type;
    if (lf && lf->lfFaceName[0]) {
        if (SendMessageW(combo, CB_FINDSTRING, (WPARAM)-1, (LPARAM)lf->lfFaceName) == CB_ERR)
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    }
    return 1;
}

/* 填充字体下拉框并选中当前字体 */
static void PopulateFontCombo(HWND hdlg)
{
    HWND combo = GetDlgItem(hdlg, IDC_COMBO_FONT);
    LOGFONTW lf;
    HDC hdc;
    LRESULT idx;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    hdc = GetDC(NULL);
    if (hdc) {
        EnumFontFamiliesExW(hdc, &lf, EnumFontProc, (LPARAM)combo, 0);
        ReleaseDC(NULL, hdc);
    }

    idx = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)g_cfg.fontName);
    if (idx == CB_ERR)
        idx = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)g_cfg.fontName);
    SendMessageW(combo, CB_SETCURSEL, idx, 0);
}

/* 把滑块值同步到编辑框（程序化更新，屏蔽 EN_CHANGE） */
static void SyncSliderToEdit(HWND hdlg, const NumField* f)
{
    HWND slider = GetDlgItem(hdlg, f->sliderId);
    int v = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
    s_syncing = TRUE;
    SetDlgItemInt(hdlg, f->editId, (UINT)ClampInt(v, f->lo, f->hi), FALSE);
    s_syncing = FALSE;
}

/* 把编辑框里合法且未越界的值同步到滑块 */
static void SyncEditToSlider(HWND hdlg, const NumField* f)
{
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(hdlg, f->editId, &ok, FALSE);
    if (ok) {
        HWND slider = GetDlgItem(hdlg, f->sliderId);
        if (v < (UINT)f->lo) v = (UINT)f->lo;
        if (v > (UINT)f->hi) v = (UINT)f->hi;
        SendMessageW(slider, TBM_SETPOS, TRUE, (LPARAM)v);
    }
}

/* 从日期时间选择器读取墙上时间并转成 UTC FILETIME */
static BOOL ReadTargetFromDtp(HWND hdlg, FILETIME* outFt)
{
    SYSTEMTIME st;
    HWND dtp = GetDlgItem(hdlg, IDC_DTP_TARGET);
    if (!dtp || !outFt) return FALSE;
    ZeroMemory(&st, sizeof(st));
    SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
    return WallTimeToUtc(&st, g_cfg.tzUseSystem, g_cfg.tzOffsetMinutes, outFt);
}

/*
 * 校验并保存设置到 ini，然后立即重建窗口生效。
 * 返回 TRUE 表示可以关闭对话框；FALSE 表示校验失败（不关闭）。
 */
static BOOL ValidateAndSave(HWND hdlg)
{
    WCHAR buf[256];
    WCHAR targetStr[64];
    WCHAR numStr[16];
    FILETIME ft;
    SYSTEMTIME st;
    HWND slider;
    BOOL writeOk = TRUE;
    /* 先把时区同步进 g_cfg，再读取目标时间 */
    {
        BOOL newUseSystem = (IsDlgButtonChecked(hdlg, IDC_CHK_SYSTEM_TZ) == BST_CHECKED);
        g_cfg.tzUseSystem = newUseSystem;
        if (!newUseSystem) {
            int sel = (int)SendDlgItemMessageW(hdlg, IDC_EDIT_TZ_OFFSET, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= TZ_ITEM_COUNT) sel = TzMinutesToIndex(g_cfg.tzOffsetMinutes);
            g_cfg.tzOffsetMinutes = s_tzItems[sel].minutes;
        }
    }
    /* 目标时间：从日期时间选择器读取（选择器保证日期合法） */
    ZeroMemory(&st, sizeof(st));
    SendDlgItemMessageW(hdlg, IDC_DTP_TARGET, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
    if (!WallTimeToUtc(&st, g_cfg.tzUseSystem, g_cfg.tzOffsetMinutes, &ft)) {
        MessageBoxW(hdlg, L"目标时间无效，本次修改未保存。",
                    L"设置", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return FALSE;
    }
    StringCchPrintfW(targetStr, COUNT_OF(targetStr),
        L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    /* ---- 以下逐项写入 g_cfg 与 ini ---- */

    /* 显示文字 */
    GetDlgItemTextW(hdlg, IDC_EDIT_TEXT, buf, COUNT_OF(buf));
    StringCchCopyW(g_cfg.text, COUNT_OF(g_cfg.text), buf);
    writeOk = WritePrivateProfileStringW(L"Timer", L"Text", buf, g_iniPath) && writeOk;

    /* 目标时间（已校验通过） */
    g_cfg.targetFt = ft;
    g_cfg.targetOk = TRUE;
    writeOk = WritePrivateProfileStringW(L"Timer", L"TargetTime", targetStr, g_iniPath) && writeOk;

    /* 显示模式 */
    if (IsDlgButtonChecked(hdlg, IDC_RADIO_DESKTOP))
        g_cfg.mode = MODE_DESKTOP;
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_DESKTOP_COMPAT))
        g_cfg.mode = MODE_DESKTOP_COMPAT;
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_PASSTHROUGH))
        g_cfg.mode = MODE_PASSTHROUGH;
    else
        g_cfg.mode = MODE_FLOAT;

    switch (g_cfg.mode) {
    case MODE_DESKTOP:        StringCchCopyW(buf, COUNT_OF(buf), L"Desktop");       break;
    case MODE_DESKTOP_COMPAT: StringCchCopyW(buf, COUNT_OF(buf), L"CompatDesktop"); break;
    case MODE_PASSTHROUGH:    StringCchCopyW(buf, COUNT_OF(buf), L"PassThrough");   break;
    default:                  StringCchCopyW(buf, COUNT_OF(buf), L"Float");         break;
    }
    writeOk = WritePrivateProfileStringW(L"Display", L"Mode", buf, g_iniPath) && writeOk;
    /* 时区 */
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.tzUseSystem ? 1 : 0);
    writeOk = WritePrivateProfileStringW(L"Timer", L"UseSystemTZ", numStr, g_iniPath) && writeOk;
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.tzOffsetMinutes);
    writeOk = WritePrivateProfileStringW(L"Timer", L"TZOffsetMinutes", numStr, g_iniPath) && writeOk;

    /* 字体名称 */
    GetComboSelText(hdlg, IDC_COMBO_FONT, buf);
    if (buf[0] == L'\0')
        StringCchCopyW(buf, COUNT_OF(buf), g_cfg.fontName);
    StringCchCopyW(g_cfg.fontName, COUNT_OF(g_cfg.fontName), buf);
    writeOk = WritePrivateProfileStringW(L"Style", L"FontName", buf, g_iniPath) && writeOk;

    /* 字号 / 边距 / 行距（范围与 LoadConfig 的 ClampInt 保持一致） */
    g_cfg.fontSize = GetDlgIntClamped(hdlg, IDC_EDIT_FONTSIZE, g_cfg.fontSize, 8, 200);
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.fontSize);
    writeOk = WritePrivateProfileStringW(L"Style", L"FontSize", numStr, g_iniPath) && writeOk;

    g_cfg.cdFontSize = GetDlgIntClamped(hdlg, IDC_EDIT_CDFONTSIZE, g_cfg.cdFontSize, 8, 300);
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.cdFontSize);
    writeOk = WritePrivateProfileStringW(L"Style", L"CountdownFontSize", numStr, g_iniPath) && writeOk;

    g_cfg.padding = GetDlgIntClamped(hdlg, IDC_EDIT_PADDING, g_cfg.padding, 0, 200);
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.padding);
    writeOk = WritePrivateProfileStringW(L"Style", L"Padding", numStr, g_iniPath) && writeOk;

    g_cfg.lineSpacing = GetDlgIntClamped(hdlg, IDC_EDIT_LINESPACING, g_cfg.lineSpacing, 0, 200);
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.lineSpacing);
    writeOk = WritePrivateProfileStringW(L"Style", L"LineSpacing", numStr, g_iniPath) && writeOk;

    /* 颜色 */
    g_cfg.textColor = s_textColor;
    g_cfg.cdColor = s_cdColor;
    g_cfg.backColor = s_backColor;

    /* COLORREF 的内存布局是 0x00BBGGRR，直接 %06X 出来是 BGR 顺序，
   而 config.c 的 ParseColor 是按 RRGGBB 读的。这里分别取 R/G/B 再格式化。*/
    StringCchPrintfW(buf, COUNT_OF(buf), L"%02X%02X%02X",
        GetRValue(s_textColor), GetGValue(s_textColor), GetBValue(s_textColor));
    writeOk = WritePrivateProfileStringW(L"Style", L"TextColor", buf, g_iniPath) && writeOk;

    StringCchPrintfW(buf, COUNT_OF(buf), L"%02X%02X%02X",
        GetRValue(s_cdColor), GetGValue(s_cdColor), GetBValue(s_cdColor));
    writeOk = WritePrivateProfileStringW(L"Style", L"CountdownColor", buf, g_iniPath) && writeOk;

    StringCchPrintfW(buf, COUNT_OF(buf), L"%02X%02X%02X",
        GetRValue(s_backColor), GetGValue(s_backColor), GetBValue(s_backColor));
    writeOk = WritePrivateProfileStringW(L"Style", L"BackColor", buf, g_iniPath) && writeOk;

    /* 背景不透明度 */
    slider = GetDlgItem(hdlg, IDC_SLIDER_OPACITY);
    if (slider)
        g_cfg.backOpacity = ClampInt((int)SendMessageW(slider, TBM_GETPOS, 0, 0), 0, 255);
    StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", g_cfg.backOpacity);
    writeOk = WritePrivateProfileStringW(L"Style", L"BackOpacity", numStr, g_iniPath) && writeOk;

    /* 开机自启 */
    {
        BOOL asEnabled = (IsDlgButtonChecked(hdlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
        int asMode = IsDlgButtonChecked(hdlg, IDC_RADIO_AS_SCHED) ? 2 : 1;
        int asMinutes = g_cfg.autoStartMinutes;
        HWND dtpAs = GetDlgItem(hdlg, IDC_DTP_AS_TIME);
        if (asMode == 2 && dtpAs) {
            SYSTEMTIME ast;
            ZeroMemory(&ast, sizeof(ast));
            if (SendMessageW(dtpAs, DTM_GETSYSTEMTIME, 0, (LPARAM)&ast) == GDT_VALID)
                asMinutes = ast.wHour * 60 + ast.wMinute;
        }
        g_cfg.autoStartEnabled = asEnabled;
        g_cfg.autoStartMode = asMode;
        g_cfg.autoStartMinutes = asMinutes;
        StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", asEnabled ? 1 : 0);
        writeOk = WritePrivateProfileStringW(L"AutoStart", L"Enabled", numStr, g_iniPath) && writeOk;
        StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", asMode);
        writeOk = WritePrivateProfileStringW(L"AutoStart", L"Mode", numStr, g_iniPath) && writeOk;
        StringCchPrintfW(numStr, COUNT_OF(numStr), L"%d", asMinutes);
        writeOk = WritePrivateProfileStringW(L"AutoStart", L"StartMinutes", numStr, g_iniPath) && writeOk;
        /* 立即生效：写入/更新/删除 注册表+快捷方式+计划任务 */
        AutoRunSync(asEnabled, asMode, asMinutes);
    }

    /* 关键：强制重建窗口并刷新，让设置立即生效 */
    RecreateTimerWindow();
    RefreshContent(TRUE);

    if (!writeOk) {
        MessageBoxW(hdlg,
                    L"设置已立即生效，但写入配置文件失败，重启程序后将还原。\r\n\r\n"
                    L"通常是程序所在目录没有写入权限（例如 Program Files）。\r\n"
                    L"请将程序移动到普通目录，或以管理员身份运行后再修改设置。",
                    L"设置", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    }
    return TRUE;
}

/*
 * 实时预览：把控件当前值写入 g_cfg 并立即刷新窗口（不写 ini）。
 * allowRecreate=TRUE 时允许因显示模式变化而重建窗口。
 * 输入不完整 / 非法的项保持旧值，不打断用户输入；确定后才写入配置文件，
 * 取消 / 关闭则通过 RestoreLivePreview 整体还原。
 */
static void ApplyLive(HWND hdlg, BOOL allowRecreate)
{
    WCHAR buf[256];
    FILETIME ft;
    DisplayMode newMode;
    HWND slider;

    /* 显示文字 */
    GetDlgItemTextW(hdlg, IDC_EDIT_TEXT, buf, COUNT_OF(buf));
    StringCchCopyW(g_cfg.text, COUNT_OF(g_cfg.text), buf);
    /* 时区：先更新 g_cfg，再解析目标时间 */
    {
        BOOL newUseSystem = (IsDlgButtonChecked(hdlg, IDC_CHK_SYSTEM_TZ) == BST_CHECKED);
        int  newOffset = g_cfg.tzOffsetMinutes;
        if (!newUseSystem) {
            int sel = (int)SendDlgItemMessageW(hdlg, IDC_EDIT_TZ_OFFSET, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= TZ_ITEM_COUNT) sel = TzMinutesToIndex(g_cfg.tzOffsetMinutes);
            newOffset = s_tzItems[sel].minutes;
        }
        g_cfg.tzUseSystem = newUseSystem;
        g_cfg.tzOffsetMinutes = newOffset;
    }
    /* 目标时间：日期时间选择器直接给出合法时间 */
    if (ReadTargetFromDtp(hdlg, &ft)) {
        g_cfg.targetFt = ft;
        g_cfg.targetOk = TRUE;
    }

    /* 显示模式 */
    if (IsDlgButtonChecked(hdlg, IDC_RADIO_DESKTOP))            newMode = MODE_DESKTOP;
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_DESKTOP_COMPAT)) newMode = MODE_DESKTOP_COMPAT;
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_PASSTHROUGH))   newMode = MODE_PASSTHROUGH;
    else                                                        newMode = MODE_FLOAT;

    /* 字体 / 字号 / 边距 / 行距 */
    GetComboSelText(hdlg, IDC_COMBO_FONT, buf);
    if (buf[0] != L'\0')
        StringCchCopyW(g_cfg.fontName, COUNT_OF(g_cfg.fontName), buf);
    g_cfg.fontSize    = GetDlgIntClamped(hdlg, IDC_EDIT_FONTSIZE,    g_cfg.fontSize,    8, 200);
    g_cfg.cdFontSize  = GetDlgIntClamped(hdlg, IDC_EDIT_CDFONTSIZE,  g_cfg.cdFontSize,  8, 300);
    g_cfg.padding     = GetDlgIntClamped(hdlg, IDC_EDIT_PADDING,     g_cfg.padding,     0, 200);
    g_cfg.lineSpacing = GetDlgIntClamped(hdlg, IDC_EDIT_LINESPACING, g_cfg.lineSpacing, 0, 200);

    /* 颜色 */
    g_cfg.textColor = s_textColor;
    g_cfg.cdColor   = s_cdColor;
    g_cfg.backColor = s_backColor;

    /* 背景不透明度 */
    slider = GetDlgItem(hdlg, IDC_SLIDER_OPACITY);
    if (slider)
        g_cfg.backOpacity = ClampInt((int)SendMessageW(slider, TBM_GETPOS, 0, 0), 0, 255);

    /* 分层窗口用 UpdateLayeredWindow 整窗更新，非模式变化刷新即可自动调整大小 */
    if (newMode != g_cfg.mode) {
        if (allowRecreate) {
            g_cfg.mode = newMode;
            RecreateTimerWindow();
        }
    } else {
        RefreshContent(TRUE);
    }
}

/* 取消 / 关闭对话框时撤销实时预览的全部改动 */
static void RestoreLivePreview(void)
{
    g_cfg = s_origCfg;
    RecreateTimerWindow();
    RefreshContent(TRUE);
}

/* 对话框过程 */
static INT_PTR CALLBACK SettingsDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        WCHAR opacityStr[16];
        SYSTEMTIME st;
        HWND slider;
        int i;

        /* 实时预览：留快照；初始化期间 SetDlgItemX 也会触发 EN_CHANGE，先屏蔽 */
        s_origCfg = g_cfg;
        s_ready = FALSE;
        s_syncing = FALSE;

        /* 显示模式 */
        CheckRadioButton(hdlg, IDC_RADIO_DESKTOP, IDC_RADIO_FLOAT,
                         IDC_RADIO_DESKTOP + g_cfg.mode);

        /* 显示文字 */
        SetDlgItemTextW(hdlg, IDC_EDIT_TEXT, g_cfg.text);
        SendDlgItemMessageW(hdlg, IDC_EDIT_TEXT, EM_LIMITTEXT,
                            COUNT_OF(g_cfg.text) - 1, 0);
        /*显示图标*/
        {
            HINSTANCE h_inst = (HINSTANCE)GetWindowLongPtrW(hdlg, GWLP_HINSTANCE);
            HICON h_icon = LoadIconW(h_inst, MAKEINTRESOURCEW(IDI_APP_ICON));
            SendMessageW(hdlg, WM_SETICON, ICON_BIG, (LPARAM)h_icon);
            SendMessageW(hdlg, WM_SETICON, ICON_SMALL, (LPARAM)h_icon);
        }

        /* 目标时间：系统日期时间选择器（自定义格式：日期 + 时分秒） */
        {
            HWND dtp = GetDlgItem(hdlg, IDC_DTP_TARGET);
            if (dtp) {
                SendMessageW(dtp, DTM_SETFORMATW, 0,
                    (LPARAM)L"yyyy'-'MM'-'dd HH':'mm':'ss");
                if (g_cfg.targetOk) {
                    UtcToWallTime(&g_cfg.targetFt, g_cfg.tzUseSystem,
                        g_cfg.tzOffsetMinutes, &st);
                    SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
                }
            }
        }

        /* 时区 */
        CheckDlgButton(hdlg, IDC_CHK_SYSTEM_TZ,
            g_cfg.tzUseSystem ? BST_CHECKED : BST_UNCHECKED);
        {
            HWND hCombo = GetDlgItem(hdlg, IDC_EDIT_TZ_OFFSET);
            int i;
            for (i = 0; i < TZ_ITEM_COUNT; i++)
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)s_tzItems[i].label);
            SendMessageW(hCombo, CB_SETCURSEL, TzMinutesToIndex(g_cfg.tzOffsetMinutes), 0);
            EnableWindow(hCombo, !g_cfg.tzUseSystem);
        }

        /* 开机自启 */
        CheckDlgButton(hdlg, IDC_CHK_AUTOSTART,
            g_cfg.autoStartEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(hdlg, IDC_RADIO_AS_BOOT, IDC_RADIO_AS_SCHED,
            g_cfg.autoStartMode == 2 ? IDC_RADIO_AS_SCHED : IDC_RADIO_AS_BOOT);
        {
            HWND dtpAs = GetDlgItem(hdlg, IDC_DTP_AS_TIME);
            if (dtpAs) {
                SYSTEMTIME ast;
                SendMessageW(dtpAs, DTM_SETFORMATW, 0, (LPARAM)L"HH':'mm");
                GetLocalTime(&ast);
                ast.wHour = (WORD)(g_cfg.autoStartMinutes / 60);
                ast.wMinute = (WORD)(g_cfg.autoStartMinutes % 60);
                ast.wSecond = 0;
                SendMessageW(dtpAs, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&ast);
            }
        }
        UpdateAutoStartControls(hdlg);

        /* 字体名称下拉框（枚举系统字体） */
        PopulateFontCombo(hdlg);

        /* 字号 / 边距 / 行距：编辑框 + 滑块 */
        SetDlgItemInt(hdlg, IDC_EDIT_FONTSIZE, (UINT)g_cfg.fontSize, FALSE);
        SetDlgItemInt(hdlg, IDC_EDIT_CDFONTSIZE, (UINT)g_cfg.cdFontSize, FALSE);
        SetDlgItemInt(hdlg, IDC_EDIT_PADDING, (UINT)g_cfg.padding, FALSE);
        SetDlgItemInt(hdlg, IDC_EDIT_LINESPACING, (UINT)g_cfg.lineSpacing, FALSE);
        {
            struct { int sliderId; int lo, hi, val; } init[] = {
                { IDC_SLIDER_FONTSIZE,    8, 200, g_cfg.fontSize },
                { IDC_SLIDER_CDFONTSIZE,  8, 300, g_cfg.cdFontSize },
                { IDC_SLIDER_PADDING,     0, 200, g_cfg.padding },
                { IDC_SLIDER_LINESPACING, 0, 200, g_cfg.lineSpacing },
            };
            for (i = 0; i < (int)COUNT_OF(init); i++) {
                HWND s = GetDlgItem(hdlg, init[i].sliderId);
                if (s) {
                    SendMessageW(s, TBM_SETRANGE, TRUE, MAKELONG(init[i].lo, init[i].hi));
                    SendMessageW(s, TBM_SETPOS, TRUE, init[i].val);
                }
            }
        }

        /* 颜色预览 */
        s_textColor = g_cfg.textColor;
        s_cdColor = g_cfg.cdColor;
        s_backColor = g_cfg.backColor;
        UpdateColorPreview(hdlg, IDC_STATIC_TEXTCOLOR, s_textColor);
        UpdateColorPreview(hdlg, IDC_STATIC_CDCOLOR, s_cdColor);
        UpdateColorPreview(hdlg, IDC_STATIC_BACKCOLOR, s_backColor);

        /* 不透明度滑块 */
        slider = GetDlgItem(hdlg, IDC_SLIDER_OPACITY);
        if (slider) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
            SendMessageW(slider, TBM_SETPOS, TRUE, g_cfg.backOpacity);
        }

        StringCchPrintfW(opacityStr, COUNT_OF(opacityStr), L"%d", g_cfg.backOpacity);
        SetDlgItemTextW(hdlg, IDC_LABEL_OPACITY, opacityStr);

        s_ready = TRUE;   /* 初始化完成，开放实时预览 */
        return TRUE;
    }

    case WM_HSCROLL: {
        HWND ctrl = (HWND)lp;
        int i;
        if (!ctrl) break;

        /* 数值滑块 → 同步编辑框 + 实时预览 */
        for (i = 0; i < NUM_FIELD_COUNT; i++) {
            if (GetDlgItem(hdlg, s_numFields[i].sliderId) == ctrl) {
                SyncSliderToEdit(hdlg, &s_numFields[i]);
                if (s_ready) ApplyLive(hdlg, FALSE);
                return TRUE;
            }
        }

        /* 不透明度滑块 → 更新数值标签 */
        {
            HWND slider = GetDlgItem(hdlg, IDC_SLIDER_OPACITY);
            if (slider && ctrl == slider) {
                int pos = (int)SendMessageW(ctrl, TBM_GETPOS, 0, 0);
                WCHAR str[16];
                StringCchPrintfW(str, COUNT_OF(str), L"%d", pos);
                SetDlgItemTextW(hdlg, IDC_LABEL_OPACITY, str);
                if (s_ready) ApplyLive(hdlg, FALSE);
                return TRUE;
            }
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm && nm->idFrom == IDC_DTP_TARGET && nm->code == DTN_DATETIMECHANGE) {
            if (s_ready) ApplyLive(hdlg, FALSE);
            return TRUE;
        }
        /* 自启定时选择器：无实时预览（取消会还原），这里无需处理 */
        break;
    }

    case WM_DRAWITEM: {
        /* 绘制颜色预览静态控件 */
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->CtlID == IDC_STATIC_TEXTCOLOR ||
            dis->CtlID == IDC_STATIC_CDCOLOR ||
            dis->CtlID == IDC_STATIC_BACKCOLOR) {
            COLORREF color = RGB(255, 255, 255);
            HBRUSH brush;
            HPEN pen;
            HGDIOBJ oldPen, oldBrush;

            if (dis->CtlID == IDC_STATIC_TEXTCOLOR)      color = s_textColor;
            else if (dis->CtlID == IDC_STATIC_CDCOLOR)   color = s_cdColor;
            else if (dis->CtlID == IDC_STATIC_BACKCOLOR) color = s_backColor;

            brush = CreateSolidBrush(color);
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);

            /* 边框 */
            pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
            oldPen = SelectObject(dis->hDC, pen);
            oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                      dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldBrush);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(pen);

            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD notify = HIWORD(wp);

        /* 编辑框内容变化 → 同步滑块 + 实时预览 */
        if (notify == EN_CHANGE && s_ready && !s_syncing &&
            (id == IDC_EDIT_TEXT || id == IDC_EDIT_FONTSIZE ||
                id == IDC_EDIT_CDFONTSIZE || id == IDC_EDIT_PADDING ||
                id == IDC_EDIT_LINESPACING)) {
            int i;
            for (i = 0; i < NUM_FIELD_COUNT; i++) {
                if (s_numFields[i].editId == id) {
                    SyncEditToSlider(hdlg, &s_numFields[i]);
                    break;
                }
            }
            ApplyLive(hdlg, FALSE);
            return TRUE;
        }

        /* 字体下拉框选择变化 → 实时预览 */
        if (id == IDC_COMBO_FONT && notify == CBN_SELCHANGE && s_ready) {
            ApplyLive(hdlg, FALSE);
            return TRUE;
        }

        /* 时区下拉框选择变化 → 实时预览 */
        if (id == IDC_EDIT_TZ_OFFSET && notify == CBN_SELCHANGE && s_ready) {
            ApplyLive(hdlg, FALSE);
            return TRUE;
        }

        /* "跟随系统时区" 复选框切换 */
        if (id == IDC_CHK_SYSTEM_TZ && notify == BN_CLICKED) {
            BOOL useSystem = (IsDlgButtonChecked(hdlg, IDC_CHK_SYSTEM_TZ) == BST_CHECKED);
            EnableWindow(GetDlgItem(hdlg, IDC_EDIT_TZ_OFFSET), !useSystem);
            if (s_ready) ApplyLive(hdlg, FALSE);
            return TRUE;
        }

        /* 显示模式单选 → 实时预览（模式变化需要重建窗口） */
        if (notify == BN_CLICKED && s_ready &&
            (id == IDC_RADIO_DESKTOP || id == IDC_RADIO_DESKTOP_COMPAT ||
             id == IDC_RADIO_PASSTHROUGH || id == IDC_RADIO_FLOAT)) {
            ApplyLive(hdlg, TRUE);
            return TRUE;
        }

        /* 开机自启：总开关 / 两种模式切换 → 联动子控件显隐 */
        if (notify == BN_CLICKED &&
            (id == IDC_CHK_AUTOSTART || id == IDC_RADIO_AS_BOOT ||
             id == IDC_RADIO_AS_SCHED)) {
            UpdateAutoStartControls(hdlg);
            return TRUE;
        }

        switch (id) {
        case IDC_BTN_TEXTCOLOR:
            if (PickColor(hdlg, &s_textColor)) {
                UpdateColorPreview(hdlg, IDC_STATIC_TEXTCOLOR, s_textColor);
                if (s_ready) ApplyLive(hdlg, FALSE);
            }
            return TRUE;

        case IDC_BTN_CDCOLOR:
            if (PickColor(hdlg, &s_cdColor)) {
                UpdateColorPreview(hdlg, IDC_STATIC_CDCOLOR, s_cdColor);
                if (s_ready) ApplyLive(hdlg, FALSE);
            }
            return TRUE;

        case IDC_BTN_BACKCOLOR:
            if (PickColor(hdlg, &s_backColor)) {
                UpdateColorPreview(hdlg, IDC_STATIC_BACKCOLOR, s_backColor);
                if (s_ready) ApplyLive(hdlg, FALSE);
            }
            return TRUE;

        case IDOK:
            if (ValidateAndSave(hdlg))
                EndDialog(hdlg, IDOK);
            return TRUE;

        case IDCANCEL:
            RestoreLivePreview();   /* 撤销实时预览 */
            EndDialog(hdlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        RestoreLivePreview();       /* 点 X 关闭同样撤销实时预览 */
        EndDialog(hdlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/*===========================================================================
 * 对话框内存模板构建器
 *
 * 修复说明：旧版手工把 dlg->cdit 写成 17，但模板里实际写入了 22 个控件，
 * 导致排第 18 位之后的不透明度滑块、「确定」「取消」按钮从未被创建，
 * 设置对话框无法保存，成了摆设。现在由构建器自动计数，写入多少就是多少。
 *===========================================================================*/
typedef struct {
    DLGTEMPLATE* dlg;
    WCHAR* p;
    int count;
} DlgBuilder;

static void DlgAlign(DlgBuilder* b)
{
    while ((ULONG_PTR)b->p % 4) b->p++;
}

static void DlgBegin(DlgBuilder* b, BYTE* buf, short cx, short cy,
                     const WCHAR* title)
{
    b->dlg = (DLGTEMPLATE*)buf;
    b->dlg->style = DS_SETFONT | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    b->dlg->dwExtendedStyle = 0;
    b->dlg->cdit = 0;
    b->dlg->x = 0; b->dlg->y = 0;
    b->dlg->cx = cx; b->dlg->cy = cy;
    b->count = 0;

    b->p = (WCHAR*)(b->dlg + 1);
    *b->p++ = 0;                                   /* 菜单：无 */
    *b->p++ = 0;                                   /* 窗口类：默认 */
    wcscpy(b->p, title);
    b->p += wcslen(title) + 1;
    *(WORD*)b->p = 9; b->p += 1;                   /* 字号 9pt */
    wcscpy(b->p, L"Microsoft YaHei UI");
    b->p += wcslen(L"Microsoft YaHei UI") + 1;
}

/* 添加使用系统预定义类原子的控件：0x0080=按钮 0x0081=编辑框 0x0082=静态文本 */
static void DlgAddAtom(DlgBuilder* b, WORD atom, DWORD style, DWORD exStyle,
                       int x, int y, int cx, int cy, int id, const WCHAR* text)
{
    DLGITEMTEMPLATE* item;

    DlgAlign(b);
    item = (DLGITEMTEMPLATE*)b->p;
    item->style = style;
    item->dwExtendedStyle = exStyle;
    item->x = (short)x;  item->y = (short)y;
    item->cx = (short)cx; item->cy = (short)cy;
    item->id = (WORD)id;

    b->p = (WCHAR*)(item + 1);
    *b->p++ = 0xFFFF;
    *b->p++ = atom;
    if (text) {
        wcscpy(b->p, text);
        b->p += wcslen(text) + 1;
    } else {
        *b->p++ = 0;
    }
    *b->p++ = 0;                                   /* creation data：无 */
    b->count++;
}

/* 添加使用类名的控件（如 TRACKBAR_CLASSW 滑块） */
static void DlgAddClass(DlgBuilder* b, const WCHAR* cls, DWORD style, DWORD exStyle,
                        int x, int y, int cx, int cy, int id, const WCHAR* text)
{
    DLGITEMTEMPLATE* item;

    DlgAlign(b);
    item = (DLGITEMTEMPLATE*)b->p;
    item->style = style;
    item->dwExtendedStyle = exStyle;
    item->x = (short)x;  item->y = (short)y;
    item->cx = (short)cx; item->cy = (short)cy;
    item->id = (WORD)id;

    b->p = (WCHAR*)(item + 1);
    wcscpy(b->p, cls);
    b->p += wcslen(cls) + 1;
    if (text) {
        wcscpy(b->p, text);
        b->p += wcslen(text) + 1;
    } else {
        *b->p++ = 0;
    }
    *b->p++ = 0;
    b->count++;
}

static DLGTEMPLATE* DlgEnd(DlgBuilder* b)
{
    b->dlg->cdit = (WORD)b->count;                 /* 自动计数，杜绝漏建控件 */
    return b->dlg;
}

#define ATOM_BUTTON 0x0080
#define ATOM_EDIT   0x0081
#define ATOM_STATIC 0x0082

/* 创建对话框内存模板 - 涵盖 ini 中除 [Window] 自动段之外的全部配置项 */
static DLGTEMPLATE* CreateDialogTemplate(void)
{
    static BYTE buffer[8192];
    DlgBuilder b;
    int i;

    static const int radioIds[] = { IDC_RADIO_DESKTOP, IDC_RADIO_DESKTOP_COMPAT,
                                    IDC_RADIO_PASSTHROUGH, IDC_RADIO_FLOAT };
    static const LPCWSTR radioLabels[] = { L"[原生]贴桌面", L"[兼容]贴桌面", L"穿透", L"浮动" };
    static const int radioX[] = { 14, 14, 170, 170 };
    static const int radioY[] = { 18, 36, 18, 36 };
    static const int radioW[] = { 140, 140, 64, 64 };
    static const int colorStaticIds[] = { IDC_STATIC_TEXTCOLOR, IDC_STATIC_CDCOLOR, IDC_STATIC_BACKCOLOR };
    static const int colorBtnIds[] = { IDC_BTN_TEXTCOLOR, IDC_BTN_CDCOLOR, IDC_BTN_BACKCOLOR };
    static const LPCWSTR colorLabels[] = { L"文字颜色", L"倒计时颜色", L"背景颜色" };

    DlgBegin(&b, buffer, 320, 398, L"设置");

    /* ---- 显示模式 ---- */
    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0,
               8, 6, 304, 64, 0xFFFF, L"显示模式");
    for (i = 0; i < 4; i++) {
        DlgAddAtom(&b, ATOM_BUTTON,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                   (i == 0 ? WS_GROUP : 0), 0,
                   radioX[i], radioY[i], radioW[i], 12, radioIds[i], radioLabels[i]);
    }
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               16, 54, 288, 12, 0xFFFF, L"原生贴桌面失效时，请改用兼容");

    /* ---- 显示文字 ----（WS_GROUP 用于结束上面的单选按钮组） */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE | WS_GROUP, 0,
               12, 80, 64, 12, 0xFFFF, L"显示文字:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
               80, 78, 228, 14, IDC_EDIT_TEXT, NULL);

    /* ---- 目标时间（系统日期时间选择器） ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 98, 64, 12, 0xFFFF, L"目标时间:");
    DlgAddClass(&b, DATETIMEPICK_CLASSW,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
        80, 96, 228, 16, IDC_DTP_TARGET, NULL);

    /* ---- 时区 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 116, 64, 12, 0xFFFF, L"时区:");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,
               80, 116, 70, 12, IDC_CHK_SYSTEM_TZ, L"跟随系统");
    DlgAddClass(&b, L"ComboBox",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE,
        156, 114, 152, 200, IDC_EDIT_TZ_OFFSET, NULL);

    /* ---- 字体名称（系统字体下拉框） ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 134, 64, 12, 0xFFFF, L"字体名称:");
    DlgAddClass(&b, L"ComboBox",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE,
        80, 132, 228, 200, IDC_COMBO_FONT, NULL);

    /* ---- 文字字号 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 152, 88, 12, 0xFFFF, L"文字字号:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               104, 150, 46, 14, IDC_EDIT_FONTSIZE, NULL);
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                156, 150, 152, 16, IDC_SLIDER_FONTSIZE, NULL);

    /* ---- 倒计时字号 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 170, 88, 12, 0xFFFF, L"倒计时字号:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               104, 168, 46, 14, IDC_EDIT_CDFONTSIZE, NULL);
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                156, 168, 152, 16, IDC_SLIDER_CDFONTSIZE, NULL);

    /* ---- 颜色（标签 / 预览 / 选择按钮） ---- */
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
                   12 + i * 100, 190, 96, 12, 0xFFFF, colorLabels[i]);
    }
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0,
                   12 + i * 100, 204, 28, 16, colorStaticIds[i], NULL);
    }
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_BUTTON,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                   44 + i * 100, 204, 40, 16, colorBtnIds[i], L"...");
    }

    /* ---- 背景不透明度 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 226, 88, 12, 0xFFFF, L"不透明度:");
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                104, 224, 170, 16, IDC_SLIDER_OPACITY, NULL);
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               280, 226, 28, 12, IDC_LABEL_OPACITY, L"150");

    /* ---- 内边距 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 244, 88, 12, 0xFFFF, L"内边距:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               104, 242, 46, 14, IDC_EDIT_PADDING, NULL);
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                156, 242, 152, 16, IDC_SLIDER_PADDING, NULL);

    /* ---- 行距 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 262, 88, 12, 0xFFFF, L"行距:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               104, 260, 46, 14, IDC_EDIT_LINESPACING, NULL);
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                156, 260, 152, 16, IDC_SLIDER_LINESPACING, NULL);

    /* ---- 开机自启 ---- */
    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0,
               8, 282, 304, 80, 0xFFFF, L"开机自启 / 定时启动");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | WS_GROUP, 0,
               18, 296, 180, 12, IDC_CHK_AUTOSTART, L"启用开机自启（默认开启）");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0,
               28, 312, 240, 12, IDC_RADIO_AS_BOOT,
               L"每次开机启动（注册表+启动文件夹+计划任务 三重保障）");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0,
               28, 328, 110, 12, IDC_RADIO_AS_SCHED, L"每日定时启动");
    DlgAddClass(&b, DATETIMEPICK_CLASSW,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT, 0,
        150, 326, 80, 16, IDC_DTP_AS_TIME, NULL);
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               18, 346, 288, 12, 0xFFFF,
               L"定时启动晚于开机时间时，登录时仍会兜底拉起一次");

    /* ---- 确定 / 取消 ---- */
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
               108, 366, 72, 20, IDOK, L"确定");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
               200, 366, 72, 20, IDCANCEL, L"取消");

    return DlgEnd(&b);
}

void ShowSettingsDialog(HWND parent)
{
    /* 初始化通用控件（滑块 + 日期时间选择器需要） */
    INITCOMMONCONTROLSEX icc;
    DLGTEMPLATE* dlgTemplate;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&icc);

    dlgTemplate = CreateDialogTemplate();
    DialogBoxIndirectParamW(g_hInst, dlgTemplate, parent, SettingsDlgProc, 0);
}

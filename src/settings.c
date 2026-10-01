#include "lilith_timer.h"
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "comctl32.lib")

/* 设置对话框控件 ID */
#define IDC_RADIO_DESKTOP     2001
#define IDC_RADIO_PASSTHROUGH 2002
#define IDC_RADIO_FLOAT       2003
#define IDC_EDIT_TEXT         2004
#define IDC_EDIT_TARGET       2005
#define IDC_STATIC_TEXTCOLOR  2006
#define IDC_STATIC_CDCOLOR    2007
#define IDC_STATIC_BACKCOLOR  2008
#define IDC_BTN_TEXTCOLOR     2009
#define IDC_BTN_CDCOLOR       2010
#define IDC_BTN_BACKCOLOR     2011
#define IDC_SLIDER_OPACITY    2012
#define IDC_LABEL_OPACITY     2013
#define IDC_EDIT_FONTNAME     2014
#define IDC_EDIT_FONTSIZE     2015
#define IDC_EDIT_CDFONTSIZE   2016
#define IDC_EDIT_PADDING      2017
#define IDC_EDIT_LINESPACING  2018

/* 当前编辑的颜色值 */
static COLORREF s_textColor;
static COLORREF s_cdColor;
static COLORREF s_backColor;

/* 实时预览：打开对话框时的配置快照（取消时恢复），s_ready 屏蔽初始化期的 EN_CHANGE */
static AppConfig s_origCfg;
static BOOL      s_ready = FALSE;
static HWND      s_hTargetTip = NULL;   /* 目标时间输入框的气泡提示 */

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
    HWND slider;
    BOOL writeOk = TRUE;

    /* 目标时间先校验：格式错误则提示并留在对话框中 */
    GetDlgItemTextW(hdlg, IDC_EDIT_TARGET, targetStr, COUNT_OF(targetStr));
    if (!ParseTargetTime(targetStr, &ft)) {
        MessageBoxW(hdlg,
                    L"目标时间格式不正确，本次修改未保存。\r\n\r\n"
                    L"正确格式示例：2027-01-01 00:00:00\r\n"
                    L"（秒可省略为 2027-01-01 00:00；分隔符 - 和 / 均可）",
                    L"设置", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        SetFocus(GetDlgItem(hdlg, IDC_EDIT_TARGET));
        SendDlgItemMessageW(hdlg, IDC_EDIT_TARGET, EM_SETSEL, 0, -1);
        return FALSE;
    }

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
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_PASSTHROUGH))
        g_cfg.mode = MODE_PASSTHROUGH;
    else
        g_cfg.mode = MODE_FLOAT;

    switch (g_cfg.mode) {
    case MODE_DESKTOP:     StringCchCopyW(buf, COUNT_OF(buf), L"Desktop");     break;
    case MODE_PASSTHROUGH: StringCchCopyW(buf, COUNT_OF(buf), L"PassThrough"); break;
    default:               StringCchCopyW(buf, COUNT_OF(buf), L"Float");       break;
    }
    writeOk = WritePrivateProfileStringW(L"Display", L"Mode", buf, g_iniPath) && writeOk;

    /* 字体名称（留空表示不修改） */
    GetDlgItemTextW(hdlg, IDC_EDIT_FONTNAME, buf, COUNT_OF(buf));
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

    /* 目标时间：输入完整合法才更新，非法时保持旧值 */
    GetDlgItemTextW(hdlg, IDC_EDIT_TARGET, buf, COUNT_OF(buf));
    if (ParseTargetTime(buf, &ft)) {
        g_cfg.targetFt = ft;
        g_cfg.targetOk = TRUE;
    }

    /* 显示模式 */
    if (IsDlgButtonChecked(hdlg, IDC_RADIO_DESKTOP))          newMode = MODE_DESKTOP;
    else if (IsDlgButtonChecked(hdlg, IDC_RADIO_PASSTHROUGH)) newMode = MODE_PASSTHROUGH;
    else                                                      newMode = MODE_FLOAT;

    /* 字体 / 字号 / 边距 / 行距 */
    GetDlgItemTextW(hdlg, IDC_EDIT_FONTNAME, buf, COUNT_OF(buf));
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
        s_hTargetTip = NULL;
        WCHAR timeStr[64];
        WCHAR opacityStr[16];
        SYSTEMTIME st;
        HWND slider;

        /* 实时预览：留快照；初始化期间 SetDlgItemX 也会触发 EN_CHANGE，先屏蔽 */
        s_origCfg = g_cfg;
        s_ready = FALSE;

        /* 显示模式 */
        CheckRadioButton(hdlg, IDC_RADIO_DESKTOP, IDC_RADIO_FLOAT,
                         IDC_RADIO_DESKTOP + g_cfg.mode);

        /* 显示文字 */
        SetDlgItemTextW(hdlg, IDC_EDIT_TEXT, g_cfg.text);
        SendDlgItemMessageW(hdlg, IDC_EDIT_TEXT, EM_LIMITTEXT,
                            COUNT_OF(g_cfg.text) - 1, 0);
        /*显示图标*/
        HINSTANCE h_inst = (HINSTANCE)GetWindowLongPtrW(hdlg, GWLP_HINSTANCE);
        HICON h_icon = LoadIconW(h_inst, MAKEINTRESOURCEW(IDI_APP_ICON));
        SendMessageW(hdlg, WM_SETICON, ICON_BIG, (LPARAM)h_icon);
        SendMessageW(hdlg, WM_SETICON, ICON_SMALL, (LPARAM)h_icon);

        /* 目标时间 */
        if (g_cfg.targetOk) {
            FileTimeToSystemTime(&g_cfg.targetFt, &st);
            StringCchPrintfW(timeStr, COUNT_OF(timeStr),
                L"%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        } else {
            StringCchCopyW(timeStr, COUNT_OF(timeStr), L"2027-01-01 00:00:00");
        }
        SetDlgItemTextW(hdlg, IDC_EDIT_TARGET, timeStr);
        /* --- 目标时间输入框：灰色提示 + 气泡 Tooltip --- */
        {
            HWND hTarget = GetDlgItem(hdlg, IDC_EDIT_TARGET);
            if (hTarget) {
                /* 输入框为空时显示灰色示例（用户一开始输入就消失，
                   删空又会回来），TRUE 表示获得焦点时也显示 */
                SendMessageW(hTarget, EM_SETCUEBANNER, TRUE,
                    (LPARAM)L"例如：2027-01-01 00:00:00");
            }

            /* 气泡提示：鼠标悬停在输入框上时弹出完整格式说明 */
            s_hTargetTip = CreateWindowExW(
                0, TOOLTIPS_CLASSW, NULL,
                WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hdlg, NULL, g_hInst, NULL);

            if (s_hTargetTip && hTarget) {
                TOOLINFOW ti;
                ZeroMemory(&ti, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = hdlg;
                ti.uId = (UINT_PTR)hTarget;
                ti.lpszText = (LPWSTR)L"请按 YYYY-MM-DD HH:MM:SS 格式输入。\n"
                    L"例如：2027-01-01 00:00:00\n"
                    L"秒可以省略：2027-01-01 00:00\n"
                    L"日期分隔符 - 和 / 均可。";
                SendMessageW(s_hTargetTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
                SendMessageW(s_hTargetTip, TTM_SETMAXTIPWIDTH, 0, 320);
                SendMessageW(s_hTargetTip, TTM_SETDELAYTIME, TTDT_INITIAL, 150);
            }
        }
        /* 字体与字号 */
        SetDlgItemTextW(hdlg, IDC_EDIT_FONTNAME, g_cfg.fontName);
        SendDlgItemMessageW(hdlg, IDC_EDIT_FONTNAME, EM_LIMITTEXT,
                            LF_FACESIZE - 1, 0);
        SetDlgItemInt(hdlg, IDC_EDIT_FONTSIZE, (UINT)g_cfg.fontSize, FALSE);
        SetDlgItemInt(hdlg, IDC_EDIT_CDFONTSIZE, (UINT)g_cfg.cdFontSize, FALSE);

        /* 边距与行距 */
        SetDlgItemInt(hdlg, IDC_EDIT_PADDING, (UINT)g_cfg.padding, FALSE);
        SetDlgItemInt(hdlg, IDC_EDIT_LINESPACING, (UINT)g_cfg.lineSpacing, FALSE);

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
        HWND slider = GetDlgItem(hdlg, IDC_SLIDER_OPACITY);
        if (slider && (HWND)lp == slider) {
            int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
            WCHAR str[16];
            StringCchPrintfW(str, COUNT_OF(str), L"%d", pos);
            SetDlgItemTextW(hdlg, IDC_LABEL_OPACITY, str);
            if (s_ready) ApplyLive(hdlg, FALSE);   /* 拖动滑块实时生效 */
        }
        return TRUE;
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

        /* 编辑框内容变化 → 实时预览 */
        if (notify == EN_CHANGE && s_ready &&
            (id == IDC_EDIT_TEXT || id == IDC_EDIT_TARGET ||
             id == IDC_EDIT_FONTNAME || id == IDC_EDIT_FONTSIZE ||
             id == IDC_EDIT_CDFONTSIZE || id == IDC_EDIT_PADDING ||
             id == IDC_EDIT_LINESPACING)) {
            ApplyLive(hdlg, FALSE);
            return TRUE;
        }

        /* 显示模式单选 → 实时预览（模式变化需要重建窗口） */
        if (notify == BN_CLICKED && s_ready &&
            (id == IDC_RADIO_DESKTOP || id == IDC_RADIO_PASSTHROUGH ||
             id == IDC_RADIO_FLOAT)) {
            ApplyLive(hdlg, TRUE);
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

    static const int radioIds[] = { IDC_RADIO_DESKTOP, IDC_RADIO_PASSTHROUGH, IDC_RADIO_FLOAT };
    static const LPCWSTR radioLabels[] = { L"贴桌面", L"穿透置顶", L"浮动窗" };
    static const int colorStaticIds[] = { IDC_STATIC_TEXTCOLOR, IDC_STATIC_CDCOLOR, IDC_STATIC_BACKCOLOR };
    static const int colorBtnIds[] = { IDC_BTN_TEXTCOLOR, IDC_BTN_CDCOLOR, IDC_BTN_BACKCOLOR };
    static const LPCWSTR colorLabels[] = { L"文字颜色", L"倒计时颜色", L"背景颜色" };

    DlgBegin(&b, buffer, 284, 254, L"设置");

    /* ---- 显示模式 ---- */
    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0,
               8, 6, 268, 42, 0xFFFF, L"显示模式");
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_BUTTON,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                   (i == 0 ? WS_GROUP : 0), 0,
                   20 + i * 86, 22, 82, 12, radioIds[i], radioLabels[i]);
    }

    /* ---- 显示文字 ----（WS_GROUP 用于结束上面的单选按钮组） */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE | WS_GROUP, 0,
               12, 58, 56, 12, 0xFFFF, L"显示文字:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
               74, 56, 202, 14, IDC_EDIT_TEXT, NULL);

    /* ---- 目标时间 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 76, 56, 12, 0xFFFF, L"目标时间:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
               74, 74, 202, 14, IDC_EDIT_TARGET, NULL);

    /* ---- 字体名称 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 94, 56, 12, 0xFFFF, L"字体名称:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
               74, 92, 202, 14, IDC_EDIT_FONTNAME, NULL);

    /* ---- 字号 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 112, 56, 12, 0xFFFF, L"文字字号:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               74, 110, 52, 14, IDC_EDIT_FONTSIZE, NULL);
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               140, 112, 70, 12, 0xFFFF, L"倒计时字号:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               214, 110, 62, 14, IDC_EDIT_CDFONTSIZE, NULL);

    /* ---- 颜色（标签 / 预览 / 选择按钮） ---- */
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
                   12 + i * 92, 134, 88, 12, 0xFFFF, colorLabels[i]);
    }
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0,
                   12 + i * 92, 148, 28, 16, colorStaticIds[i], NULL);
    }
    for (i = 0; i < 3; i++) {
        DlgAddAtom(&b, ATOM_BUTTON,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                   44 + i * 92, 148, 40, 16, colorBtnIds[i], L"...");
    }

    /* ---- 背景不透明度 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 176, 70, 12, 0xFFFF, L"不透明度:");
    DlgAddClass(&b, TRACKBAR_CLASSW,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_BOTTOM, 0,
                86, 174, 154, 16, IDC_SLIDER_OPACITY, NULL);
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               246, 176, 30, 12, IDC_LABEL_OPACITY, L"150");

    /* ---- 内边距 / 行距 ---- */
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 200, 56, 12, 0xFFFF, L"内边距:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               74, 198, 52, 14, IDC_EDIT_PADDING, NULL);
    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               140, 200, 70, 12, 0xFFFF, L"行距:");
    DlgAddAtom(&b, ATOM_EDIT,
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               214, 198, 62, 14, IDC_EDIT_LINESPACING, NULL);

    /* ---- 确定 / 取消 ---- */
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
               96, 224, 72, 20, IDOK, L"确定");
    DlgAddAtom(&b, ATOM_BUTTON,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
               180, 224, 72, 20, IDCANCEL, L"取消");

    return DlgEnd(&b);
}

void ShowSettingsDialog(HWND parent)
{
    /* 初始化通用控件（滑块需要） */
    INITCOMMONCONTROLSEX icc;
    DLGTEMPLATE* dlgTemplate;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    dlgTemplate = CreateDialogTemplate();
    DialogBoxIndirectParamW(g_hInst, dlgTemplate, parent, SettingsDlgProc, 0);
}

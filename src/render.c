#include "lilith_timer.h"

HFONT CreateAppFont(int px)
{
    return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY,
                       DEFAULT_PITCH, g_cfg.fontName);
}

static void MeasureContent(HDC hdc, HFONT f1, HFONT f2, const WCHAR* cd,
                           int* w1, int* h1, int* w2, int* h2)
{
    RECT r = { 0, 0, 0, 0 };
    HGDIOBJ old;

    old = SelectObject(hdc, f1);
    if (g_cfg.text[0] != L'\0')
        DrawTextW(hdc, g_cfg.text, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    *w1 = r.right; *h1 = r.bottom;

    SelectObject(hdc, f2);
    SetRect(&r, 0, 0, 0, 0);
    DrawTextW(hdc, cd, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    *w2 = r.right; *h2 = r.bottom;

    SelectObject(hdc, old);
}

static void CalcWindowSize(HDC hdc, HFONT f1, HFONT f2, const WCHAR* cd,
                           int* outW, int* outH)
{
    int w1, h1, w2, h2;
    MeasureContent(hdc, f1, f2, cd, &w1, &h1, &w2, &h2);
    if (g_cfg.w > 0 && g_cfg.h > 0) {
        *outW = g_cfg.w;
        *outH = g_cfg.h;
    } else {
        *outW = (w1 > w2 ? w1 : w2) + g_cfg.padding * 2;
        *outH = h1 + g_cfg.lineSpacing + h2 + g_cfg.padding * 2;
    }
    *outW = ClampInt(*outW, 16, 8192);
    *outH = ClampInt(*outH, 16, 8192);
}

static void DrawContent(HDC hdc, HFONT f1, HFONT f2, const WCHAR* cd,
                        int W, int H, int* band1Top, int* band1Bot,
                        int* band2Top, int* band2Bot)
{
    int w1, h1, w2, h2, contentH, y;
    RECT rc;
    HGDIOBJ old;

    MeasureContent(hdc, f1, f2, cd, &w1, &h1, &w2, &h2);
    contentH = h1 + g_cfg.lineSpacing + h2;
    y = (H - contentH) / 2;
    if (y < 0) y = 0;

    SetBkMode(hdc, TRANSPARENT);

    old = SelectObject(hdc, f1);
    if (h1 > 0) {
        SetRect(&rc, 0, y, W, y + h1);
        SetTextColor(hdc, g_cfg.textColor);
        DrawTextW(hdc, g_cfg.text, -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        *band1Top = y; *band1Bot = y + h1;
    } else {
        *band1Top = *band1Bot = 0;
    }

    SelectObject(hdc, f2);
    SetRect(&rc, 0, y + h1 + g_cfg.lineSpacing, W, y + h1 + g_cfg.lineSpacing + h2);
    SetTextColor(hdc, g_cfg.cdColor);
    DrawTextW(hdc, cd, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    *band2Top = rc.top; *band2Bot = rc.bottom;

    SelectObject(hdc, old);
}

static void FixAlphaRow(DWORD* px, int n, COLORREF bg, COLORREF fg, int alpha)
{
    const int br = GetRValue(bg), bg_ = GetGValue(bg), bb = GetBValue(bg);
    const int fr = GetRValue(fg), fg_ = GetGValue(fg), fb = GetBValue(fg);
    const DWORD bgPre = ((DWORD)alpha << 24) |
                        (((br * alpha + 127) / 255) << 16) |
                        (((bg_ * alpha + 127) / 255) << 8) |
                        ((bb * alpha + 127) / 255);
    int dr = fr - br, dg = fg_ - bg_, db = fb - bb;
    int ch, d, bgc;
    int i;

    if (dr == 0 && dg == 0 && db == 0) {
        for (i = 0; i < n; i++) px[i] = bgPre;
        return;
    }
    if (abs(dr) >= abs(dg) && abs(dr) >= abs(db)) { ch = 0; d = dr; bgc = br; }
    else if (abs(dg) >= abs(db))                  { ch = 1; d = dg; bgc = bg_; }
    else                                          { ch = 2; d = db; bgc = bb; }

    for (i = 0; i < n; i++) {
        DWORD p = px[i];
        int ob = (int)(p & 0xFF), og = (int)((p >> 8) & 0xFF), or_ = (int)((p >> 16) & 0xFF);
        int outc = (ch == 0) ? or_ : (ch == 1) ? og : ob;
        double cov, A, inv;
        int ia, pr, pg, pb;

        if (outc == bgc) { px[i] = bgPre; continue; }

        cov = (double)(outc - bgc) / (double)d;
        if (cov < 0.0) cov = 0.0; else if (cov > 1.0) cov = 1.0;
        A   = alpha + cov * (255 - alpha);
        inv = 1.0 - cov;
        ia  = (int)(A + 0.5);
        pr  = (int)((fr * cov * 255.0 + br * alpha * inv) / 255.0 + 0.5);
        pg  = (int)((fg_ * cov * 255.0 + bg_ * alpha * inv) / 255.0 + 0.5);
        pb  = (int)((fb * cov * 255.0 + bb * alpha * inv) / 255.0 + 0.5);
        px[i] = ((DWORD)ia << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
    }
}

int RenderLayered(void)
{
    WCHAR cd[128];
    HDC hdcScreen, hdcMem;
    HFONT f1, f2;
    int W, H;
    BITMAPINFO bmi;
    void* bits = NULL;
    HBITMAP dib;
    HGDIOBJ oldBmp;
    int b1t, b1b, b2t, b2b, yy;
    POINT ptSrc = { 0, 0 };
    SIZE sz;
    BLENDFUNCTION blend;
    BOOL ok;

    if (!g_hwndTimer || !IsWindow(g_hwndTimer)) return RENDER_FAILED;

    BuildCountdownText(cd, COUNT_OF(cd));

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    f1 = CreateAppFont(g_cfg.fontSize);
    f2 = CreateAppFont(g_cfg.cdFontSize);

    CalcWindowSize(hdcMem, f1, f2, cd, &W, &H);

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib) {
        DeleteObject(f1); DeleteObject(f2);
        DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
        return RENDER_FAILED;
    }
    oldBmp = SelectObject(hdcMem, dib);

    {
        DWORD* px = (DWORD*)bits;
        int count = W * H, i;
        DWORD fill = ((DWORD)g_cfg.backOpacity << 24) |
                     ((DWORD)GetRValue(g_cfg.backColor) << 16) |
                     ((DWORD)GetGValue(g_cfg.backColor) << 8) |
                     (DWORD)GetBValue(g_cfg.backColor);
        for (i = 0; i < count; i++) px[i] = fill;
    }

    DrawContent(hdcMem, f1, f2, cd, W, H, &b1t, &b1b, &b2t, &b2b);

    for (yy = 0; yy < H; yy++) {
        DWORD* row = (DWORD*)bits + (size_t)yy * W;
        COLORREF fg;
        if (yy >= b1t && yy < b1b)      fg = g_cfg.textColor;
        else if (yy >= b2t && yy < b2b) fg = g_cfg.cdColor;
        else                            fg = g_cfg.backColor;
        FixAlphaRow(row, W, g_cfg.backColor, fg, g_cfg.backOpacity);
    }

    sz.cx = W; sz.cy = H;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    ok = UpdateLayeredWindow(g_hwndTimer, hdcScreen, NULL, &sz, hdcMem, &ptSrc,
                             0, &blend, ULW_ALPHA);
    if (!ok) {
        /* ULW 失败只记录一次，避免 500ms 定时器刷爆日志 */
        static BOOL s_ulwFailLogged = FALSE;
        DWORD err = GetLastError();
        if (!s_ulwFailLogged) {
            LONG exstyle = GetWindowLongW(g_hwndTimer, GWL_EXSTYLE);
            DbgLog(L"UpdateLayeredWindow 失败: err=%lu | mode=%d | size=(%d,%d) | exstyle=0x%08lX",
                   (unsigned long)err, (int)g_cfg.mode, W, H, (unsigned long)exstyle);
            s_ulwFailLogged = TRUE;
        }
    }

    SelectObject(hdcMem, oldBmp);
    DeleteObject(dib);
    DeleteObject(f1); DeleteObject(f2);
    DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);

    return ok ? RENDER_OK : RENDER_ULW_FAILED;
}

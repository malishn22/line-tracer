// LineTracer - shared GDI/GDI+ drawing helpers for the custom controls.
#include "ui_internal.h"

namespace ui {
namespace detail {

Gdiplus::Color gc(COLORREF c, BYTE a) { return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

static void roundPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillEllipse(HDC dc, float cx, float cy, float r, COLORREF fill, COLORREF border, float bw) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush b(gc(fill));
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
    if (border != CLR_NONE) {
        Gdiplus::Pen p(gc(border), bw);
        g.DrawEllipse(&p, cx - r, cy - r, r * 2, r * 2);
    }
}

void drawText(HDC dc, const wchar_t* t, RECT rc, COLORREF c, HFONT f, UINT flags) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, t, -1, &rc, flags | DT_NOPREFIX | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

std::wstring windowText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}

void notify(HWND h, UINT code) {
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), code), reinterpret_cast<LPARAM>(h));
}

void trackLeave(HWND h) {
    TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
    TrackMouseEvent(&t);
}

void redraw(HWND h) { InvalidateRect(h, nullptr, FALSE); }

}  // namespace detail

void fillRound(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath p;
    roundPath(p, rc.left + 0.5f, rc.top + 0.5f, float(rc.right - rc.left) - 1, float(rc.bottom - rc.top) - 1, float(radius));
    Gdiplus::SolidBrush b(gc(fill));
    g.FillPath(&b, &p);
    if (border != CLR_NONE) {
        Gdiplus::Pen pen(gc(border), 1.0f);
        g.DrawPath(&pen, &p);
    }
}

}  // namespace ui

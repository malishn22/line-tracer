#include "ui_controls.h"

#include <objidl.h>

#include <algorithm>
#include <string>
#include <vector>

using std::max;
using std::min;
#include <commctrl.h>
#include <gdiplus.h>

#include "ui_theme.h"

namespace ui {
namespace {

ULONG_PTR g_gdip = 0;
HINSTANCE g_inst = nullptr;

Gdiplus::Color gc(COLORREF c, BYTE a = 255) { return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

void roundPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillEllipse(HDC dc, float cx, float cy, float r, COLORREF fill, COLORREF border = CLR_NONE, float bw = 1.5f) {
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

// Double-buffered WM_PAINT: `body` draws into a memory DC the size of the client area, pre-filled with `bg`.
template <class F>
void paintBuffered(HWND h, COLORREF bg, F body) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(mem, &rc, br);
    DeleteObject(br);
    body(mem, rc);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

void notify(HWND h, UINT code) {
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), code), reinterpret_cast<LPARAM>(h));
}

void trackLeave(HWND h) {
    TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
    TrackMouseEvent(&t);
}

template <class T>
T* data(HWND h) { return reinterpret_cast<T*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

template <class T>
void attach(HWND h) { SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new T())); }

template <class T>
void detach(HWND h) {
    delete data<T>(h);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
}

void redraw(HWND h) { InvalidateRect(h, nullptr, FALSE); }

// ---------------- button ----------------

struct BtnData { BtnKind kind = BtnKind::Secondary; bool hover = false, down = false; };

LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    BtnData* d = data<BtnData>(h);
    switch (m) {
    case WM_NCCREATE: attach<BtnData>(h); break;
    case WM_NCDESTROY: detach<BtnData>(h); break;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_BUTTON;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        redraw(h);
        return r;
    }
    case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: redraw(h); return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        bool en = IsWindowEnabled(h) != FALSE, focus = GetFocus() == h;
        paintBuffered(h, t.panel, [&](HDC dc, RECT rc) {
            COLORREF fill, txt, border = CLR_NONE;
            bool press = d->down && d->hover, hot = d->hover;
            if (!en) {
                fill = t.field; txt = t.textDim; border = t.border;
            } else if (d->kind == BtnKind::Primary) {
                fill = press ? t.accentPress : hot ? t.accentHover : t.accent; txt = t.accentText;
            } else if (d->kind == BtnKind::Danger) {
                fill = press ? t.dangerPress : hot ? t.dangerHover : t.danger; txt = t.accentText;
            } else {
                fill = press ? t.track : hot ? t.fieldHover : t.field; txt = t.text; border = t.border;
            }
            if (focus && en) border = t.accent;
            fillRound(dc, rc, S(8), fill, border);
            drawText(dc, windowText(h).c_str(), rc, txt, fontSemibold(), DT_CENTER);
        });
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!d->hover) { d->hover = true; trackLeave(h); redraw(h); }
        return 0;
    case WM_MOUSELEAVE: d->hover = false; redraw(h); return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(h)) { SetFocus(h); SetCapture(h); d->down = true; redraw(h); }
        return 0;
    case WM_LBUTTONUP:
        if (d->down) {
            ReleaseCapture();
            d->down = false;
            RECT rc;
            GetClientRect(h, &rc);
            POINT p{short(LOWORD(l)), short(HIWORD(l))};
            redraw(h);
            if (PtInRect(&rc, p)) notify(h, BN_CLICKED);
        }
        return 0;
    case WM_CAPTURECHANGED: d->down = false; redraw(h); return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE) { d->down = d->hover = true; redraw(h); }
        return 0;
    case WM_KEYUP:
        if (w == VK_SPACE && d->down) { d->down = d->hover = false; redraw(h); notify(h, BN_CLICKED); }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- toggle switch ----------------

struct ToggleData { bool checked = false, hover = false; };

LRESULT CALLBACK ToggleProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ToggleData* d = data<ToggleData>(h);
    auto flip = [&] { d->checked = !d->checked; redraw(h); notify(h, BN_CLICKED); };
    switch (m) {
    case WM_NCCREATE: attach<ToggleData>(h); break;
    case WM_NCDESTROY: detach<ToggleData>(h); break;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_BUTTON;
    case BM_GETCHECK: return d->checked ? BST_CHECKED : BST_UNCHECKED;
    case BM_SETCHECK: d->checked = w == BST_CHECKED; redraw(h); return 0;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        redraw(h);
        return r;
    }
    case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: redraw(h); return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        bool en = IsWindowEnabled(h) != FALSE, focus = GetFocus() == h;
        paintBuffered(h, t.panel, [&](HDC dc, RECT rc) {
            int tw = S(36), th = S(20), cy = rc.bottom / 2;
            RECT tr{0, cy - th / 2, tw, cy + th / 2};
            COLORREF on = en ? t.accent : t.track;
            fillRound(dc, tr, th / 2, d->checked ? on : (d->hover && en ? t.fieldHover : t.track), focus && en ? t.accent : CLR_NONE);
            float kr = th / 2.0f - S(3);
            float kx = d->checked ? tr.right - th / 2.0f : tr.left + th / 2.0f;
            fillEllipse(dc, kx, float(cy), kr, d->checked ? t.accentText : t.text);
            RECT lr{tw + S(10), 0, rc.right, rc.bottom};
            drawText(dc, windowText(h).c_str(), lr, en ? t.text : t.textDim, font(), DT_LEFT | DT_END_ELLIPSIS);
        });
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!d->hover) { d->hover = true; trackLeave(h); redraw(h); }
        return 0;
    case WM_MOUSELEAVE: d->hover = false; redraw(h); return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(h)) { SetFocus(h); flip(); }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE && IsWindowEnabled(h)) flip();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- slider ----------------

struct SliderData { int lo = 0, hi = 100, pos = 0, page = 10; bool drag = false, hover = false; };

int sliderPad() { return S(9); }

int sliderPosFromX(HWND h, const SliderData* d, int x) {
    RECT rc;
    GetClientRect(h, &rc);
    int a = sliderPad(), b = rc.right - sliderPad();
    if (b <= a || d->hi <= d->lo) return d->lo;
    double f = double(std::clamp(x, a, b) - a) / double(b - a);
    return d->lo + int(f * (d->hi - d->lo) + 0.5);
}

LRESULT CALLBACK SliderProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SliderData* d = data<SliderData>(h);
    auto setPos = [&](int p) {
        p = std::clamp(p, d->lo, d->hi);
        bool changed = p != d->pos;
        d->pos = p;
        if (changed) redraw(h);
        return changed;
    };
    auto send = [&](UINT code) { SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(code, d->pos), reinterpret_cast<LPARAM>(h)); };
    switch (m) {
    case WM_NCCREATE: attach<SliderData>(h); break;
    case WM_NCDESTROY: detach<SliderData>(h); break;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case TBM_SETRANGE: d->lo = short(LOWORD(l)); d->hi = short(HIWORD(l)); setPos(d->pos); redraw(h); return 0;
    case TBM_SETPOS: setPos(int(l)); return 0;
    case TBM_GETPOS: return d->pos;
    case TBM_GETRANGEMIN: return d->lo;
    case TBM_GETRANGEMAX: return d->hi;
    case TBM_SETPAGESIZE: { int old = d->page; d->page = int(l); return old; }
    case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: redraw(h); return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        bool en = IsWindowEnabled(h) != FALSE, focus = GetFocus() == h;
        paintBuffered(h, t.panel, [&](HDC dc, RECT rc) {
            int pad = sliderPad(), cy = rc.bottom / 2, th = S(4);
            double f = d->hi > d->lo ? double(d->pos - d->lo) / (d->hi - d->lo) : 0.0;
            int x = pad + int(f * (rc.right - 2 * pad) + 0.5);
            RECT tr{pad, cy - th / 2, rc.right - pad, cy + th / 2};
            fillRound(dc, tr, th / 2, t.track);
            RECT fr{pad, tr.top, std::max<int>(x, pad + th), tr.bottom};
            fillRound(dc, fr, th / 2, en ? t.accent : t.textDim);
            float r = float(d->drag || d->hover ? S(8) : S(7));
            fillEllipse(dc, float(x), float(cy), r, en ? t.accentText : t.field, en ? (focus ? t.accentHover : t.accent) : t.border, float(S(2)));
        });
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!d->hover) { d->hover = true; trackLeave(h); redraw(h); }
        if (d->drag && setPos(sliderPosFromX(h, d, int(short(LOWORD(l)))))) send(TB_THUMBTRACK);
        return 0;
    case WM_MOUSELEAVE: d->hover = false; redraw(h); return 0;
    case WM_LBUTTONDOWN:
        if (!IsWindowEnabled(h)) return 0;
        SetFocus(h);
        SetCapture(h);
        d->drag = true;
        setPos(sliderPosFromX(h, d, int(short(LOWORD(l)))));
        redraw(h);
        send(TB_THUMBTRACK);  // always, so the app learns the drag started even if the value did not change
        return 0;
    case WM_LBUTTONUP:
        if (d->drag) {
            ReleaseCapture();
            d->drag = false;
            redraw(h);
            send(TB_ENDTRACK);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (d->drag) { d->drag = false; redraw(h); send(TB_ENDTRACK); }
        return 0;
    case WM_KEYDOWN: {
        int p = d->pos;
        switch (w) {
        case VK_LEFT: case VK_DOWN: p -= 1; break;
        case VK_RIGHT: case VK_UP: p += 1; break;
        case VK_PRIOR: p += d->page; break;
        case VK_NEXT: p -= d->page; break;
        case VK_HOME: p = d->lo; break;
        case VK_END: p = d->hi; break;
        default: return 0;
        }
        if (setPos(p)) send(TB_LINEUP);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- combo + its popup list ----------------

const UINT LTP_KEY = WM_APP + 1;       // combo -> popup: forwarded key (wParam = VK)
const UINT LTC_CLOSED = WM_APP + 2;    // popup -> combo: popup went away

struct ComboData {
    std::vector<std::wstring> items;
    int sel = -1;
    bool hover = false;
    HWND popup = nullptr;
};

struct PopupData { HWND combo = nullptr; int hot = -1; };

int itemH() { return S(28); }
int popupPad() { return S(4); }

void closePopup(HWND combo) {
    ComboData* d = data<ComboData>(combo);
    if (d && d->popup) DestroyWindow(d->popup);
}

void commit(HWND popup, int idx) {
    PopupData* p = data<PopupData>(popup);
    HWND combo = p->combo;
    ComboData* d = data<ComboData>(combo);
    DestroyWindow(popup);
    if (idx >= 0 && idx < int(d->items.size()) && idx != d->sel) {
        d->sel = idx;
        redraw(combo);
        notify(combo, CBN_SELCHANGE);
    }
}

LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PopupData* p = data<PopupData>(h);
    switch (m) {
    case WM_NCCREATE: {
        attach<PopupData>(h);
        data<PopupData>(h)->combo = static_cast<HWND>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        break;
    }
    case WM_NCDESTROY: detach<PopupData>(h); break;
    case WM_CREATE:
        SetCapture(h);  // outside clicks arrive here and dismiss the list
        return 0;
    case WM_DESTROY:
        if (GetCapture() == h) ReleaseCapture();
        SendMessageW(p->combo, LTC_CLOSED, 0, 0);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_PAINT: {
        const Theme& t = theme();
        ComboData* cd = data<ComboData>(p->combo);
        paintBuffered(h, t.window, [&](HDC dc, RECT rc) {
            fillRound(dc, rc, S(8), t.panel, t.border);
            for (size_t i = 0; i < cd->items.size(); ++i) {
                RECT ir{popupPad(), popupPad() + int(i) * itemH(), rc.right - popupPad(), popupPad() + int(i + 1) * itemH()};
                if (int(i) == p->hot) fillRound(dc, ir, S(6), t.fieldHover);
                RECT tr = ir;
                tr.left += S(10);
                tr.right -= S(24);
                drawText(dc, cd->items[i].c_str(), tr, t.text, int(i) == cd->sel ? fontSemibold() : font(), DT_LEFT | DT_END_ELLIPSIS);
                if (int(i) == cd->sel) fillEllipse(dc, float(ir.right - S(12)), float((ir.top + ir.bottom) / 2), float(S(3)), t.accent);
            }
        });
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt{short(LOWORD(l)), short(HIWORD(l))};
        RECT rc;
        GetClientRect(h, &rc);
        int hot = PtInRect(&rc, pt) ? (pt.y - popupPad()) / itemH() : -1;
        if (pt.y < popupPad()) hot = -1;
        if (hot != p->hot) { p->hot = hot; redraw(h); }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{short(LOWORD(l)), short(HIWORD(l))};
        RECT rc;
        GetClientRect(h, &rc);
        int idx = PtInRect(&rc, pt) && pt.y >= popupPad() ? (pt.y - popupPad()) / itemH() : -1;
        commit(h, idx);  // idx -1 (outside the list) just closes
        return 0;
    }
    case LTP_KEY: {
        ComboData* cd = data<ComboData>(p->combo);
        int n = int(cd->items.size());
        if (w == VK_DOWN) p->hot = std::min(n - 1, p->hot + 1);
        else if (w == VK_UP) p->hot = std::max(0, p->hot < 0 ? cd->sel : p->hot - 1);
        else if (w == VK_RETURN || w == VK_SPACE) { commit(h, p->hot); return 0; }
        else if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
        redraw(h);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

void openPopup(HWND combo) {
    ComboData* d = data<ComboData>(combo);
    if (d->popup || d->items.empty()) return;
    notify(combo, CBN_DROPDOWN);  // lets the app refresh the list first
    if (d->items.empty()) return;
    RECT rc;
    GetWindowRect(combo, &rc);
    int w = rc.right - rc.left, hgt = int(d->items.size()) * itemH() + 2 * popupPad();
    HMONITOR mon = MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    int y = rc.bottom + S(4);
    if (y + hgt > mi.rcWork.bottom) y = rc.top - S(4) - hgt;
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"LTPopup", L"", WS_POPUP, rc.left, y, w, hgt,
                                 GetAncestor(combo, GA_ROOT), nullptr, g_inst, combo);
    if (!popup) return;
    d->popup = popup;
    PopupData* p = data<PopupData>(popup);
    p->hot = d->sel;
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    redraw(combo);
}

LRESULT CALLBACK ComboProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ComboData* d = data<ComboData>(h);
    switch (m) {
    case WM_NCCREATE: attach<ComboData>(h); break;
    case WM_NCDESTROY: detach<ComboData>(h); break;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case CB_ADDSTRING: d->items.emplace_back(reinterpret_cast<const wchar_t*>(l)); return LRESULT(d->items.size() - 1);
    case CB_RESETCONTENT: d->items.clear(); d->sel = -1; redraw(h); return 0;
    case CB_GETCOUNT: return LRESULT(d->items.size());
    case CB_GETCURSEL: return d->sel;
    case CB_SETCURSEL: d->sel = (int(w) >= 0 && int(w) < int(d->items.size())) ? int(w) : -1; redraw(h); return d->sel;
    case LTC_CLOSED: d->popup = nullptr; redraw(h); return 0;
    case WM_ENABLE: case WM_SETFOCUS: redraw(h); return 0;
    case WM_KILLFOCUS: closePopup(h); redraw(h); return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        bool en = IsWindowEnabled(h) != FALSE, focus = GetFocus() == h;
        paintBuffered(h, t.panel, [&](HDC dc, RECT rc) {
            fillRound(dc, rc, S(8), en ? (d->hover || d->popup ? t.fieldHover : t.field) : t.field,
                      (d->popup || (focus && en)) ? t.accent : t.border);
            RECT tr = rc;
            tr.left += S(12);
            tr.right -= S(30);
            const wchar_t* s = d->sel >= 0 ? d->items[size_t(d->sel)].c_str() : L"";
            drawText(dc, s, tr, en ? t.text : t.textDim, font(), DT_LEFT | DT_END_ELLIPSIS);
            // chevron
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Pen pen(gc(en ? t.textDim : t.track), 1.6f);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            float cx = float(rc.right - S(16)), cy = rc.bottom / 2.0f, a = float(S(4));
            g.DrawLine(&pen, cx - a, cy - a / 2, cx, cy + a / 2);
            g.DrawLine(&pen, cx, cy + a / 2, cx + a, cy - a / 2);
        });
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!d->hover) { d->hover = true; trackLeave(h); redraw(h); }
        return 0;
    case WM_MOUSELEAVE: d->hover = false; redraw(h); return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(h)) {
            SetFocus(h);
            if (d->popup) closePopup(h);
            else openPopup(h);
        }
        return 0;
    case WM_KEYDOWN: {
        if (!IsWindowEnabled(h)) return 0;
        if (d->popup) { SendMessageW(d->popup, LTP_KEY, w, 0); return 0; }
        int n = int(d->items.size());
        int s = d->sel;
        if (w == VK_DOWN && (GetKeyState(VK_MENU) & 0x8000)) { openPopup(h); return 0; }
        if (w == VK_SPACE || w == VK_F4) { openPopup(h); return 0; }
        if (w == VK_DOWN) s = std::min(n - 1, s + 1);
        else if (w == VK_UP) s = std::max(0, s - 1);
        else return 0;
        if (s != d->sel && n > 0) { d->sel = s; redraw(h); notify(h, CBN_SELCHANGE); }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- hotkey capture ----------------

// Same contract as the stock hotkey control: HKM_SETHOTKEY(MAKEWORD(vk, HOTKEYF_*)), HKM_GETHOTKEY, EN_CHANGE.
// While focused it shows the held modifiers, and commits once a non-modifier key goes down.
struct HotkeyData { unsigned vk = 0, mods = 0; bool hover = false; };

unsigned heldMods() {
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? HOTKEYF_SHIFT : 0) | ((GetKeyState(VK_CONTROL) & 0x8000) ? HOTKEYF_CONTROL : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? HOTKEYF_ALT : 0);
}

std::wstring hotkeyLabel(unsigned vk, unsigned mods, bool extended) {
    std::wstring s;
    if (mods & HOTKEYF_CONTROL) s += L"Ctrl + ";
    if (mods & HOTKEYF_ALT) s += L"Alt + ";
    if (mods & HOTKEYF_SHIFT) s += L"Shift + ";
    if (vk) {
        wchar_t key[64] = {};
        LONG sc = LONG(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
        if (extended) sc |= 1 << 24;
        if (GetKeyNameTextW(sc, key, 64) <= 0) swprintf_s(key, L"0x%02X", vk);
        s += key;
    }
    return s;
}

LRESULT CALLBACK HotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    HotkeyData* d = data<HotkeyData>(h);
    switch (m) {
    case WM_NCCREATE: attach<HotkeyData>(h); break;
    case WM_NCDESTROY: detach<HotkeyData>(h); break;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS;  // Tab, Enter and Esc still reach the dialog
    case HKM_SETHOTKEY: d->vk = LOBYTE(LOWORD(w)); d->mods = HIBYTE(LOWORD(w)); redraw(h); return 0;
    case HKM_GETHOTKEY: return MAKEWORD(d->vk, d->mods);
    case WM_SETFOCUS: case WM_KILLFOCUS: redraw(h); return 0;
    case WM_LBUTTONDOWN: SetFocus(h); return 0;
    case WM_MOUSEMOVE:
        if (!d->hover) { d->hover = true; trackLeave(h); redraw(h); }
        return 0;
    case WM_MOUSELEAVE: d->hover = false; redraw(h); return 0;
    case WM_CHAR: case WM_SYSCHAR: return 0;  // no beeps
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (w == VK_SHIFT || w == VK_CONTROL || w == VK_MENU || w == VK_LWIN || w == VK_RWIN) {
            d->vk = 0;
            d->mods = heldMods();
            redraw(h);
            notify(h, EN_CHANGE);  // vk == 0: the app ignores it until a real key arrives
            return 0;
        }
        d->vk = unsigned(w);
        d->mods = heldMods();
        redraw(h);
        notify(h, EN_CHANGE);
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP:
        if (w == VK_SHIFT || w == VK_CONTROL || w == VK_MENU) {
            if (d->vk == 0) { d->mods = heldMods(); redraw(h); }  // releasing modifiers without a key clears the preview
        }
        return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        bool focus = GetFocus() == h;
        paintBuffered(h, t.panel, [&](HDC dc, RECT rc) {
            fillRound(dc, rc, S(8), t.field, focus ? t.accent : (d->hover ? t.textDim : t.border));
            RECT tr = rc;
            tr.left += S(12);
            tr.right -= S(8);
            std::wstring s = hotkeyLabel(d->vk, d->mods, false);
            if (s.empty()) drawText(dc, focus ? L"Press a key combination..." : L"None", tr, t.textDim, font(), DT_LEFT);
            else drawText(dc, s.c_str(), tr, t.text, fontSemibold(), DT_LEFT | DT_END_ELLIPSIS);
        });
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------- label ----------------

LRESULT CALLBACK LabelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        redraw(h);
        return r;
    }
    case WM_ENABLE: redraw(h); return 0;
    case WM_PAINT: {
        const Theme& t = theme();
        unsigned f = unsigned(GetWindowLongPtrW(h, GWLP_USERDATA));
        bool en = IsWindowEnabled(h) != FALSE;
        paintBuffered(h, (f & LF_WINDOW) ? t.window : t.panel, [&](HDC dc, RECT rc) {
            UINT fl = (f & LF_RIGHT) ? DT_RIGHT : DT_LEFT;
            if (f & LF_ELLIPSIS) fl |= DT_END_ELLIPSIS;
            drawText(dc, windowText(h).c_str(), rc, (f & LF_DIM) || !en ? t.textDim : t.text, (f & LF_BOLD) ? fontSemibold() : font(), fl);
        });
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

void registerOne(const wchar_t* name, WNDPROC proc, LPCWSTR cursor = IDC_ARROW, UINT style = 0) {
    WNDCLASSW wc{};
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, cursor);
    wc.lpszClassName = name;
    RegisterClassW(&wc);
}

HWND create(const wchar_t* cls, HWND parent, int id, const wchar_t* text, DWORD style) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
}

}  // namespace

void startup(HINSTANCE inst) {
    g_inst = inst;
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&g_gdip, &in, nullptr);
    registerOne(L"LTButton", ButtonProc, IDC_HAND);
    registerOne(L"LTToggle", ToggleProc, IDC_HAND);
    registerOne(L"LTSlider", SliderProc, IDC_HAND);
    registerOne(L"LTCombo", ComboProc, IDC_HAND);
    registerOne(L"LTLabel", LabelProc);
    registerOne(L"LTHotkey", HotkeyProc, IDC_IBEAM);
    registerOne(L"LTPopup", PopupProc, IDC_ARROW, CS_DROPSHADOW);
}

void shutdown() {
    if (g_gdip) Gdiplus::GdiplusShutdown(g_gdip);
    g_gdip = 0;
}

HWND createButton(HWND parent, int id, const wchar_t* text, BtnKind kind) {
    HWND h = create(L"LTButton", parent, id, text, WS_TABSTOP);
    setButtonKind(h, kind);
    return h;
}

void setButtonKind(HWND h, BtnKind kind) {
    if (BtnData* d = data<BtnData>(h)) {
        d->kind = kind;
        redraw(h);
    }
}

HWND createToggle(HWND parent, int id, const wchar_t* text) { return create(L"LTToggle", parent, id, text, WS_TABSTOP); }

HWND createSlider(HWND parent, int id, int lo, int hi, int pos) {
    HWND h = create(L"LTSlider", parent, id, L"", WS_TABSTOP);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
    SendMessageW(h, TBM_SETPOS, TRUE, pos);
    return h;
}

HWND createCombo(HWND parent, int id) { return create(L"LTCombo", parent, id, L"", WS_TABSTOP); }

HWND createHotkey(HWND parent, int id) { return create(L"LTHotkey", parent, id, L"", WS_TABSTOP); }

HWND createLabel(HWND parent, int id, const wchar_t* text, unsigned flags) {
    HWND h = create(L"LTLabel", parent, id, text, 0);
    SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(flags));
    return h;
}

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

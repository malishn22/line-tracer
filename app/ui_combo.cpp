// LineTracer - drop-down combo box and its popup list.
#include "ui_internal.h"

namespace ui {
namespace {

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
                                 GetAncestor(combo, GA_ROOT), nullptr, instance(), combo);
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
            fillRound(dc, rc, S(7), en ? (d->hover || d->popup ? t.fieldHover : t.field) : t.field,
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

}  // namespace

namespace detail {
void registerCombo() {
    registerOne(L"LTCombo", ComboProc, IDC_HAND);
    registerOne(L"LTPopup", PopupProc, IDC_ARROW, CS_DROPSHADOW);
}
}  // namespace detail

HWND createCombo(HWND parent, int id) { return create(L"LTCombo", parent, id, L"", WS_TABSTOP); }

}  // namespace ui

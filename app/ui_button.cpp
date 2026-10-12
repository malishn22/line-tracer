// LineTracer - push button and toggle switch.
#include "ui_internal.h"

namespace ui {
namespace {

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
            fillRound(dc, rc, S(7), fill, border);
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

}  // namespace

namespace detail {
void registerButtons() {
    registerOne(L"LTButton", ButtonProc, IDC_HAND);
    registerOne(L"LTToggle", ToggleProc, IDC_HAND);
}
}  // namespace detail

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

}  // namespace ui

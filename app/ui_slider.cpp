// LineTracer - slider (TBM_* / WM_HSCROLL contract).
#include "ui_internal.h"

namespace ui {
namespace {

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

}  // namespace

namespace detail {
void registerSlider() {
    registerOne(L"LTSlider", SliderProc, IDC_HAND);
}
}  // namespace detail

HWND createSlider(HWND parent, int id, int lo, int hi, int pos) {
    HWND h = create(L"LTSlider", parent, id, L"", WS_TABSTOP);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
    SendMessageW(h, TBM_SETPOS, TRUE, pos);
    return h;
}

}  // namespace ui

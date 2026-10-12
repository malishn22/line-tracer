// LineTracer - hotkey capture field and static label.
#include "ui_internal.h"

namespace ui {
namespace {

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

}  // namespace

namespace detail {
void registerText() {
    registerOne(L"LTLabel", LabelProc);
    registerOne(L"LTHotkey", HotkeyProc, IDC_IBEAM);
}
}  // namespace detail

HWND createHotkey(HWND parent, int id) { return create(L"LTHotkey", parent, id, L"", WS_TABSTOP); }

HWND createLabel(HWND parent, int id, const wchar_t* text, unsigned flags) {
    HWND h = create(L"LTLabel", parent, id, text, 0);
    SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(flags));
    return h;
}

}  // namespace ui

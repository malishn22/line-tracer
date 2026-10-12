// LineTracer - helpers shared by the ui_*.cpp control implementations. Not part of the public API.
#pragma once
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
namespace detail {

Gdiplus::Color gc(COLORREF c, BYTE a = 255);
void fillEllipse(HDC dc, float cx, float cy, float r, COLORREF fill, COLORREF border = CLR_NONE, float bw = 1.5f);
void drawText(HDC dc, const wchar_t* t, RECT rc, COLORREF c, HFONT f, UINT flags);
std::wstring windowText(HWND h);
void notify(HWND h, UINT code);  // WM_COMMAND to the parent, like a stock control
void trackLeave(HWND h);
void redraw(HWND h);

HINSTANCE instance();
void registerOne(const wchar_t* name, WNDPROC proc, LPCWSTR cursor = IDC_ARROW, UINT style = 0);
HWND create(const wchar_t* cls, HWND parent, int id, const wchar_t* text, DWORD style);

// One per control family; ui::startup() calls each.
void registerButtons();  // LTButton, LTToggle
void registerSlider();   // LTSlider
void registerCombo();    // LTCombo, LTPopup
void registerText();     // LTLabel, LTHotkey

// Double-buffered WM_PAINT: `body` draws into a memory DC the size of the client area, pre-filled with `bg`.
template <class F>
inline void paintBuffered(HWND h, COLORREF bg, F body) {
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

template <class T>
T* data(HWND h) { return reinterpret_cast<T*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

template <class T>
inline void attach(HWND h) { SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new T())); }

template <class T>
inline void detach(HWND h) {
    delete data<T>(h);
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
}

}  // namespace detail
using namespace detail;
}  // namespace ui

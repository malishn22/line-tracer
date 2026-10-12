// LineTracer - custom-drawn controls: GDI+ lifetime, window-class registration and shared creation.
// The controls themselves live in ui_button / ui_slider / ui_combo / ui_text.
#include "ui_internal.h"

namespace ui {
namespace {

ULONG_PTR g_gdip = 0;
HINSTANCE g_inst = nullptr;

}  // namespace

namespace detail {

HINSTANCE instance() { return g_inst; }

void registerOne(const wchar_t* name, WNDPROC proc, LPCWSTR cursor, UINT style) {
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

}  // namespace detail

void startup(HINSTANCE inst) {
    g_inst = inst;
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&g_gdip, &in, nullptr);
    registerButtons();
    registerSlider();
    registerCombo();
    registerText();
}

void shutdown() {
    if (g_gdip) Gdiplus::GdiplusShutdown(g_gdip);
    g_gdip = 0;
}

}  // namespace ui

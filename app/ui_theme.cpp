#include "ui_theme.h"

#include <dwmapi.h>

#include <initializer_list>

namespace ui {
namespace {

const Theme kDark = {
    true,
    RGB(24, 25, 29), RGB(33, 34, 40), RGB(54, 56, 66), RGB(232, 234, 239), RGB(146, 150, 162),
    RGB(44, 46, 55), RGB(56, 59, 70), RGB(64, 67, 80),
    RGB(88, 130, 255), RGB(112, 148, 255), RGB(70, 108, 235), RGB(255, 255, 255),
    RGB(235, 84, 84), RGB(244, 108, 108), RGB(214, 66, 66),
    RGB(0, 0, 0), RGB(70, 70, 80), RGB(255, 255, 255), RGB(255, 90, 70), RGB(255, 210, 60),
};

const Theme kLight = {
    false,
    RGB(240, 242, 247), RGB(255, 255, 255), RGB(222, 225, 233), RGB(28, 30, 36), RGB(104, 109, 122),
    RGB(238, 240, 246), RGB(226, 230, 240), RGB(208, 213, 225),
    RGB(54, 98, 240), RGB(76, 116, 246), RGB(40, 82, 215), RGB(255, 255, 255),
    RGB(215, 58, 58), RGB(228, 80, 80), RGB(190, 44, 44),
    RGB(0, 0, 0), RGB(70, 70, 80), RGB(255, 255, 255), RGB(255, 90, 70), RGB(255, 210, 60),
};

Mode g_mode = Mode::Auto;
const Theme* g_theme = &kDark;
int g_dpi = 96;
HFONT g_font = nullptr, g_semi = nullptr, g_large = nullptr;
HBRUSH g_window = nullptr, g_panel = nullptr, g_field = nullptr;

bool systemPrefersDark() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

void replaceBrush(HBRUSH& b, COLORREF c) {
    if (b) DeleteObject(b);
    b = CreateSolidBrush(c);
}

HFONT makeFont(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

}  // namespace

void setMode(Mode m) {
    g_mode = m;
    bool dark = m == Mode::Dark || (m == Mode::Auto && systemPrefersDark());
    g_theme = dark ? &kDark : &kLight;
    replaceBrush(g_window, g_theme->window);
    replaceBrush(g_panel, g_theme->panel);
    replaceBrush(g_field, g_theme->field);
}

Mode mode() { return g_mode; }
const Theme& theme() { return *g_theme; }

void setDpi(int dpi) {
    g_dpi = dpi;
    for (HFONT* f : std::initializer_list<HFONT*>{&g_font, &g_semi, &g_large})
        if (*f) DeleteObject(*f);
    g_font = makeFont(9, FW_NORMAL);
    g_semi = makeFont(9, FW_SEMIBOLD);
    g_large = makeFont(13, FW_SEMIBOLD);
}

int dpi() { return g_dpi; }
int S(int v) { return MulDiv(v, g_dpi, 96); }

HFONT font() { return g_font; }
HFONT fontSemibold() { return g_semi; }
HFONT fontLarge() { return g_large; }

HBRUSH windowBrush() { return g_window; }
HBRUSH panelBrush() { return g_panel; }
HBRUSH fieldBrush() { return g_field; }

void applyChrome(HWND h, COLORREF caption) {
    const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ = 20, DWMWA_CAPTION_COLOR_ = 35, DWMWA_TEXT_COLOR_ = 36;
    BOOL dark = g_theme->dark ? TRUE : FALSE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE_, &dark, sizeof(dark));
    DwmSetWindowAttribute(h, DWMWA_CAPTION_COLOR_, &caption, sizeof(caption));
    COLORREF text = g_theme->text;
    DwmSetWindowAttribute(h, DWMWA_TEXT_COLOR_, &text, sizeof(text));
}

}  // namespace ui

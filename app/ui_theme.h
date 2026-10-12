// LineTracer - UI theme: light/dark palettes, DPI-scaled metrics, fonts, window chrome.
#pragma once
#include <windows.h>

namespace ui {

struct Theme {
    bool dark;
    COLORREF window, panel, border, text, textDim;
    COLORREF field, fieldHover, track;
    COLORREF accent, accentHover, accentPress, accentText;
    COLORREF danger, dangerHover, dangerPress;
    // The canvas stays dark in both themes: the glowing trace needs a black background.
    COLORREF canvasBg, canvasFrame, trail, marker, hover;
};

enum class Mode { Auto = 0, Light = 1, Dark = 2 };

// Picks the palette for `m` (Auto follows the Windows "app mode" setting) and rebuilds the brushes.
void setMode(Mode m);
Mode mode();
const Theme& theme();

void setDpi(int dpi);  // rebuilds the fonts
int dpi();
int S(int v);          // 96-dpi units -> pixels

HFONT font();
HFONT fontSemibold();
HFONT fontLarge();

HBRUSH windowBrush();
HBRUSH panelBrush();
HBRUSH fieldBrush();

// Dark title bar (and a caption colour matching `caption`) on Windows 10 20H1+/11; silently ignored elsewhere.
void applyChrome(HWND h, COLORREF caption);

}  // namespace ui

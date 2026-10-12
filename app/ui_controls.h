// LineTracer - custom-drawn controls (button, toggle, slider, combo, label) that paint from ui::Theme.
// They answer the same messages as the stock controls they replace (BM_*, TBM_*, CB_*, WM_HSCROLL and
// WM_COMMAND notifications), so the app code that drives them barely changes.
#pragma once
#include <windows.h>
#include <commctrl.h>  // CLR_NONE

namespace ui {

enum class BtnKind { Secondary, Primary, Danger };
enum LabelFlags { LF_WINDOW = 1, LF_DIM = 2, LF_RIGHT = 4, LF_ELLIPSIS = 8, LF_BOLD = 16 };

void startup(HINSTANCE inst);  // GDI+ and window classes; call once before creating any control
void shutdown();

HWND createButton(HWND parent, int id, const wchar_t* text, BtnKind kind = BtnKind::Secondary);
void setButtonKind(HWND h, BtnKind kind);
HWND createToggle(HWND parent, int id, const wchar_t* text);  // BM_GETCHECK / BM_SETCHECK, BN_CLICKED
HWND createSlider(HWND parent, int id, int lo, int hi, int pos);  // TBM_*, WM_HSCROLL (TB_THUMBTRACK / TB_ENDTRACK)
HWND createCombo(HWND parent, int id);  // CB_ADDSTRING/RESETCONTENT/GETCOUNT/SETCURSEL/GETCURSEL, CBN_DROPDOWN/SELCHANGE
HWND createHotkey(HWND parent, int id);  // HKM_SETHOTKEY / HKM_GETHOTKEY (HOTKEYF_* flags), EN_CHANGE
HWND createLabel(HWND parent, int id, const wchar_t* text, unsigned flags = 0);

// Anti-aliased rounded rectangle (border optional: pass CLR_NONE).
void fillRound(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border = CLR_NONE);

}  // namespace ui

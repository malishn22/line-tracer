// LineTracer - main window: control creation, layout, font and slider handling.
#include "main_window.h"

#include <algorithm>
#include <cstdio>

#include "canvas.h"
#include "session.h"
#include "status.h"

namespace app {

void applyFont() {
    ui::setDpi(A.dpi);
    A.font = ui::font();
    // Only the stock controls (the title edit, the hotkey control) use WM_SETFONT; the custom ones read ui::font().
    EnumChildWindows(A.hwnd, [](HWND c, LPARAM) -> BOOL { SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(A.font), TRUE); return TRUE; }, 0);
}

void layout() {
    RECT cr;
    GetClientRect(A.hwnd, &cr);
    const int pad = S(8), cp = S(6), rh = S(26), gap = S(5);
    auto mv = [](HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); };
    int cardW = cr.right - 2 * pad;
    int y = pad;

    // Card 1: record / settings / colour mode on the left, file actions on the right.
    int h1 = rh + 2 * cp;
    A.cards[0] = {pad, y, pad + cardW, y + h1};
    int x = pad + cp, cy = y + cp;
    mv(A.hRec, x, cy, S(118), rh); x += S(118) + gap;
    mv(A.hSettings, x, cy, S(80), rh); x += S(80) + S(16);
    mv(A.hColor, x, cy, S(132), rh);
    int rx = pad + cardW - cp;
    rx -= S(96); mv(A.hExport, rx, cy, S(96), rh); rx -= gap;
    rx -= S(60); mv(A.hSave, rx, cy, S(60), rh); rx -= gap;
    rx -= S(60); mv(A.hOpen, rx, cy, S(60), rh);
    y += h1 + gap;

    // Card 2: four "label  slider" cells on one line (the label carries the live value).
    int h2 = rh + 2 * cp;
    A.cards[1] = {pad, y, pad + cardW, y + h2};
    // Each label is as wide as the widest text it can show, so the gap to its slider is the same everywhere.
    HWND lbls[4] = {A.hLblBright, A.hLblGain, A.hLblGlow, A.hLblThick};
    HWND sls[4] = {A.hBright, A.hGain, A.hGlow, A.hThick};
    const wchar_t* widest[4] = {L"Brightness 5.00", L"Gain 1.00", L"Glow 50", L"Thickness 6"};
    int lw[4];
    HDC mdc = GetDC(A.hwnd);
    HGDIOBJ of = SelectObject(mdc, ui::font());
    for (int i = 0; i < 4; ++i) {
        SIZE sz{};
        GetTextExtentPoint32W(mdc, widest[i], int(wcslen(widest[i])), &sz);
        lw[i] = sz.cx + S(4);
    }
    SelectObject(mdc, of);
    ReleaseDC(A.hwnd, mdc);
    const int cellGap = S(28), lblSlider = S(8);
    int avail = cardW - 2 * cp - 3 * cellGap - 4 * lblSlider - (lw[0] + lw[1] + lw[2] + lw[3]);
    int sw = std::max<int>(S(40), avail / 4);
    int cx = pad + cp;
    for (int i = 0; i < 4; ++i) {
        mv(lbls[i], cx, y + cp, lw[i], rh);
        mv(sls[i], cx + lw[i] + lblSlider, y + cp + (rh - S(20)) / 2, sw, S(20));
        cx += lw[i] + lblSlider + sw + cellGap;
    }
    y += h2 + gap;

    // Card 3: transport.
    int h3 = rh + 2 * cp;
    A.cards[2] = {pad, y, pad + cardW, y + h3};
    x = pad + cp;
    cy = y + cp;
    mv(A.hPlay, x, cy, S(68), rh); x += S(68) + gap;
    mv(A.hSpeed, x, cy, S(68), rh); x += S(68) + S(16);
    int timeW = S(150);
    int sliderW = std::max<int>(S(100), pad + cardW - cp - timeW - S(12) - x);
    mv(A.hTime, x, cy + (rh - S(20)) / 2, sliderW, S(20));
    mv(A.hLblTime, pad + cardW - cp - timeW, cy, timeW, rh);
    y += h3 + gap;

    int bottom = cr.bottom - S(46);
    int cvW = std::max<int>(cardW - 2, 10), cvH = std::max<int>(bottom - y - 2, 10);
    mv(A.canvas, pad + 1, y + 1, cvW, cvH);
    HRGN rgn = CreateRoundRectRgn(0, 0, cvW + 1, cvH + 1, S(12), S(12));
    SetWindowRgn(A.canvas, rgn, TRUE);  // the system owns rgn from here on
    mv(A.hStats, pad + S(2), bottom + S(4), cardW - S(4), S(18));
    mv(A.hInfo, pad + S(2), bottom + S(23), cardW - S(4), S(18));
    resizeCanvas();
    RedrawWindow(A.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void createControls() {
    A.hRec = ui::createButton(A.hwnd, ID_REC, L"Record", ui::BtnKind::Primary);
    setRecordLabel();
    A.hSettings = ui::createButton(A.hwnd, ID_SETTINGS, L"Settings");
    A.hFilter = ui::createToggle(A.hwnd, ID_FILTER, L"Window filter");
    A.hTitle = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10, 10, A.hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TITLE)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(A.hTitle, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"title contains..."));
    A.hPick = ui::createButton(A.hwnd, ID_PICK, L"Pick");
    A.hColor = ui::createCombo(A.hwnd, ID_COLOR);
    for (const wchar_t* s : {L"Color: White", L"Color: By time", L"Color: By speed"}) SendMessageW(A.hColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(A.hColor, CB_SETCURSEL, 0, 0);
    A.hOpen = ui::createButton(A.hwnd, ID_OPEN, L"Open");
    A.hSave = ui::createButton(A.hwnd, ID_SAVE, L"Save");
    A.hExport = ui::createButton(A.hwnd, ID_EXPORT, L"Export PNG");
    A.hLblBright = ui::createLabel(A.hwnd, ID_LBL_BRIGHT, L"Brightness", ui::LF_RIGHT);
    A.hBright = ui::createSlider(A.hwnd, ID_BRIGHT, 1, 100, 20);  // value/20 -> 0.05 .. 5.0, default 1.0
    A.hLblGain = ui::createLabel(A.hwnd, ID_LBL_GAIN, L"Gain", ui::LF_RIGHT);
    A.hGain = ui::createSlider(A.hwnd, ID_GAIN, 1, 100, 20);      // value/100 -> default 0.2
    A.hLblGlow = ui::createLabel(A.hwnd, ID_LBL_GLOW, L"Glow", ui::LF_RIGHT);
    A.hGlow = ui::createSlider(A.hwnd, ID_GLOW, 0, 50, 4);
    A.hMonitor = ui::createCombo(A.hwnd, ID_MONITOR);
    populateMonitors(false);
    A.hPlay = ui::createButton(A.hwnd, ID_PLAY, L"Play");
    A.hSpeed = ui::createCombo(A.hwnd, ID_SPEED);
    for (const wchar_t* s : {L"1x", L"5x", L"20x", L"60x"}) SendMessageW(A.hSpeed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(A.hSpeed, CB_SETCURSEL, 1, 0);
    A.hBlind = ui::createToggle(A.hwnd, ID_BLIND, L"Blind recording");
    SendMessageW(A.hBlind, BM_SETCHECK, BST_CHECKED, 0);  // on by default
    // These live in the Settings dialog: hidden here, re-parented into it while it is open.
    for (HWND h : {A.hFilter, A.hTitle, A.hPick, A.hMonitor, A.hBlind}) ShowWindow(h, SW_HIDE);
    A.hLblThick = ui::createLabel(A.hwnd, ID_LBL_THICK, L"Thickness 1", ui::LF_RIGHT);
    A.hThick = ui::createSlider(A.hwnd, ID_THICK, 1, 6, 1);
    A.hTime = ui::createSlider(A.hwnd, ID_TIME, 0, 1000, 0);
    SendMessageW(A.hTime, TBM_SETPAGESIZE, 0, 50);
    A.hLblTime = ui::createLabel(A.hwnd, ID_LBL_TIME, L"", ui::LF_RIGHT | ui::LF_DIM);
    A.hStats = ui::createLabel(A.hwnd, ID_STATS, L"", ui::LF_WINDOW);
    A.hInfo = ui::createLabel(A.hwnd, ID_INFO, L"", ui::LF_WINDOW | ui::LF_DIM | ui::LF_ELLIPSIS);
    A.canvas = CreateWindowExW(0, L"LTCanvas", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10, A.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void readSliders() {
    A.brightness = float(SendMessageW(A.hBright, TBM_GETPOS, 0, 0)) / 20.0f;
    A.gain = float(SendMessageW(A.hGain, TBM_GETPOS, 0, 0)) / 100.0f;
    A.glow = float(SendMessageW(A.hGlow, TBM_GETPOS, 0, 0));
    wchar_t b[48];
    swprintf_s(b, L"Brightness %.2f", A.brightness);
    SetWindowTextW(A.hLblBright, b);
    swprintf_s(b, L"Gain %.2f", A.gain);
    SetWindowTextW(A.hLblGain, b);
    swprintf_s(b, L"Glow %d", int(A.glow));
    SetWindowTextW(A.hLblGlow, b);
    A.dispDirty = true;  // display-time only: no re-render of the accumulation buffer
    InvalidateRect(A.canvas, nullptr, FALSE);
}

// Thickness changes the accumulation buffer (unlike brightness/gain/glow), so it needs a redraw.
static void onThickness() {
    A.thickness = int(SendMessageW(A.hThick, TBM_GETPOS, 0, 0));
    wchar_t b[32];
    swprintf_s(b, L"Thickness %d", A.thickness);
    SetWindowTextW(A.hLblThick, b);
    A.rend.setThickness(A.thickness);
    redrawAll();
}

static void onTimeSlider() {
    if (A.recording || A.path.samples.empty()) return;
    int pos = int(SendMessageW(A.hTime, TBM_GETPOS, 0, 0));
    A.playTime = durationMs() * pos / 1000.0;
    updateTimeLabel();
    A.viewN = pos >= 1000 ? A.path.samples.size() : countAtTime(A.playTime);
    A.scrub = true;
    A.lastTick = GetTickCount64();  // if playing, carry on from the new position
    syncDraw();
}

// Slider notifications (WM_HSCROLL): the time slider scrubs, thickness redraws, the rest are display-time only.
void onHScroll(WPARAM w, LPARAM l) {
    HWND src = reinterpret_cast<HWND>(l);
    if (src == A.hTime) {
        if (LOWORD(w) == TB_THUMBTRACK) A.sliderDrag = true;  // playback must not move the thumb meanwhile
        if (LOWORD(w) == TB_ENDTRACK) {
            A.sliderDrag = false;
            A.lastTick = GetTickCount64();
        } else {
            onTimeSlider();
        }
    } else if (src == A.hThick) {
        onThickness();
    } else {
        readSliders();
    }
}

}  // namespace app

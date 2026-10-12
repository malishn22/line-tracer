// LineTracer - Win32 front end: recorder UI, live preview, replay and analysis.
// All drawing happens in this app's own window; nothing is ever drawn over another application.
#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../core/fileio.h"
#include "../core/png.h"
#include "app_state.h"
#include "canvas.h"
#include "session.h"
#include "settings.h"
#include "status.h"

using namespace lt;
using namespace app;

namespace {

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
void onThickness() {
    A.thickness = int(SendMessageW(A.hThick, TBM_GETPOS, 0, 0));
    wchar_t b[32];
    swprintf_s(b, L"Thickness %d", A.thickness);
    SetWindowTextW(A.hLblThick, b);
    A.rend.setThickness(A.thickness);
    redrawAll();
}

void onTimeSlider() {
    if (A.recording || A.path.samples.empty()) return;
    int pos = int(SendMessageW(A.hTime, TBM_GETPOS, 0, 0));
    A.playTime = durationMs() * pos / 1000.0;
    updateTimeLabel();
    A.viewN = pos >= 1000 ? A.path.samples.size() : countAtTime(A.playTime);
    A.scrub = true;
    A.lastTick = GetTickCount64();  // if playing, carry on from the new position
    syncDraw();
}

void pickCountdown() {
    if (--A.pickCount > 0) {
        wchar_t b[32];
        swprintf_s(b, L"Pick (%d)", A.pickCount);
        SetWindowTextW(A.hPick, b);
        return;
    }
    KillTimer(A.hwnd, TIMER_PICK);
    SetWindowTextW(A.hPick, L"Pick");
    std::wstring t = foregroundWindowTitle(A.hwnd);
    if (t.empty() || t == SETTINGS_TITLE) {  // the Settings dialog itself can be the foreground window
        setStatus(L"Pick: no other window was focused. Click the target window during the countdown.");
        return;
    }
    SetWindowTextW(A.hTitle, t.c_str());
    SendMessageW(A.hFilter, BM_SETCHECK, BST_CHECKED, 0);
    updateMonitorEnable();
    setStatus(L"Picked window title: " + t);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        A.hwnd = h;
        A.dpi = int(GetDpiForWindow(h));
        ui::setDpi(A.dpi);
        loadSettings();
        ui::setMode(A.themeMode);
        ui::applyChrome(h, ui::theme().window);
        createControls();
        applySettingsToControls();
        applyFont();
        SetTimer(h, TIMER_TICK, 33, nullptr);
        if (!registerRecordHotkey()) A.status = hotkeyName(A.hkMods, A.hkVk) + L" is already taken by another program; use the Record button or pick another key in Settings.";
        else A.status = L"Press " + hotkeyName(A.hkMods, A.hkVk) + L" or Record to start.";
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        const ui::Theme& t = ui::theme();
        FillRect(dc, &ps.rcPaint, ui::windowBrush());
        for (const RECT& c : A.cards) ui::fillRound(dc, c, S(10), t.panel, t.border);
        RECT cv;
        GetWindowRect(A.canvas, &cv);
        MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&cv), 2);
        InflateRect(&cv, 1, 1);
        ui::fillRound(dc, cv, S(13), t.canvasBg, t.border);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (l && wcscmp(reinterpret_cast<const wchar_t*>(l), L"ImmersiveColorSet") == 0 && A.themeMode == ui::Mode::Auto) applyTheme();
        return 0;
    case WM_SIZE:
        if (A.canvas) layout();
        updateInfo();
        updateStats();
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {S(960), S(520)};
        return 0;
    case WM_DPICHANGED: {
        A.dpi = HIWORD(w);
        const RECT* r = reinterpret_cast<const RECT*>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        applyFont();
        layout();
        return 0;
    }
    case WM_HOTKEY:
        if (w == HOTKEY_ID && !A.settingsWnd) toggleRecording();
        return 0;
    case WM_TIMER:
        if (w == TIMER_TICK) onTick();
        else if (w == TIMER_PICK) pickCountdown();
        return 0;
    case WM_HSCROLL: {
        HWND src = reinterpret_cast<HWND>(l);
        if (src == A.hTime) {
            if (LOWORD(w) == TB_THUMBTRACK) A.sliderDrag = true;  // playback must not move the thumb meanwhile
            if (LOWORD(w) == TB_ENDTRACK) {
                A.sliderDrag = false;
                A.lastTick = GetTickCount64();
            } else {
                onTimeSlider();
            }
        }
        else if (src == A.hThick) onThickness();
        else readSliders();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        switch (id) {
        case ID_REC: toggleRecording(); break;
        case ID_SETTINGS: openSettings(); break;
        case ID_FILTER:
            updateMonitorEnable();
            if (isChecked(A.hFilter))
                setStatus(L"Window filter is on: the target window is the canvas, so the monitor choice is ignored.");
            break;
        case ID_MONITOR:
            if (code == CBN_SELCHANGE) onMonitorChanged();
            else if (code == CBN_DROPDOWN) populateMonitors(true);
            break;
        case ID_PICK:
            if (A.pickCount <= 0 && !A.recording) {
                A.pickCount = 5;
                SetWindowTextW(A.hPick, L"Pick (5)");
                SetTimer(h, TIMER_PICK, 1000, nullptr);
                setStatus(L"Pick: click the target window now, its title is captured when the countdown ends.");
            }
            break;
        case ID_COLOR:
            if (code == CBN_SELCHANGE) {
                A.rend.setMode(ColorMode(SendMessageW(A.hColor, CB_GETCURSEL, 0, 0)));
                redrawAll();
            }
            break;
        case ID_OPEN: {
            std::wstring f;
            if (!A.recording && fileDialog(false, L"LineTrace (*.ltr)\0*.ltr\0All files\0*.*\0", L"ltr", f)) loadFile(f);
            break;
        }
        case ID_SAVE: {
            std::wstring f;
            if (A.path.samples.empty() || A.recording) break;
            if (fileDialog(true, L"LineTrace (*.ltr)\0*.ltr\0", L"ltr", f)) {
                std::string err;
                setStatus(savePath(f, A.path, &err) ? L"Saved " + f : L"Save failed.");
            }
            break;
        }
        case ID_EXPORT: if (!A.recording) doExport(); break;
        case ID_PLAY: togglePlay(); break;
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_TICK);
        unregisterToggleHotkey(h, HOTKEY_ID);
        A.rec.stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Headless: LineTracer.exe --export in.ltr out.png [white|time|speed]
int headlessExport(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    Path p;
    std::string err;
    if (!loadPath(argv[2], p, &err)) return 3;
    ExportSettings s;
    s.timeSpanMs = std::max(1.0, p.samples.size() >= 2 ? double(p.samples.back().ms) : 1.0);
    if (argc >= 5) {
        std::wstring m = argv[4];
        s.mode = m == L"time" ? ColorMode::Time : m == L"speed" ? ColorMode::Speed : ColorMode::White;
    }
    return exportPng(argv[3], p, s) ? 0 : 4;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && std::wstring(argv[1]) == L"--export") return headlessExport(argc, argv);

    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);

    ui::startup(inst);

    WNDCLASSW sc{};
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    sc.lpszClassName = L"LTSettings";
    RegisterClassW(&sc);

    WNDCLASSW cc{};
    cc.lpfnWndProc = CanvasProc;
    cc.hInstance = inst;
    cc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    cc.lpszClassName = L"LTCanvas";
    RegisterClassW(&cc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LineTracerMain";
    RegisterClassW(&wc);

    UINT sysDpi = GetDpiForSystem();
    HWND hw = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              MulDiv(1280, sysDpi, 96), MulDiv(860, sysDpi, 96), nullptr, nullptr, inst, nullptr);
    if (!hw) return 1;
    ShowWindow(hw, show);
    layout();
    readSliders();
    updateStats();
    updateInfo();
    if (argv && argc >= 2 && argv[1][0] != L'-') loadFile(argv[1]);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ui::shutdown();
    return int(msg.wParam);
}

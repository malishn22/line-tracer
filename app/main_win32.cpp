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
#include "main_window.h"
#include "resource.h"
#include "session.h"
#include "settings.h"
#include "status.h"

using namespace lt;
using namespace app;

namespace {

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

// Button, toggle and combo notifications routed to WM_COMMAND.
void onCommand(int id, int code) {
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
            SetTimer(A.hwnd, TIMER_PICK, 1000, nullptr);
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
    case WM_HSCROLL:
        onHScroll(w, l);
        return 0;
    case WM_COMMAND:
        onCommand(LOWORD(w), HIWORD(w));
        return 0;
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
    // Pick the icon frames that match the DPI-scaled title bar and taskbar sizes.
    for (int big = 0; big < 2; ++big) {
        int cx = GetSystemMetricsForDpi(big ? SM_CXICON : SM_CXSMICON, sysDpi);
        int cy = GetSystemMetricsForDpi(big ? SM_CYICON : SM_CYSMICON, sysDpi);
        HANDLE icon = LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
        if (icon) SendMessageW(hw, WM_SETICON, big ? ICON_BIG : ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }
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

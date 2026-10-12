// LineTracer - settings persistence, the Settings dialog, hotkey handling and theme application.
#include "settings.h"

#include <initguid.h>
#include <KnownFolders.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>

#include "status.h"

namespace fs = std::filesystem;

namespace app {

static fs::path settingsFile() {
    fs::path dir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = p;
        CoTaskMemFree(p);
    }
    dir /= L"LineTracer";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"settings.ini";
}

// Loads what lives in App fields; the controls are filled by applySettingsToControls once they exist.
struct SavedSettings { bool blind = true, filter = false; std::wstring title; };
static SavedSettings gSaved;

void applyTheme() {
    ui::setMode(A.themeMode);
    ui::applyChrome(A.hwnd, ui::theme().window);
    if (A.settingsWnd) ui::applyChrome(A.settingsWnd, ui::theme().panel);
    RedrawWindow(A.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (A.settingsWnd) RedrawWindow(A.settingsWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void loadSettings() {
    std::wstring f = settingsFile().wstring();
    auto geti = [&](const wchar_t* k, int d) { return int(GetPrivateProfileIntW(L"LineTracer", k, d, f.c_str())); };
    auto gets = [&](const wchar_t* k) {
        wchar_t b[512] = {};
        GetPrivateProfileStringW(L"LineTracer", k, L"", b, 512, f.c_str());
        return std::wstring(b);
    };
    unsigned vk = unsigned(geti(L"hotkeyVk", VK_F9));
    if (vk > 0 && vk < 256) {
        A.hkVk = vk;
        A.hkMods = unsigned(geti(L"hotkeyMods", 0)) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT);
    }
    gSaved.blind = geti(L"blind", 1) != 0;
    gSaved.filter = geti(L"filter", 0) != 0;
    gSaved.title = gets(L"title");
    A.monAll = geti(L"monitorAll", 0) != 0;
    A.monName = gets(L"monitor");
    int th = geti(L"theme", 0);
    A.themeMode = th == 1 ? ui::Mode::Light : th == 2 ? ui::Mode::Dark : ui::Mode::Auto;
}

static void saveSettings() {
    std::wstring f = settingsFile().wstring();
    auto put = [&](const wchar_t* k, const std::wstring& v) { WritePrivateProfileStringW(L"LineTracer", k, v.c_str(), f.c_str()); };
    wchar_t t[512] = {};
    GetWindowTextW(A.hTitle, t, 512);
    put(L"hotkeyVk", std::to_wstring(A.hkVk));
    put(L"hotkeyMods", std::to_wstring(A.hkMods));
    put(L"blind", isChecked(A.hBlind) ? L"1" : L"0");
    put(L"filter", isChecked(A.hFilter) ? L"1" : L"0");
    put(L"title", t);
    put(L"monitorAll", A.monAll ? L"1" : L"0");
    put(L"monitor", A.monName);
    put(L"theme", std::to_wstring(int(A.themeMode)));
}

void applySettingsToControls() {
    SendMessageW(A.hBlind, BM_SETCHECK, gSaved.blind ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(A.hFilter, BM_SETCHECK, gSaved.filter ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(A.hTitle, gSaved.title.c_str());
    updateMonitorEnable();
}

// Hotkey control <-> MOD_* flags (the control uses HOTKEYF_SHIFT=1, CONTROL=2, ALT=4).
static unsigned hkfToMod(unsigned f) { return ((f & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | ((f & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) | ((f & HOTKEYF_ALT) ? MOD_ALT : 0); }
static unsigned modToHkf(unsigned m) { return ((m & MOD_SHIFT) ? HOTKEYF_SHIFT : 0) | ((m & MOD_CONTROL) ? HOTKEYF_CONTROL : 0) | ((m & MOD_ALT) ? HOTKEYF_ALT : 0); }

bool registerRecordHotkey() { return registerToggleHotkey(A.hwnd, HOTKEY_ID, A.hkMods, A.hkVk); }

static bool hotkeyKeyAllowed(unsigned mods, unsigned vk) {
    return mods != 0 || (vk >= VK_F1 && vk <= VK_F24);  // a bare letter would steal that key everywhere
}

// The title edit sits inside a rounded "field" painted by the dialog (96-dpi rect: x, y, w, h).
static RECT titleFieldRect() { return {S(150), S(56), S(150) + S(236), S(56) + S(32)}; }

LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        const ui::Theme& t = ui::theme();
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, ui::panelBrush());
        ui::fillRound(dc, titleFieldRect(), S(8), t.field, GetFocus() == A.hTitle ? t.accent : t.border);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, ui::theme().text);
        SetBkColor(dc, ui::theme().field);
        return reinterpret_cast<LRESULT>(ui::fieldBrush());
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == ID_TITLE && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
            RECT f = titleFieldRect();
            InvalidateRect(h, &f, FALSE);
        }
        if (id == ID_APPEARANCE && code == CBN_SELCHANGE) {
            A.themeMode = ui::Mode(std::max<LRESULT>(0, SendMessageW(reinterpret_cast<HWND>(l), CB_GETCURSEL, 0, 0)));
            applyTheme();
            return 0;
        }
        if (id == ID_SET_CLOSE || id == IDOK || id == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        if (id == ID_SET_HOTKEY && code == EN_CHANGE) {
            LRESULT v = SendMessageW(reinterpret_cast<HWND>(l), HKM_GETHOTKEY, 0, 0);
            unsigned vk = unsigned(LOBYTE(v)), mods = hkfToMod(unsigned(HIBYTE(LOWORD(v))));
            HWND hint = GetDlgItem(h, ID_HINT);
            if (vk == 0) return 0;  // only modifiers held so far
            if (!hotkeyKeyAllowed(mods, vk)) {
                SetWindowTextW(hint, L"Add Ctrl, Alt or Shift (only F1-F24 can be used alone).");
            } else if (!registerToggleHotkey(A.hwnd, HOTKEY_ID, mods, vk)) {
                SetWindowTextW(hint, L"That key is already taken by another program.");
            } else {
                unregisterToggleHotkey(A.hwnd, HOTKEY_ID);  // test only; it is re-registered when the dialog closes
                A.hkMods = mods;
                A.hkVk = vk;
                SetWindowTextW(hint, (L"Record/stop key set to " + hotkeyName(mods, vk) + L".").c_str());
            }
            return 0;
        }
        // The re-parented main-window controls (monitor, filter, pick...) report to the main window.
        return SendMessageW(A.hwnd, WM_COMMAND, w, l);
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void openSettings() {
    if (A.recording || A.settingsWnd) return;
    unregisterToggleHotkey(A.hwnd, HOTKEY_ID);  // so the hotkey control can see the current key being pressed
    RECT cr = {0, 0, S(504), S(300)};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&cr, style, FALSE, 0, UINT(A.dpi));
    RECT or_;
    GetWindowRect(A.hwnd, &or_);
    int w = cr.right - cr.left, hgt = cr.bottom - cr.top;
    int x = or_.left + ((or_.right - or_.left) - w) / 2, y = or_.top + ((or_.bottom - or_.top) - hgt) / 2;
    HWND d = CreateWindowExW(0, L"LTSettings", SETTINGS_TITLE, style | WS_VISIBLE, x, y, w, hgt, A.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d) {
        registerRecordHotkey();
        return;
    }
    A.settingsWnd = d;
    ui::applyChrome(d, ui::theme().panel);

    auto place = [&](HWND c, int cx, int cy, int cw_, int ch_) {
        SetParent(c, d);
        MoveWindow(c, S(cx), S(cy), S(cw_), S(ch_), TRUE);
        ShowWindow(c, SW_SHOW);
    };
    auto label = [&](const wchar_t* text, int id, unsigned flags, int cx, int cy, int cw_, int ch_) {
        HWND c = ui::createLabel(d, id, text, flags);
        MoveWindow(c, S(cx), S(cy), S(cw_), S(ch_), TRUE);
        return c;
    };
    label(L"Monitor", 0x7001, 0, 18, 20, 120, 24);
    place(A.hMonitor, 150, 16, 336, 32);
    place(A.hFilter, 18, 56, 128, 32);
    place(A.hTitle, 150 + 10, 56 + 8, 236 - 20, 32 - 16);
    place(A.hPick, 394, 56, 92, 32);
    place(A.hBlind, 18, 98, 300, 32);
    label(L"Record / stop hotkey", 0x7002, 0, 18, 142, 130, 24);
    HWND hk = ui::createHotkey(d, ID_SET_HOTKEY);
    MoveWindow(hk, S(150), S(136), S(200), S(32), TRUE);
    SendMessageW(hk, HKM_SETHOTKEY, MAKEWORD(A.hkVk, modToHkf(A.hkMods)), 0);
    label(L"Appearance", 0x7003, 0, 18, 184, 120, 24);
    HWND ap = ui::createCombo(d, ID_APPEARANCE);
    for (const wchar_t* s : {L"Follow system", L"Light", L"Dark"}) SendMessageW(ap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(ap, CB_SETCURSEL, int(A.themeMode), 0);
    MoveWindow(ap, S(150), S(180), S(200), S(32), TRUE);
    label(L"Tip: use a modifier combo such as Ctrl+Alt+R so the game does not also see the key.", ID_HINT, ui::LF_DIM, 18, 226, 468, 22);
    HWND close = ui::createButton(d, ID_SET_CLOSE, L"Close", ui::BtnKind::Primary);
    MoveWindow(close, S(394), S(256), S(92), S(32), TRUE);
    EnumChildWindows(d, [](HWND c, LPARAM) -> BOOL { SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(A.font), TRUE); return TRUE; }, 0);
    updateMonitorEnable();

    EnableWindow(A.hwnd, FALSE);
    MSG msg{};
    while (IsWindow(d) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(d, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (msg.message == WM_QUIT) PostQuitMessage(int(msg.wParam));

    // Hand the shared controls back to the main window (hidden) and persist the choices.
    for (HWND c : {A.hMonitor, A.hFilter, A.hTitle, A.hPick, A.hBlind}) {
        ShowWindow(c, SW_HIDE);
        SetParent(c, A.hwnd);
    }
    A.settingsWnd = nullptr;
    EnableWindow(A.hwnd, TRUE);
    SetForegroundWindow(A.hwnd);
    if (!registerRecordHotkey()) setStatus(hotkeyName(A.hkMods, A.hkVk) + L" is already taken by another program; use the Record button.");
    else setStatus(L"Press " + hotkeyName(A.hkMods, A.hkVk) + L" or Record to start.");
    setRecordLabel();
    saveSettings();
}

}  // namespace app

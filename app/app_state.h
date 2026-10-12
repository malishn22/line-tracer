// LineTracer - the single App state object shared by the front-end modules, plus tiny helpers.
#pragma once
#include <windows.h>

#include <commctrl.h>

#include <string>
#include <vector>

#include "../core/hitgrid.h"
#include "../core/path.h"
#include "../core/renderer.h"
#include "../core/stats.h"
#include "../platform/capture.h"
#include "ui_controls.h"
#include "ui_theme.h"

namespace app {
using namespace lt;

enum {
    ID_REC = 101, ID_FILTER, ID_TITLE, ID_PICK, ID_COLOR, ID_OPEN, ID_SAVE, ID_EXPORT,
    ID_BRIGHT, ID_GAIN, ID_GLOW, ID_PLAY, ID_SPEED, ID_TIME,
    ID_LBL_BRIGHT, ID_LBL_GAIN, ID_LBL_GLOW, ID_STATS, ID_INFO, ID_MONITOR, ID_LBL_MONITOR, ID_BLIND, ID_THICK, ID_LBL_THICK,
    ID_SETTINGS, ID_SET_HOTKEY, ID_SET_CLOSE, ID_LBL_TIME, ID_APPEARANCE,
};
enum { TIMER_TICK = 1, TIMER_PICK = 2, HOTKEY_ID = 1 };
inline constexpr const wchar_t* SETTINGS_TITLE = L"Settings";
inline constexpr const wchar_t* APP_TITLE = L"LineTracer";
inline constexpr const wchar_t* BLIND_STATS = L"Recording (blind mode)";
inline constexpr int MARGIN = 10;
inline constexpr int ID_HINT = 0x7000;
inline constexpr double SPEEDS[4] = {1, 5, 20, 60};

struct App {
    HWND hwnd = nullptr, canvas = nullptr;
    HWND hRec, hFilter, hTitle, hPick, hColor, hOpen, hSave, hExport, hSettings;
    HWND settingsWnd = nullptr;  // non-null while the Settings dialog is open
    unsigned hkMods = 0, hkVk = VK_F9;  // record/stop hotkey (MOD_* flags + virtual key)
    bool sliderDrag = false;  // mouse is held on the time slider
    HWND hBright, hGain, hGlow, hLblBright, hLblGain, hLblGlow;
    HWND hPlay, hSpeed, hTime, hStats, hInfo, hMonitor, hLblMonitor, hBlind, hThick, hLblThick, hLblTime;
    RECT cards[3] = {};  // painted panels behind the toolbar, the display sliders and the playback bar
    ui::Mode themeMode = ui::Mode::Auto;
    int thickness = 1;  // line width in px, applies to the preview and to PNG exports
    HFONT font = nullptr;
    bool blindActive = false;  // latched at Record: no preview/stats/hover until Stop ("play first, analyze afterward")

    std::vector<MonitorInfo> monitors;  // as shown in the combo; "All monitors" is the extra last entry
    bool monAll = false;                // selection: all monitors, or the one named monName
    std::wstring monName;               // empty until the first populate (then the primary monitor)
    int dpi = 96;

    Path path;
    Analysis an;
    HitGrid grid;
    Renderer rend;
    CursorRecorder rec;

    bool recording = false, boundsApplied = false;
    double timeSpan = 60000;
    float brightness = 1.0f, gain = 0.2f, glow = 4.0f;

    size_t viewN = 0;  // samples shown (playhead)
    bool scrub = false, playing = false;
    double playTime = 0;
    ULONGLONG lastTick = 0;

    HBITMAP dib = nullptr;
    uint32_t* bits = nullptr;
    int cw = 0, ch = 0;
    bool dispDirty = true;

    size_t hoverIdx = SIZE_MAX;
    std::wstring status;
    std::wstring hoverText, cacheStats, cacheInfo, cacheTime;
    int pickCount = 0;
};

extern App A;

int S(int v);  // scale a 96-dpi length to the current dpi
bool blindNow();
bool isChecked(HWND h);  // BM_GETCHECK == BST_CHECKED
void setText(HWND h, const std::wstring& s, std::wstring* cache = nullptr);  // skips the call when *cache already equals s
std::wstring fmtDuration(double ms);
double durationMs();

}  // namespace app

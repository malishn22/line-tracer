// Platform capture interface. A macOS/Linux backend implements this header in
// platform/<os>/ ; core/ never includes anything from here.
//
// Design rule (anti-cheat hygiene): capture is PASSIVE POLLING of the cursor position only.
// No hooks, no injection, no process handles, no overlays on other windows.
#pragma once
#include <string>
#include <vector>

#include "../core/path.h"

namespace lt {

struct CaptureConfig {
    bool filterEnabled = false;
    std::wstring titleSubstring;  // case-insensitive; matched against the foreground window title
    // Record only while the cursor is inside `region` (e.g. one monitor); the region is the canvas.
    // Ignored when filterEnabled (the target window's client rect is the canvas then).
    bool restrictToRegion = false;
    Bounds region;
};

struct MonitorInfo {
    std::wstring name;  // OS device name, stable across re-enumeration (e.g. \\.\DISPLAY1)
    Bounds rect;        // physical pixels, virtual-desktop coordinates (may be negative)
    bool primary = false;
};
std::vector<MonitorInfo> listMonitors();  // sorted left to right, then top to bottom

class CursorRecorder {
public:
    CursorRecorder();
    ~CursorRecorder();
    CursorRecorder(const CursorRecorder&) = delete;
    CursorRecorder& operator=(const CursorRecorder&) = delete;

    bool start(const CaptureConfig& cfg);
    void stop();  // joins the polling thread
    bool running() const;
    bool paused() const;                  // filter on and the target window is not focused
    void drain(std::vector<Sample>& out); // append samples captured since the last drain
    bool bounds(Bounds& b) const;         // false until the canvas bounds are known
    // Time of the last stored sample's clock: ms since start() as of now.
    unsigned elapsedMs() const;

private:
    struct Impl;
    Impl* p_;
};

// Global start/stop hotkey (user-assignable, default F9). nativeWindow is an HWND on Windows; WM_HOTKEY is delivered to it.
bool registerToggleHotkey(void* nativeWindow, int id, unsigned mods, unsigned vk);  // mods = MOD_* flags, vk = virtual key
void unregisterToggleHotkey(void* nativeWindow, int id);

// Title of the current foreground window; empty if it is `excludeWindow` or has no title.
std::wstring foregroundWindowTitle(void* excludeWindow);

}  // namespace lt

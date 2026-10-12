// LineTracer - Win32 front end: recorder UI, live preview, replay and analysis.
// All drawing happens in this app's own window; nothing is ever drawn over another application.
#include <windows.h>

#include <initguid.h>
#include <KnownFolders.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../core/fileio.h"
#include "../core/hitgrid.h"
#include "../core/png.h"
#include "../core/renderer.h"
#include "../core/stats.h"
#include "../platform/capture.h"

using namespace lt;
namespace fs = std::filesystem;

namespace {

enum {
    ID_REC = 101, ID_FILTER, ID_TITLE, ID_PICK, ID_COLOR, ID_OPEN, ID_SAVE, ID_EXPORT,
    ID_BRIGHT, ID_GAIN, ID_GLOW, ID_PLAY, ID_SPEED, ID_TIME,
    ID_LBL_BRIGHT, ID_LBL_GAIN, ID_LBL_GLOW, ID_STATS, ID_INFO, ID_MONITOR, ID_LBL_MONITOR, ID_BLIND, ID_THICK, ID_LBL_THICK,
    ID_SETTINGS, ID_SET_HOTKEY, ID_SET_CLOSE,
};
enum { TIMER_TICK = 1, TIMER_PICK = 2, HOTKEY_ID = 1 };
const wchar_t* SETTINGS_TITLE = L"Settings";
const int MARGIN = 10;
const double SPEEDS[4] = {1, 5, 20, 60};
const wchar_t* APP_TITLE = L"LineTracer";

struct App {
    HWND hwnd = nullptr, canvas = nullptr;
    HWND hRec, hFilter, hTitle, hPick, hColor, hOpen, hSave, hExport, hSettings;
    HWND settingsWnd = nullptr;  // non-null while the Settings dialog is open
    unsigned hkMods = 0, hkVk = VK_F9;  // record/stop hotkey (MOD_* flags + virtual key)
    bool sliderDrag = false;  // mouse is held on the time slider
    HWND hBright, hGain, hGlow, hLblBright, hLblGain, hLblGlow;
    HWND hPlay, hSpeed, hTime, hStats, hInfo, hMonitor, hLblMonitor, hBlind, hThick, hLblThick;
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
    std::wstring hoverText, cacheStats, cacheInfo;
    int pickCount = 0;
} A;

int S(int v) { return MulDiv(v, A.dpi, 96); }

bool blindNow() { return A.recording && A.blindActive; }
const wchar_t* BLIND_STATS = L"Recording (blind mode)";

void setText(HWND h, const std::wstring& s, std::wstring* cache = nullptr) {
    if (cache) {
        if (*cache == s) return;
        *cache = s;
    }
    SetWindowTextW(h, s.c_str());
}

std::wstring fmtDuration(double ms) {
    wchar_t b[64];
    int m = int(ms / 60000);
    swprintf_s(b, L"%d:%04.1f", m, (ms - m * 60000.0) / 1000.0);
    return b;
}

double durationMs() {
    return A.path.samples.size() >= 2 ? double(A.path.samples.back().ms) - A.path.samples.front().ms : 0.0;
}

// ---------- drawing state ----------

void redrawAll() {
    A.rend.clear();
    A.rend.drawTo(A.path, A.viewN);
    A.dispDirty = true;
    InvalidateRect(A.canvas, nullptr, FALSE);
}

void syncDraw() {
    A.rend.drawTo(A.path, A.viewN);  // incremental forward, full redraw if the playhead moved back
    A.dispDirty = true;
    InvalidateRect(A.canvas, nullptr, FALSE);
}

void reconfigure() {
    if (A.cw <= 0 || A.ch <= 0) return;
    A.rend.configure(A.cw, A.ch, A.path.bounds, MARGIN);
    A.rend.drawTo(A.path, A.viewN);
    A.dispDirty = true;
    InvalidateRect(A.canvas, nullptr, FALSE);
}

void resizeCanvas() {
    RECT rc;
    GetClientRect(A.canvas, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0 || (w == A.cw && h == A.ch)) return;
    if (A.dib) DeleteObject(A.dib);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    A.dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&A.bits), nullptr, 0);
    A.cw = w;
    A.ch = h;
    reconfigure();
}

void updateStats() {
    if (blindNow()) {  // no numbers while recording in blind mode
        setText(A.hStats, BLIND_STATS, &A.cacheStats);
        return;
    }
    Stats s = A.an.stats(A.path);
    wchar_t b[256];
    swprintf_s(b, L"Points: %zu    Duration: %s    Distance: %.0f px    Avg speed: %.0f px/s    Peak speed: %.0f px/s",
               s.points, fmtDuration(s.durationMs).c_str(), s.totalDist, s.avgSpeed, s.peakSpeed);
    setText(A.hStats, b, &A.cacheStats);
}

void updateInfo() { setText(A.hInfo, A.hoverIdx != SIZE_MAX ? A.hoverText : A.status, &A.cacheInfo); }

void setStatus(const std::wstring& s) {
    A.status = s;
    updateInfo();
}

void setSliderFromTime() {
    double d = durationMs();
    int pos = d > 0 ? int(std::min(1.0, A.playTime / d) * 1000.0 + 0.5) : 0;
    SendMessageW(A.hTime, TBM_SETPOS, TRUE, pos);
}

size_t countAtTime(double t) {
    const auto& v = A.path.samples;
    if (v.empty()) return 0;
    double t0 = v.front().ms;
    auto it = std::upper_bound(v.begin(), v.end(), t0 + t, [](double tt, const Sample& s) { return tt < s.ms; });
    return std::max<size_t>(1, size_t(it - v.begin()));
}

void setPlayButton() { SetWindowTextW(A.hPlay, A.playing ? L"Pause" : L"Play"); }

// Replace the whole path (load) and rebuild every derived structure.
void onPathReplaced() {
    A.an.reset();
    A.an.update(A.path);
    A.grid.reset();
    A.grid.sync(A.path);
    A.viewN = A.path.samples.size();
    A.scrub = false;
    A.playing = false;
    A.playTime = durationMs();
    A.hoverIdx = SIZE_MAX;
    A.timeSpan = std::max(durationMs(), 1.0);
    A.rend.setTimeSpan(A.timeSpan);
    setPlayButton();
    setSliderFromTime();
    A.rend.configure(A.cw, A.ch, A.path.bounds, MARGIN);
    redrawAll();
    updateStats();
}

fs::path lineTraceDir() {
    fs::path dir;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        dir = docs;
        CoTaskMemFree(docs);
    }
    dir /= L"LineTrace";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// The monitor choice only applies when the window filter is off (the filtered window is the canvas then).
void updateMonitorEnable() {
    bool filterOn = SendMessageW(A.hFilter, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(A.hMonitor, !A.recording && !filterOn);
}

std::wstring hotkeyName(unsigned mods, unsigned vk) {
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT) s += L"Alt+";
    if (mods & MOD_SHIFT) s += L"Shift+";
    wchar_t key[64] = {};
    LONG sc = LONG(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    if (GetKeyNameTextW(sc, key, 64) <= 0) swprintf_s(key, L"0x%02X", vk);
    return s + key;
}

void setRecordLabel() {
    std::wstring t = (A.recording ? L"Stop (" : L"Record (") + hotkeyName(A.hkMods, A.hkVk) + L")";
    SetWindowTextW(A.hRec, t.c_str());
}

void enableControlsForRecording(bool rec) {
    BOOL idle = rec ? FALSE : TRUE;
    for (HWND h : {A.hOpen, A.hSave, A.hExport, A.hPlay, A.hTime, A.hFilter, A.hTitle, A.hPick, A.hBlind, A.hSettings}) EnableWindow(h, idle);
    setRecordLabel();
    updateMonitorEnable();
}

// (Re)fill the monitor combo. Keeps the selection by device name; if that monitor was unplugged,
// falls back to the primary one. Skips the rebuild when nothing changed (safe to call from CBN_DROPDOWN).
void populateMonitors(bool announce) {
    std::vector<MonitorInfo> now = listMonitors();
    bool same = now.size() == A.monitors.size();
    for (size_t i = 0; same && i < now.size(); ++i)
        same = now[i].name == A.monitors[i].name && now[i].rect.x == A.monitors[i].rect.x &&
               now[i].rect.y == A.monitors[i].rect.y && now[i].rect.w == A.monitors[i].rect.w &&
               now[i].rect.h == A.monitors[i].rect.h;
    if (same && SendMessageW(A.hMonitor, CB_GETCOUNT, 0, 0) > 0) return;

    A.monitors = now;
    SendMessageW(A.hMonitor, CB_RESETCONTENT, 0, 0);
    int sel = -1, prim = 0;
    for (size_t i = 0; i < now.size(); ++i) {
        wchar_t b[96];
        swprintf_s(b, L"Monitor %zu%s  %dx%d", i + 1, now[i].primary ? L" (primary)" : L"", now[i].rect.w, now[i].rect.h);
        SendMessageW(A.hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(b));
        if (now[i].primary) prim = int(i);
        if (!A.monAll && now[i].name == A.monName) sel = int(i);
    }
    SendMessageW(A.hMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All monitors"));
    bool lost = !A.monAll && !A.monName.empty() && sel < 0;
    if (A.monAll) sel = int(now.size());
    else if (sel < 0) sel = prim;
    if (!A.monAll && sel < int(now.size())) A.monName = now[sel].name;
    SendMessageW(A.hMonitor, CB_SETCURSEL, sel, 0);
    if (lost && announce) setStatus(L"The selected monitor is gone; switched to the primary monitor.");
}

void onMonitorChanged() {
    int idx = int(SendMessageW(A.hMonitor, CB_GETCURSEL, 0, 0));
    if (idx < 0) return;
    A.monAll = idx >= int(A.monitors.size());
    if (!A.monAll) A.monName = A.monitors[idx].name;
}

void appendSamples(const std::vector<Sample>& v) {
    if (v.empty()) return;
    A.path.samples.insert(A.path.samples.end(), v.begin(), v.end());
    if (blindNow()) return;  // derived data (drawing, stats, hit grid) is built once, after Stop
    A.an.update(A.path);
    A.grid.sync(A.path);
    if (!A.scrub) A.viewN = A.path.samples.size();
    syncDraw();
    updateStats();
}

void pumpRecorder() {
    Bounds b;
    if (!A.boundsApplied && A.rec.bounds(b)) {
        A.path.bounds = b;
        A.boundsApplied = true;
        reconfigure();
    }
    if (!A.boundsApplied) return;
    std::vector<Sample> v;
    A.rec.drain(v);
    appendSamples(v);
}

void startRecording() {
    CaptureConfig cfg;
    cfg.filterEnabled = SendMessageW(A.hFilter, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t t[512] = {};
    GetWindowTextW(A.hTitle, t, 512);
    cfg.titleSubstring = t;
    if (cfg.filterEnabled && cfg.titleSubstring.empty()) {
        setStatus(L"Window filter is on but the title text is empty. Type a title or use Pick.");
        return;
    }
    if (!cfg.filterEnabled) {
        populateMonitors(true);  // pick up displays plugged in or removed since startup
        if (!A.monAll)
            for (const MonitorInfo& m : A.monitors)
                if (m.name == A.monName) {
                    cfg.restrictToRegion = true;
                    cfg.region = m.rect;
                }
    }
    A.path = Path();
    A.an.reset();
    A.grid.reset();
    A.viewN = 0;
    A.scrub = A.playing = false;
    A.hoverIdx = SIZE_MAX;
    A.boundsApplied = false;
    A.blindActive = SendMessageW(A.hBlind, BM_GETCHECK, 0, 0) == BST_CHECKED;
    A.timeSpan = 60000;
    A.rend.setTimeSpan(A.timeSpan);
    setPlayButton();
    SendMessageW(A.hTime, TBM_SETPOS, TRUE, 0);
    redrawAll();
    updateStats();
    if (!A.rec.start(cfg)) return;
    A.recording = true;
    enableControlsForRecording(true);
    updateStats();  // shows the blind-mode text when blind
    InvalidateRect(A.canvas, nullptr, FALSE);
    setStatus(cfg.filterEnabled ? L"Recording (waiting for the target window to be focused)..." : L"Recording...");
}

void stopRecording() {
    A.rec.stop();
    pumpRecorder();
    A.recording = false;
    A.blindActive = false;
    enableControlsForRecording(false);
    // Build everything derived from the samples (a no-op catch-up unless blind mode deferred it).
    A.an.reset();
    A.an.update(A.path);
    A.grid.reset();
    A.grid.sync(A.path);
    A.scrub = false;
    A.viewN = A.path.samples.size();
    A.playTime = durationMs();
    A.timeSpan = std::max(durationMs(), 1.0);  // colour-by-time is normalised once recording ends
    A.rend.setTimeSpan(A.timeSpan);
    redrawAll();
    updateStats();
    setSliderFromTime();
    if (A.path.samples.empty()) {
        setStatus(L"Stopped. No movement was recorded, nothing saved.");
        return;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"linetrace_%04d%02d%02d_%02d%02d%02d.ltr", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fs::path f = lineTraceDir() / name;
    std::string err;
    if (savePath(f, A.path, &err)) setStatus(L"Saved " + f.wstring());
    else setStatus(L"Auto-save FAILED: " + std::wstring(err.begin(), err.end()));
}

void toggleRecording() {
    if (A.recording) stopRecording();
    else startRecording();
}

bool fileDialog(bool save, const wchar_t* filter, const wchar_t* defExt, std::wstring& out) {
    wchar_t buf[MAX_PATH] = {};
    std::wstring dir = lineTraceDir().wstring();
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = A.hwnd;
    o.lpstrFilter = filter;
    o.lpstrFile = buf;
    o.nMaxFile = MAX_PATH;
    o.lpstrInitialDir = dir.c_str();
    o.lpstrDefExt = defExt;
    o.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return false;
    out = buf;
    return true;
}

void loadFile(const fs::path& f) {
    Path p;
    std::string err;
    if (!loadPath(f, p, &err)) {
        setStatus(L"Load failed: " + std::wstring(err.begin(), err.end()));
        return;
    }
    A.path = std::move(p);
    onPathReplaced();
    setStatus(L"Loaded " + f.wstring());
}

ExportSettings currentExportSettings() {
    ExportSettings s;
    s.mode = A.rend.mode();
    s.timeSpanMs = A.timeSpan;
    s.brightness = A.brightness;
    s.gain = A.gain;
    s.glow = A.glow;
    s.thickness = A.thickness;
    return s;
}

void doExport() {
    if (A.path.samples.size() < 2) { setStatus(L"Nothing to export."); return; }
    std::wstring f;
    if (!fileDialog(true, L"PNG image (*.png)\0*.png\0", L"png", f)) return;
    int w = 0, h = 0;
    if (exportPng(f, A.path, currentExportSettings(), &w, &h)) {
        wchar_t b[64];
        swprintf_s(b, L" (%dx%d)", w, h);
        setStatus(L"Exported " + f + b);
    } else {
        setStatus(L"PNG export failed.");
    }
}

void togglePlay() {
    if (A.recording || A.path.samples.size() < 2) return;
    if (A.playing) {
        A.playing = false;
    } else {
        if (A.viewN >= A.path.samples.size()) {  // at the end: restart
            A.playTime = 0;
            A.viewN = countAtTime(0);
        }
        A.scrub = true;
        A.playing = true;
        A.lastTick = GetTickCount64();
        syncDraw();
    }
    setPlayButton();
}

void onTick() {
    if (A.recording) {
        pumpRecorder();
        if (A.rec.paused()) setStatus(L"Paused: target window is not focused (next movement starts a new line).");
        else if (A.cacheInfo.find(L"Paused") == 0) setStatus(L"Recording...");
    }
    if (A.playing && !A.sliderDrag) {
        ULONGLONG now = GetTickCount64();
        A.playTime += double(now - A.lastTick) * SPEEDS[std::clamp<int>(int(SendMessageW(A.hSpeed, CB_GETCURSEL, 0, 0)), 0, 3)];
        A.lastTick = now;
        double d = durationMs();
        if (A.playTime >= d) {
            A.playTime = d;
            A.playing = false;
            setPlayButton();
        }
        A.viewN = A.playTime >= d ? A.path.samples.size() : countAtTime(A.playTime);
        setSliderFromTime();
        syncDraw();
    }
    if (A.dispDirty) InvalidateRect(A.canvas, nullptr, FALSE);
}

void updateHover(int mx, int my) {
    A.hoverIdx = SIZE_MAX;
    if (blindNow()) { updateInfo(); return; }
    if (A.path.samples.size() >= 2 && A.rend.scale() > 0) {
        Vec2 w = A.rend.toWorld(mx, my);
        Hit h = A.grid.nearest(A.path, w.x, w.y, 10.0 / A.rend.scale(), A.viewN);
        if (h.valid()) {
            A.hoverIdx = h.index;
            double cum = A.an.cumDist()[h.index];
            wchar_t b[256];
            swprintf_s(b, L"Sample #%zu    time %.3f s    speed %.0f px/s    segment length %.1f px    distance so far %.0f px",
                       h.index, A.path.samples[h.index].ms / 1000.0, segSpeed(A.path, h.index), segLength(A.path, h.index), cum);
            A.hoverText = b;
        }
    }
    updateInfo();
    InvalidateRect(A.canvas, nullptr, FALSE);
}

void clickCanvas(int mx, int my) {
    updateHover(mx, my);
    if (A.hoverIdx == SIZE_MAX || A.recording) return;
    A.playing = false;
    setPlayButton();
    A.scrub = true;
    A.viewN = A.hoverIdx + 1;
    A.playTime = double(A.path.samples[A.hoverIdx].ms) - A.path.samples.front().ms;
    setSliderFromTime();
    syncDraw();
}

void paintCanvas(HDC hdc) {
    if (blindNow()) {  // nothing drawn while recording in blind mode
        RECT rc{0, 0, A.cw, A.ch};
        FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(130, 130, 130));
        HGDIOBJ of = SelectObject(hdc, A.font);
        DrawTextW(hdc, L"Recording (blind mode). The preview appears when you stop.", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        return;
    }
    if (!A.dib) return;
    if (A.dispDirty) {
        toneMap(A.rend.accum(), A.cw, A.ch, A.brightness, A.gain, A.glow, A.bits);
        A.dispDirty = false;
    }
    HDC src = CreateCompatibleDC(hdc);
    HGDIOBJ oldSrc = SelectObject(src, A.dib);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, A.cw, A.ch);
    HGDIOBJ oldMem = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, A.cw, A.ch, src, 0, 0, SRCCOPY);

    // dim frame around the recorded bounds
    {
        Vec2 a = A.rend.toView(A.path.bounds.x, A.path.bounds.y);
        Vec2 b = A.rend.toView(A.path.bounds.x + A.path.bounds.w, A.path.bounds.y + A.path.bounds.h);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 80));
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, int(a.x), int(a.y), int(b.x), int(b.y));
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(pen);
    }
    auto seg = [&](size_t i, COLORREF c, int width) {
        const Sample& p0 = A.path.samples[i - 1];
        const Sample& p1 = A.path.samples[i];
        Vec2 a = A.rend.toView(p0.x, p0.y), b = A.rend.toView(p1.x, p1.y);
        HPEN pen = CreatePen(PS_SOLID, width, c);
        HGDIOBJ op = SelectObject(mem, pen);
        MoveToEx(mem, int(a.x), int(a.y), nullptr);
        LineTo(mem, int(b.x), int(b.y));
        SelectObject(mem, op);
        DeleteObject(pen);
    };
    if (A.scrub && A.viewN > 0 && A.viewN <= A.path.samples.size()) {  // bright trail + marker at the playhead
        size_t from = A.viewN > 120 ? A.viewN - 120 : 1;
        for (size_t i = std::max<size_t>(from, 1); i < A.viewN; ++i)
            if (A.path.isSegment(i)) seg(i, RGB(255, 255, 255), S(2));
        const Sample& m = A.path.samples[A.viewN - 1];
        Vec2 v = A.rend.toView(m.x, m.y);
        int r = S(6);
        HPEN pen = CreatePen(PS_SOLID, S(2), RGB(255, 90, 70));
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Ellipse(mem, int(v.x) - r, int(v.y) - r, int(v.x) + r, int(v.y) + r);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(pen);
    }
    if (A.hoverIdx != SIZE_MAX && A.hoverIdx < A.path.samples.size()) seg(A.hoverIdx, RGB(255, 210, 60), S(3));

    BitBlt(hdc, 0, 0, A.cw, A.ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldMem);
    DeleteObject(bmp);
    DeleteDC(mem);
    SelectObject(src, oldSrc);
    DeleteDC(src);
}

LRESULT CALLBACK CanvasProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        if (A.dib || blindNow()) paintCanvas(hdc);
        else FillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        updateHover(int(short(LOWORD(l))), int(short(HIWORD(l))));
        return 0;
    }
    case WM_MOUSELEAVE:
        A.hoverIdx = SIZE_MAX;
        updateInfo();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        clickCanvas(int(short(LOWORD(l))), int(short(HIWORD(l))));
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------- layout / creation ----------

void applyFont() {
    if (A.font) DeleteObject(A.font);
    A.font = CreateFontW(-MulDiv(9, A.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    EnumChildWindows(A.hwnd, [](HWND c, LPARAM) -> BOOL { SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(A.font), TRUE); return TRUE; }, 0);
}

void layout() {
    RECT cr;
    GetClientRect(A.hwnd, &cr);
    int pad = S(8), rh = S(26), gap = S(6);
    auto mv = [](HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); };
    int y = pad, x = pad;
    mv(A.hRec, x, y, S(110), rh); x += S(110) + gap;
    mv(A.hSettings, x, y, S(90), rh); x += S(90) + S(16);
    mv(A.hColor, x, y, S(110), S(200)); x += S(110) + S(16);
    mv(A.hOpen, x, y, S(60), rh); x += S(60) + gap;
    mv(A.hSave, x, y, S(60), rh); x += S(60) + gap;
    mv(A.hExport, x, y, S(100), rh);
    y += rh + gap; x = pad;
    int lblW = S(110), tbW = S(120), tbH = rh;  // labels carry the live value, e.g. "Glow 12"
    mv(A.hLblBright, x, y + S(4), lblW, rh); x += lblW;
    mv(A.hBright, x, y, tbW, tbH); x += tbW + S(12);
    mv(A.hLblGain, x, y + S(4), S(80), rh); x += S(80);
    mv(A.hGain, x, y, tbW, tbH); x += tbW + S(12);
    mv(A.hLblGlow, x, y + S(4), S(70), rh); x += S(70);
    mv(A.hGlow, x, y, tbW, tbH); x += tbW + S(16);
    mv(A.hLblThick, x, y + S(4), S(84), rh); x += S(84);
    mv(A.hThick, x, y, S(110), rh);
    y += rh + gap; x = pad;
    mv(A.hPlay, x, y, S(70), rh); x += S(70) + gap;
    mv(A.hSpeed, x, y, S(80), S(200)); x += S(80) + S(16);
    mv(A.hTime, x, y, std::max<int>(S(100), cr.right - x - pad), rh);
    y += rh + gap;
    int bottom = cr.bottom - S(48);
    mv(A.canvas, pad, y, std::max<int>(cr.right - 2 * pad, 10), std::max<int>(bottom - y, 10));
    mv(A.hStats, pad, bottom + S(4), cr.right - 2 * pad, S(20));
    mv(A.hInfo, pad, bottom + S(24), cr.right - 2 * pad, S(20));
    resizeCanvas();
}

HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, A.hwnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

HWND mkSlider(int id, int lo, int hi, int pos) {
    HWND h = mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, id);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
    SendMessageW(h, TBM_SETPOS, TRUE, pos);
    return h;
}

// A stock trackbar pages the thumb by ~10% when the channel is clicked. Instead jump the thumb to the click,
// notify the parent like a drag would, then replay the press on the thumb so the same press keeps dragging.
LRESULT CALLBACK TimeSliderProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_LBUTTONDOWN) {
        RECT ch, th;
        SendMessageW(h, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&ch));
        SendMessageW(h, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&th));
        int x = int(short(LOWORD(l)));
        int half = (th.right - th.left) / 2;
        int lo = ch.left + half, hi = ch.right - half;
        int range = int(SendMessageW(h, TBM_GETRANGEMAX, 0, 0)) - int(SendMessageW(h, TBM_GETRANGEMIN, 0, 0));
        double f = hi > lo ? double(std::clamp(x, lo, hi) - lo) / double(hi - lo) : 0.0;
        int pos = int(f * range + 0.5);
        SendMessageW(h, TBM_SETPOS, TRUE, pos);
        SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, pos), reinterpret_cast<LPARAM>(h));
        SendMessageW(h, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&th));
        l = MAKELPARAM((th.left + th.right) / 2, (th.top + th.bottom) / 2);
        // The trackbar holds the mouse capture until release; playback must not move the thumb meanwhile.
        A.sliderDrag = true;
        LRESULT r = DefSubclassProc(h, m, w, l);
        A.sliderDrag = false;
        A.lastTick = GetTickCount64();
        return r;
    } else if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, TimeSliderProc, 1);
    }
    return DefSubclassProc(h, m, w, l);
}

void createControls() {
    A.hRec = mk(L"BUTTON", L"Record", BS_PUSHBUTTON | WS_TABSTOP, ID_REC);
    setRecordLabel();
    A.hSettings = mk(L"BUTTON", L"Settings...", BS_PUSHBUTTON | WS_TABSTOP, ID_SETTINGS);
    A.hFilter = mk(L"BUTTON", L"Window filter", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FILTER);
    A.hTitle = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, ID_TITLE);
    SendMessageW(A.hTitle, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"title contains..."));
    A.hPick = mk(L"BUTTON", L"Pick", BS_PUSHBUTTON | WS_TABSTOP, ID_PICK);
    A.hColor = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_COLOR);
    for (const wchar_t* s : {L"White", L"By time", L"By speed"}) SendMessageW(A.hColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(A.hColor, CB_SETCURSEL, 0, 0);
    A.hOpen = mk(L"BUTTON", L"Open...", BS_PUSHBUTTON | WS_TABSTOP, ID_OPEN);
    A.hSave = mk(L"BUTTON", L"Save...", BS_PUSHBUTTON | WS_TABSTOP, ID_SAVE);
    A.hExport = mk(L"BUTTON", L"Export PNG...", BS_PUSHBUTTON | WS_TABSTOP, ID_EXPORT);
    A.hLblBright = mk(L"STATIC", L"Brightness", SS_LEFT, ID_LBL_BRIGHT);
    A.hBright = mkSlider(ID_BRIGHT, 1, 100, 20);  // value/20 -> 0.05 .. 5.0, default 1.0
    A.hLblGain = mk(L"STATIC", L"Gain", SS_LEFT, ID_LBL_GAIN);
    A.hGain = mkSlider(ID_GAIN, 1, 100, 20);      // value/100 -> default 0.2
    A.hLblGlow = mk(L"STATIC", L"Glow", SS_LEFT, ID_LBL_GLOW);
    A.hGlow = mkSlider(ID_GLOW, 0, 50, 4);
    A.hMonitor = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MONITOR);
    populateMonitors(false);
    A.hPlay = mk(L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, ID_PLAY);
    A.hSpeed = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_SPEED);
    for (const wchar_t* s : {L"1x", L"5x", L"20x", L"60x"}) SendMessageW(A.hSpeed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(A.hSpeed, CB_SETCURSEL, 1, 0);
    A.hBlind = mk(L"BUTTON", L"Blind recording", BS_AUTOCHECKBOX | WS_TABSTOP, ID_BLIND);
    SendMessageW(A.hBlind, BM_SETCHECK, BST_CHECKED, 0);  // on by default
    // These live in the Settings dialog: hidden here, re-parented into it while it is open.
    for (HWND h : {A.hFilter, A.hTitle, A.hPick, A.hMonitor, A.hBlind}) ShowWindow(h, SW_HIDE);
    A.hLblThick = mk(L"STATIC", L"Thickness 1", SS_LEFT, ID_LBL_THICK);
    A.hThick = mkSlider(ID_THICK, 1, 6, 1);
    A.hTime = mkSlider(ID_TIME, 0, 1000, 0);
    SendMessageW(A.hTime, TBM_SETPAGESIZE, 0, 50);
    SetWindowSubclass(A.hTime, TimeSliderProc, 1, 0);
    A.hStats = mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_STATS);
    A.hInfo = mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, ID_INFO);
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

// ---------- settings: persistence and dialog ----------

fs::path settingsFile() {
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
SavedSettings gSaved;

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
}

void saveSettings() {
    std::wstring f = settingsFile().wstring();
    auto put = [&](const wchar_t* k, const std::wstring& v) { WritePrivateProfileStringW(L"LineTracer", k, v.c_str(), f.c_str()); };
    wchar_t t[512] = {};
    GetWindowTextW(A.hTitle, t, 512);
    put(L"hotkeyVk", std::to_wstring(A.hkVk));
    put(L"hotkeyMods", std::to_wstring(A.hkMods));
    put(L"blind", SendMessageW(A.hBlind, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    put(L"filter", SendMessageW(A.hFilter, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    put(L"title", t);
    put(L"monitorAll", A.monAll ? L"1" : L"0");
    put(L"monitor", A.monName);
}

void applySettingsToControls() {
    SendMessageW(A.hBlind, BM_SETCHECK, gSaved.blind ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(A.hFilter, BM_SETCHECK, gSaved.filter ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(A.hTitle, gSaved.title.c_str());
    updateMonitorEnable();
}

// Hotkey control <-> MOD_* flags (the control uses HOTKEYF_SHIFT=1, CONTROL=2, ALT=4).
unsigned hkfToMod(unsigned f) { return ((f & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | ((f & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) | ((f & HOTKEYF_ALT) ? MOD_ALT : 0); }
unsigned modToHkf(unsigned m) { return ((m & MOD_SHIFT) ? HOTKEYF_SHIFT : 0) | ((m & MOD_CONTROL) ? HOTKEYF_CONTROL : 0) | ((m & MOD_ALT) ? HOTKEYF_ALT : 0); }

bool registerRecordHotkey() { return registerToggleHotkey(A.hwnd, HOTKEY_ID, A.hkMods, A.hkVk); }

bool hotkeyKeyAllowed(unsigned mods, unsigned vk) {
    return mods != 0 || (vk >= VK_F1 && vk <= VK_F24);  // a bare letter would steal that key everywhere
}

LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == ID_SET_CLOSE || id == IDOK || id == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        if (id == ID_SET_HOTKEY && code == EN_CHANGE) {
            LRESULT v = SendMessageW(reinterpret_cast<HWND>(l), HKM_GETHOTKEY, 0, 0);
            unsigned vk = unsigned(LOBYTE(v)), mods = hkfToMod(unsigned(HIBYTE(LOWORD(v))));
            HWND hint = GetDlgItem(h, 0x7000);
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
    RECT cr = {0, 0, S(480), S(228)};
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

    auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD st, int id, int cx, int cy, int cw_, int ch_) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | st, S(cx), S(cy), S(cw_), S(ch_), d, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    };
    auto place = [&](HWND c, int cx, int cy, int cw_, int ch_) {
        SetParent(c, d);
        MoveWindow(c, S(cx), S(cy), S(cw_), S(ch_), TRUE);
        ShowWindow(c, SW_SHOW);
    };
    child(L"STATIC", L"Monitor", SS_LEFT, 0x7001, 14, 22, 110, 20);
    place(A.hMonitor, 140, 18, 326, 200);
    place(A.hFilter, 14, 56, 120, 26);
    place(A.hTitle, 140, 56, 240, 26);
    place(A.hPick, 388, 56, 78, 26);
    place(A.hBlind, 14, 94, 300, 26);
    child(L"STATIC", L"Record / stop hotkey", SS_LEFT, 0x7002, 14, 136, 120, 20);
    HWND hk = child(HOTKEY_CLASSW, L"", WS_TABSTOP, ID_SET_HOTKEY, 140, 132, 200, 26);
    SendMessageW(hk, HKM_SETHOTKEY, MAKEWORD(A.hkVk, modToHkf(A.hkMods)), 0);
    child(L"STATIC", L"Tip: use a modifier combo such as Ctrl+Alt+R so the game does not also see the key.", SS_LEFT, 0x7000, 14, 168, 452, 20);
    child(L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_SET_CLOSE, 386, 190, 80, 28);
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

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        A.hwnd = h;
        A.dpi = int(GetDpiForWindow(h));
        loadSettings();
        createControls();
        applySettingsToControls();
        applyFont();
        SetTimer(h, TIMER_TICK, 33, nullptr);
        if (!registerRecordHotkey()) A.status = hotkeyName(A.hkMods, A.hkVk) + L" is already taken by another program; use the Record button or pick another key in Settings.";
        else A.status = L"Press " + hotkeyName(A.hkMods, A.hkVk) + L" or Record to start.";
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
        if (src == A.hTime) onTimeSlider();
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
            if (SendMessageW(A.hFilter, BM_GETCHECK, 0, 0) == BST_CHECKED)
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

    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&ic);

    WNDCLASSW sc{};
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    sc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
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
    return int(msg.wParam);
}

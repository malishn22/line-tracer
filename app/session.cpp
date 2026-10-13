// LineTracer - recording, playback, monitor choice and file open/save/export.
#include "session.h"

#include <initguid.h>
#include <KnownFolders.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "../core/fileio.h"
#include "../core/png.h"
#include "canvas.h"
#include "status.h"

namespace fs = std::filesystem;

namespace app {

size_t countAtTime(double t) {
    const auto& v = A.path.samples;
    if (v.empty()) return 0;
    double t0 = v.front().ms;
    auto it = std::upper_bound(v.begin(), v.end(), t0 + t, [](double tt, const Sample& s) { return tt < s.ms; });
    return std::max<size_t>(1, size_t(it - v.begin()));
}

// Rebuild everything derived from the samples and park the playhead at the end. Colour-by-time is
// normalised to the final duration here, so this is also the post-recording catch-up.
static void rebuildDerived() {
    A.an.reset();
    A.an.update(A.path);
    A.grid.reset();
    A.grid.sync(A.path);
    A.scrub = false;
    A.viewN = A.path.samples.size();
    A.playTime = durationMs();
    A.timeSpan = std::max(durationMs(), 1.0);
    A.rend.setTimeSpan(A.timeSpan);
}

// Replace the whole path (load) and rebuild every derived structure.
static void onPathReplaced() {
    rebuildDerived();
    A.playing = false;
    A.hoverIdx = SIZE_MAX;
    setPlayButton();
    setSliderFromTime();
    A.rend.configure(A.cw, A.ch, A.path.bounds, MARGIN);
    redrawAll();
    updateStats();
}

static fs::path lineTraceDir() {
    fs::path dir;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        dir = docs;
        CoTaskMemFree(docs);
    }
    fs::path legacy = dir / L"LineTrace";
    dir /= L"LineTracer";
    std::error_code ec;
    // Older builds saved to Documents\LineTrace; carry those recordings over once.
    if (fs::exists(legacy, ec) && !fs::exists(dir, ec)) fs::rename(legacy, dir, ec);
    fs::create_directories(dir, ec);
    return dir;
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

static void appendSamples(const std::vector<Sample>& v) {
    if (v.empty()) return;
    A.path.samples.insert(A.path.samples.end(), v.begin(), v.end());
    if (blindNow()) return;  // derived data (drawing, stats, hit grid) is built once, after Stop
    A.an.update(A.path);
    A.grid.sync(A.path);
    if (!A.scrub) A.viewN = A.path.samples.size();
    syncDraw();
    updateStats();
}

static void pumpRecorder() {
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

static void startRecording() {
    CaptureConfig cfg;
    cfg.filterEnabled = isChecked(A.hFilter);
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
    A.blindActive = isChecked(A.hBlind);
    A.timeSpan = 60000;
    A.rend.setTimeSpan(A.timeSpan);
    setPlayButton();
    SendMessageW(A.hTime, TBM_SETPOS, TRUE, 0);
    A.playTime = 0;
    redrawAll();
    updateStats();
    if (!A.rec.start(cfg)) return;
    A.recording = true;
    enableControlsForRecording(true);
    updateStats();  // shows the blind-mode text when blind
    InvalidateRect(A.canvas, nullptr, FALSE);
    setStatus(cfg.filterEnabled ? L"Recording (waiting for the target window to be focused)..." : L"Recording...");
}

static void stopRecording() {
    A.rec.stop();
    pumpRecorder();
    A.recording = false;
    A.blindActive = false;
    enableControlsForRecording(false);
    rebuildDerived();  // a catch-up unless blind mode deferred the work
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

static ExportSettings currentExportSettings() {
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
    static bool dotOn = false;
    bool dot = (GetTickCount64() / 500) % 2 == 0;
    if (blindNow() && dot != dotOn) InvalidateRect(A.canvas, nullptr, FALSE);  // pulsing record dot
    dotOn = dot;
    if (A.dispDirty) InvalidateRect(A.canvas, nullptr, FALSE);
}

}  // namespace app

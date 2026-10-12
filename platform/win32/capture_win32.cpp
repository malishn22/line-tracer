// Windows capture backend.
//
// APIs used (complete list): GetCursorPos, GetForegroundWindow, GetWindowTextW, GetClientRect,
// ClientToScreen, GetSystemMetrics, RegisterHotKey/UnregisterHotKey, QueryPerformanceCounter,
// timeBeginPeriod/timeEndPeriod, Sleep. Nothing here hooks input, injects, opens other
// processes, or draws on other windows.
#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#include "../capture.h"

namespace lt {

struct CursorRecorder::Impl {
    std::thread th;
    std::atomic<bool> stopFlag{false}, isRunning{false}, isPaused{false};
    mutable std::mutex mu;
    std::vector<Sample> pending;
    Bounds b;
    bool haveBounds = false;
    LARGE_INTEGER t0{}, freq{};
    CaptureConfig cfg;

    unsigned nowMs() const {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return unsigned((t.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
    }

    static std::wstring lower(std::wstring s) {
        for (auto& c : s) c = wchar_t(towlower(c));
        return s;
    }

    void run() {
        timeBeginPeriod(1);
        const std::wstring needle = lower(cfg.titleSubstring);
        const bool useRegion = cfg.restrictToRegion && !cfg.filterEnabled;
        const Bounds region = cfg.region;
        bool match = !cfg.filterEnabled;
        bool havePos = false, pendingBreak = false;
        POINT last{0, 0};
        unsigned tick = 0;
        while (!stopFlag.load()) {
            Sleep(4);
            if (cfg.filterEnabled && (tick++ % 4) == 0) {  // check the foreground title every ~16 ms
                match = false;
                HWND fg = GetForegroundWindow();
                if (fg) {
                    wchar_t buf[512];
                    int n = GetWindowTextW(fg, buf, 512);
                    if (n > 0 && lower(std::wstring(buf, size_t(n))).find(needle) != std::wstring::npos) {
                        match = true;
                        std::lock_guard<std::mutex> lk(mu);
                        if (!haveBounds) {  // canvas = the target's client rect, fixed at first focus
                            RECT rc;
                            POINT o{0, 0};
                            if (GetClientRect(fg, &rc) && ClientToScreen(fg, &o) && rc.right > 0 && rc.bottom > 0) {
                                b = Bounds{o.x, o.y, rc.right, rc.bottom};
                                haveBounds = true;
                            } else {
                                match = false;
                            }
                        }
                    }
                }
            }
            isPaused = cfg.filterEnabled && !match;
            if (!match) {
                pendingBreak = true;
                continue;
            }
            POINT pt;
            if (!GetCursorPos(&pt)) continue;
            if (useRegion && (pt.x < region.x || pt.y < region.y || pt.x >= region.x + region.w || pt.y >= region.y + region.h)) {
                pendingBreak = true;  // cursor is on another monitor: store nothing, no line across the gap
                continue;
            }
            if (havePos && pt.x == last.x && pt.y == last.y && !pendingBreak) continue;
            uint32_t flags = (pendingBreak && havePos) ? FLAG_BREAK : 0;
            pendingBreak = false;
            last = pt;
            havePos = true;
            std::lock_guard<std::mutex> lk(mu);
            pending.push_back(Sample{pt.x, pt.y, nowMs(), flags});
        }
        timeEndPeriod(1);
    }
};

CursorRecorder::CursorRecorder() : p_(new Impl) {}
CursorRecorder::~CursorRecorder() {
    stop();
    delete p_;
}

bool CursorRecorder::start(const CaptureConfig& cfg) {
    if (p_->isRunning) return false;
    p_->cfg = cfg;
    p_->pending.clear();
    p_->haveBounds = false;
    p_->stopFlag = false;
    p_->isPaused = false;
    QueryPerformanceFrequency(&p_->freq);
    QueryPerformanceCounter(&p_->t0);
    if (!cfg.filterEnabled) {
        if (cfg.restrictToRegion && cfg.region.w > 0 && cfg.region.h > 0) {
            p_->b = cfg.region;  // a single monitor
        } else {
            p_->cfg.restrictToRegion = false;
            p_->b = Bounds{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        }
        p_->haveBounds = true;
    }
    p_->isRunning = true;
    p_->th = std::thread([this] { p_->run(); });
    return true;
}

void CursorRecorder::stop() {
    if (!p_->isRunning) return;
    p_->stopFlag = true;
    if (p_->th.joinable()) p_->th.join();
    p_->isRunning = false;
    p_->isPaused = false;
}

bool CursorRecorder::running() const { return p_->isRunning; }
bool CursorRecorder::paused() const { return p_->isPaused; }

void CursorRecorder::drain(std::vector<Sample>& out) {
    std::lock_guard<std::mutex> lk(p_->mu);
    out.insert(out.end(), p_->pending.begin(), p_->pending.end());
    p_->pending.clear();
}

bool CursorRecorder::bounds(Bounds& b) const {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->haveBounds) b = p_->b;
    return p_->haveBounds;
}

unsigned CursorRecorder::elapsedMs() const { return p_->isRunning ? p_->nowMs() : 0; }

std::vector<MonitorInfo> listMonitors() {
    std::vector<MonitorInfo> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hm, &mi)) {
                MonitorInfo m;
                m.name = mi.szDevice;
                m.rect = Bounds{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                                mi.rcMonitor.bottom - mi.rcMonitor.top};
                m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back(m);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.rect.x != b.rect.x ? a.rect.x < b.rect.x : a.rect.y < b.rect.y;
    });
    return out;
}

bool registerToggleHotkey(void* w, int id, unsigned mods, unsigned vk) {
    return RegisterHotKey(static_cast<HWND>(w), id, mods | MOD_NOREPEAT, vk) != 0;
}
void unregisterToggleHotkey(void* w, int id) { UnregisterHotKey(static_cast<HWND>(w), id); }

std::wstring foregroundWindowTitle(void* exclude) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == static_cast<HWND>(exclude)) return L"";
    wchar_t buf[512];
    int n = GetWindowTextW(fg, buf, 512);
    return n > 0 ? std::wstring(buf, size_t(n)) : L"";
}

}  // namespace lt

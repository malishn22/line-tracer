// LineTracer - status/label text and control enable state.
#include "status.h"

namespace app {

void updateTimeLabel() {
    std::wstring t;
    if (blindNow()) t = L"";
    else if (A.recording) t = fmtDuration(durationMs());
    else t = fmtDuration(A.playTime) + L" / " + fmtDuration(durationMs());
    setText(A.hLblTime, t, &A.cacheTime);
}

void updateStats() {
    updateTimeLabel();
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
    updateTimeLabel();
}

void setPlayButton() { SetWindowTextW(A.hPlay, A.playing ? L"Pause" : L"Play"); }

// The monitor choice only applies when the window filter is off (the filtered window is the canvas then).
void updateMonitorEnable() {
    bool filterOn = isChecked(A.hFilter);
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
    ui::setButtonKind(A.hRec, A.recording ? ui::BtnKind::Danger : ui::BtnKind::Primary);
}

void enableControlsForRecording(bool rec) {
    BOOL idle = rec ? FALSE : TRUE;
    for (HWND h : {A.hOpen, A.hSave, A.hExport, A.hPlay, A.hTime, A.hFilter, A.hTitle, A.hPick, A.hBlind, A.hSettings}) EnableWindow(h, idle);
    setRecordLabel();
    updateMonitorEnable();
}

}  // namespace app

// LineTracer - the App state object and its small helpers.
#include "app_state.h"

#include <cstdio>

namespace app {

App A;

int S(int v) { return ui::S(v); }

bool blindNow() { return A.recording && A.blindActive; }

bool isChecked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

void setText(HWND h, const std::wstring& s, std::wstring* cache) {
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

}  // namespace app

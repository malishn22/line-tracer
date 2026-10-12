// LineTracer - the preview canvas: drawing state, painting, hover and click-to-seek.
#include "canvas.h"

#include <algorithm>

#include "status.h"

namespace app {

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

static void updateHover(int mx, int my) {
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

static void clickCanvas(int mx, int my) {
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

// Nothing is drawn while recording in blind mode: a black panel, a caption and a pulsing record dot.
static void paintBlindOverlay(HDC hdc) {
    RECT rc{0, 0, A.cw, A.ch};
    FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(235, 235, 235));
    HGDIOBJ of = SelectObject(hdc, ui::fontLarge());
    RECT title = rc;
    title.bottom = rc.bottom / 2 + S(2);
    DrawTextW(hdc, L"Recording in blind mode", -1, &title, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
    SetTextColor(hdc, RGB(130, 130, 140));
    SelectObject(hdc, A.font);
    RECT sub = rc;
    sub.top = rc.bottom / 2 + S(10);
    DrawTextW(hdc, L"The preview appears when you stop.", -1, &sub, DT_CENTER | DT_TOP | DT_SINGLELINE);
    SelectObject(hdc, of);
    // pulsing record dot above the title
    if ((GetTickCount64() / 500) % 2 == 0) {
        HBRUSH br = CreateSolidBrush(ui::theme().danger);
        HGDIOBJ ob = SelectObject(hdc, br);
        HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
        int cx = rc.right / 2, cy = rc.bottom / 2 - S(48), r = S(7);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(br);
    }
}

static void drawSegment(HDC dc, size_t i, COLORREF c, int width) {
    const Sample& p0 = A.path.samples[i - 1];
    const Sample& p1 = A.path.samples[i];
    Vec2 a = A.rend.toView(p0.x, p0.y), b = A.rend.toView(p1.x, p1.y);
    HPEN pen = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, int(a.x), int(a.y), nullptr);
    LineTo(dc, int(b.x), int(b.y));
    SelectObject(dc, op);
    DeleteObject(pen);
}

// Dim frame around the recorded bounds.
static void paintBoundsFrame(HDC dc) {
    Vec2 a = A.rend.toView(A.path.bounds.x, A.path.bounds.y);
    Vec2 b = A.rend.toView(A.path.bounds.x + A.path.bounds.w, A.path.bounds.y + A.path.bounds.h);
    HPEN pen = CreatePen(PS_SOLID, 1, ui::theme().canvasFrame);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, int(a.x), int(a.y), int(b.x), int(b.y));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

// Bright trail + marker at the playhead, and the hovered segment.
static void paintPlayheadAndHover(HDC dc) {
    if (A.scrub && A.viewN > 0 && A.viewN <= A.path.samples.size()) {
        size_t from = A.viewN > 120 ? A.viewN - 120 : 1;
        for (size_t i = std::max<size_t>(from, 1); i < A.viewN; ++i)
            if (A.path.isSegment(i)) drawSegment(dc, i, ui::theme().trail, S(2));
        const Sample& m = A.path.samples[A.viewN - 1];
        Vec2 v = A.rend.toView(m.x, m.y);
        int r = S(6);
        HPEN pen = CreatePen(PS_SOLID, S(2), ui::theme().marker);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, int(v.x) - r, int(v.y) - r, int(v.x) + r, int(v.y) + r);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
    if (A.hoverIdx != SIZE_MAX && A.hoverIdx < A.path.samples.size()) drawSegment(dc, A.hoverIdx, ui::theme().hover, S(3));
}

static void paintCanvas(HDC hdc) {
    if (blindNow()) {
        paintBlindOverlay(hdc);
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
    paintBoundsFrame(mem);
    paintPlayheadAndHover(mem);

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

}  // namespace app

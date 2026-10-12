#include "renderer.h"

#include <algorithm>
#include <cmath>

#include "stats.h"

namespace lt {

void Renderer::configure(int W, int H, const Bounds& bounds, int margin) {
    W_ = std::max(W, 1);
    H_ = std::max(H, 1);
    b_ = bounds;
    double bw = std::max(b_.w, 1), bh = std::max(b_.h, 1);
    double aw = std::max(W_ - 2 * margin, 1), ah = std::max(H_ - 2 * margin, 1);
    scale_ = std::min(aw / bw, ah / bh);
    ox_ = (W_ - bw * scale_) / 2;
    oy_ = (H_ - bh * scale_) / 2;
    acc_.assign(size_t(W_) * H_ * 3, 0.0f);
    drawn_ = 0;
}

void Renderer::clear() {
    std::fill(acc_.begin(), acc_.end(), 0.0f);
    drawn_ = 0;
}

void Renderer::setMode(ColorMode m) {
    mode_ = m;
    clear();
}

void Renderer::setTimeSpan(double ms) {
    span_ = std::max(ms, 1.0);
    if (mode_ == ColorMode::Time) clear();
}

void Renderer::setThickness(int px) {
    thick_ = std::min(8, std::max(1, px));
    clear();
}

void Renderer::drawTo(const Path& p, size_t n) {
    n = std::min(n, p.samples.size());
    if (n < drawn_) clear();
    for (size_t i = std::max<size_t>(drawn_, 1); i < n; ++i)
        if (p.isSegment(i)) drawSegment(p, i);
    drawn_ = n;
}

static void hsv(float h, float s, float v, float out[3]) {  // h in degrees
    float c = v * s, hp = h / 60.0f;
    float x = c * (1 - std::fabs(std::fmod(hp, 2.0f) - 1));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    float m = v - c;
    out[0] = r + m; out[1] = g + m; out[2] = b + m;
}

static void segColor(ColorMode mode, double span, const Path& p, size_t i, float c[3]) {
    switch (mode) {
    case ColorMode::White:
        c[0] = c[1] = c[2] = 1.0f;
        break;
    case ColorMode::Time: {
        float t = float(std::min(1.0, std::max(0.0, p.samples[i].ms / span)));
        hsv(240.0f * (1.0f - t), 0.9f, 1.0f, c);  // blue -> red
        break;
    }
    case ColorMode::Speed: {
        static const float stops[4][3] = {{0.2f, 0.4f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.15f, 0.1f}};
        const double lo = std::log10(20.0), hi = std::log10(5000.0);  // px/s, log scale
        double sp = std::max(segSpeed(p, i), 1.0);
        float t = float(std::min(1.0, std::max(0.0, (std::log10(sp) - lo) / (hi - lo))));
        float f = t * 3.0f;
        int k = std::min(2, int(f));
        float u = f - k;
        for (int j = 0; j < 3; ++j) c[j] = stops[k][j] * (1 - u) + stops[k + 1][j] * u;
        break;
    }
    }
}

void Renderer::drawSegment(const Path& p, size_t i) {
    const Sample& a = p.samples[i - 1];
    const Sample& b = p.samples[i];
    float c[3];
    segColor(mode_, span_, p, i, c);
    Vec2 va = toView(a.x, a.y), vb = toView(b.x, b.y);
    double dx = vb.x - va.x, dy = vb.y - va.y, len = std::hypot(dx, dy);
    if (thick_ <= 1 || len < 1e-9) {
        line(float(va.x), float(va.y), float(vb.x), float(vb.y), c);
        return;
    }
    double nx = -dy / len, ny = dx / len;  // unit normal; lines are spaced 1 px apart, centred on the segment
    for (int k = 0; k < thick_; ++k) {
        double o = k - (thick_ - 1) * 0.5;
        line(float(va.x + nx * o), float(va.y + ny * o), float(vb.x + nx * o), float(vb.y + ny * o), c);
    }
}

void Renderer::plot(int x, int y, float w, const float c[3]) {
    if (x < 0 || y < 0 || x >= W_ || y >= H_ || w <= 0) return;
    float* d = &acc_[(size_t(y) * W_ + x) * 3];
    d[0] += c[0] * w;
    d[1] += c[1] * w;
    d[2] += c[2] * w;
}

static float fpart(float v) { return v - std::floor(v); }
static float rfpart(float v) { return 1.0f - fpart(v); }

// Xiaolin Wu line, additive, clipped to the buffer.
void Renderer::line(float x0, float y0, float x1, float y1, const float c[3]) {
    const float LIM = 1e6f;
    if (!(std::fabs(x0) < LIM && std::fabs(y0) < LIM && std::fabs(x1) < LIM && std::fabs(y1) < LIM)) return;
    bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    auto P = [&](int x, int y, float w) { if (steep) plot(y, x, w, c); else plot(x, y, w, c); };

    float dx = x1 - x0, dy = y1 - y0;
    float grad = dx == 0.0f ? 1.0f : dy / dx;

    float xend = std::round(x0);
    float yend = y0 + grad * (xend - x0);
    float xgap = rfpart(x0 + 0.5f);
    int xp1 = int(xend), yp1 = int(std::floor(yend));
    P(xp1, yp1, rfpart(yend) * xgap);
    P(xp1, yp1 + 1, fpart(yend) * xgap);
    float inter = yend + grad;

    xend = std::round(x1);
    yend = y1 + grad * (xend - x1);
    xgap = fpart(x1 + 0.5f);
    int xp2 = int(xend), yp2 = int(std::floor(yend));
    P(xp2, yp2, rfpart(yend) * xgap);
    P(xp2, yp2 + 1, fpart(yend) * xgap);

    int xs = xp1 + 1, xe = xp2 - 1;
    int lim = steep ? H_ : W_;  // major-axis extent
    if (xs < 0) { inter += grad * float(0 - xs); xs = 0; }
    if (xe > lim - 1) xe = lim - 1;
    for (int x = xs; x <= xe; ++x) {
        int yi = int(std::floor(inter));
        P(x, yi, rfpart(inter));
        P(x, yi + 1, fpart(inter));
        inter += grad;
    }
}

// ---- display ----

static const std::vector<float>& toneLut() {
    static std::vector<float> lut = [] {
        std::vector<float> t(4097);
        for (int i = 0; i <= 4096; ++i) t[i] = 1.0f - std::exp(-8.0f * float(i) / 4096.0f);
        return t;
    }();
    return lut;
}

static void blurLine(const float* in, float* out, int n, size_t st, int r) {
    float inv = 1.0f / float(2 * r + 1), sum = 0;
    for (int k = 0; k <= std::min(r, n - 1); ++k) sum += in[k * st];
    for (int x = 0; x < n; ++x) {
        out[x * st] = sum * inv;
        if (x + r + 1 < n) sum += in[(x + r + 1) * st];
        if (x - r >= 0) sum -= in[(x - r) * st];
    }
}

// one separable box blur (horizontal then vertical), zero outside the image
static void boxBlur(std::vector<float>& img, std::vector<float>& tmp, int W, int H, int r) {
    for (int y = 0; y < H; ++y)
        for (int c = 0; c < 3; ++c) blurLine(&img[size_t(y) * W * 3 + c], &tmp[size_t(y) * W * 3 + c], W, 3, r);
    for (int x = 0; x < W; ++x)
        for (int c = 0; c < 3; ++c) blurLine(&tmp[size_t(x) * 3 + c], &img[size_t(x) * 3 + c], H, size_t(W) * 3, r);
}

void toneMap(const std::vector<float>& acc, int W, int H, float brightness, float gain, float glow,
             uint32_t* out) {
    const std::vector<float>& lut = toneLut();
    size_t n = size_t(W) * H * 3;
    std::vector<float> blur;
    float glowAmt = 0;
    if (glow > 0) {
        // Fixed radius in output pixels: a 2x export gets the same tight halo as the window preview
        // (it used to scale with image size, which made big exports hazy).
        const int r = 4;
        blur = acc;
        std::vector<float> tmp(n);
        boxBlur(blur, tmp, W, H, r);
        boxBlur(blur, tmp, W, H, r);  // two passes
        glowAmt = glow / 50.0f * float(2 * r + 1) * 0.7f;
    }
    const float k = gain * brightness;
    for (size_t px = 0; px < size_t(W) * H; ++px) {
        uint32_t ch[3];
        for (int c = 0; c < 3; ++c) {
            float v = acc[px * 3 + c];
            if (glowAmt > 0) v += glowAmt * blur[px * 3 + c];
            float a = v * k;
            float f;
            if (!(a > 0)) f = 0;
            else if (a >= 8.0f) f = lut[4096];
            else {
                float pos = a * (4096.0f / 8.0f);
                int i0 = int(pos);
                float u = pos - i0;
                f = lut[i0] * (1 - u) + lut[i0 + 1] * u;
            }
            ch[c] = uint32_t(f * 255.0f + 0.5f);
        }
        out[px] = (ch[0] << 16) | (ch[1] << 8) | ch[2];
    }
}

}  // namespace lt

#pragma once
#include <vector>

#include "path.h"

namespace lt {

enum class ColorMode { White = 0, Time = 1, Speed = 2 };

struct Vec2 {
    double x, y;
};

// Software renderer: float RGB accumulation buffer, additive anti-aliased (Wu) lines.
// Display (tone map + glow) is a separate pass, so brightness/gain/glow never need a re-render.
class Renderer {
public:
    // Resets the buffer. The bounds are letterboxed into W x H with `margin` px around.
    void configure(int W, int H, const Bounds& bounds, int margin = 0);
    void clear();  // zero the buffer and forget what was drawn
    void setMode(ColorMode m);
    void setTimeSpan(double ms);  // colour-by-time normalisation (ms mapped to the red end)
    // Line width in output pixels (1..8): n parallel 1 px lines side by side, so brightness per pixel
    // stays the same. Like the colour mode this changes the accumulation, so it clears the buffer.
    void setThickness(int px);
    int thickness() const { return thick_; }

    // Draw segments for samples [0, n). Incremental when n >= what was drawn, else clears and redraws.
    void drawTo(const Path& p, size_t n);
    size_t drawn() const { return drawn_; }

    Vec2 toView(double x, double y) const { return {(x - b_.x) * scale_ + ox_, (y - b_.y) * scale_ + oy_}; }
    Vec2 toWorld(double vx, double vy) const { return {(vx - ox_) / scale_ + b_.x, (vy - oy_) / scale_ + b_.y}; }
    double scale() const { return scale_; }
    int width() const { return W_; }
    int height() const { return H_; }
    ColorMode mode() const { return mode_; }
    double timeSpan() const { return span_; }
    const std::vector<float>& accum() const { return acc_; }

private:
    void drawSegment(const Path& p, size_t i);
    void line(float x0, float y0, float x1, float y1, const float c[3]);
    void plot(int x, int y, float w, const float c[3]);

    int W_ = 0, H_ = 0;
    Bounds b_;
    double scale_ = 1, ox_ = 0, oy_ = 0;
    ColorMode mode_ = ColorMode::White;
    double span_ = 60000;
    int thick_ = 1;
    size_t drawn_ = 0;
    std::vector<float> acc_;
};

// accumulation -> 32-bit 0x00RRGGBB pixels. Glow strength 0..50. out must hold W*H values.
void toneMap(const std::vector<float>& acc, int W, int H, float brightness, float gain, float glow,
             uint32_t* out);

}  // namespace lt

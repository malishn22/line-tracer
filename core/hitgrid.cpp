#include "hitgrid.h"

#include <algorithm>
#include <cmath>

namespace lt {

static double pointSegDist(double px, double py, double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay;
    double l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0;
    t = std::min(1.0, std::max(0.0, t));
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

static double segDist(const Path& p, size_t i, double x, double y) {
    const Sample& a = p.samples[i - 1];
    const Sample& b = p.samples[i];
    return pointSegDist(x, y, a.x, a.y, b.x, b.y);
}

static uint64_t key(int cx, int cy) { return (uint64_t(uint32_t(cx)) << 32) | uint32_t(cy); }

void HitGrid::reset() {
    cells_.clear();
    indexed_ = 0;
}

void HitGrid::insert(const Path& p, size_t i) {
    const Sample& a = p.samples[i - 1];
    const Sample& b = p.samples[i];
    double len = std::hypot(double(b.x) - a.x, double(b.y) - a.y);
    // Sample along the segment every cell/2 px; the query pads its cell range by one cell to match.
    int steps = std::max(1, int(std::ceil(len / (cell_ * 0.5))));
    uint64_t last = ~0ull;
    for (int k = 0; k <= steps; ++k) {
        double t = double(k) / steps;
        int cx = int(std::floor((a.x + (b.x - a.x) * t) / cell_));
        int cy = int(std::floor((a.y + (b.y - a.y) * t) / cell_));
        uint64_t kk = key(cx, cy);
        if (kk == last) continue;
        last = kk;
        auto& v = cells_[kk];
        if (v.empty() || v.back() != i) v.push_back(uint32_t(i));
    }
}

void HitGrid::sync(const Path& p) {
    if (indexed_ > p.samples.size()) reset();
    for (size_t i = std::max<size_t>(indexed_, 1); i < p.samples.size(); ++i)
        if (p.isSegment(i)) insert(p, i);
    indexed_ = p.samples.size();
}

Hit HitGrid::nearest(const Path& p, double x, double y, double maxDist, size_t limit) const {
    Hit best;
    best.dist = maxDist;
    int x0 = int(std::floor((x - maxDist) / cell_)) - 1, x1 = int(std::floor((x + maxDist) / cell_)) + 1;
    int y0 = int(std::floor((y - maxDist) / cell_)) - 1, y1 = int(std::floor((y + maxDist) / cell_)) + 1;
    for (int cy = y0; cy <= y1; ++cy)
        for (int cx = x0; cx <= x1; ++cx) {
            auto it = cells_.find(key(cx, cy));
            if (it == cells_.end()) continue;
            for (uint32_t i : it->second) {
                if (i >= limit || i >= p.samples.size()) continue;
                double d = segDist(p, i, x, y);
                if (d < best.dist || (d == best.dist && best.valid() && i < best.index) ||
                    (d == best.dist && !best.valid() && d <= maxDist)) {
                    best.index = i;
                    best.dist = d;
                }
            }
        }
    return best;
}

Hit bruteNearest(const Path& p, double x, double y, double maxDist, size_t limit) {
    Hit best;
    best.dist = maxDist;
    for (size_t i = 1; i < p.samples.size() && i < limit; ++i) {
        if (!p.isSegment(i)) continue;
        double d = segDist(p, i, x, y);
        if (d < best.dist || (d == best.dist && best.valid() && i < best.index) ||
            (d == best.dist && !best.valid() && d <= maxDist)) {
            best.index = i;
            best.dist = d;
        }
    }
    return best;
}

}  // namespace lt

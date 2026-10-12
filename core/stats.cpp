#include "stats.h"

#include <algorithm>
#include <cmath>

namespace lt {

double segLength(const Path& p, size_t i) {
    if (!p.isSegment(i)) return 0;
    const Sample& a = p.samples[i - 1];
    const Sample& b = p.samples[i];
    return std::hypot(double(b.x) - a.x, double(b.y) - a.y);
}

static double segDtMs(const Path& p, size_t i) {
    const Sample& a = p.samples[i - 1];
    const Sample& b = p.samples[i];
    double dt = (b.ms >= a.ms) ? double(b.ms - a.ms) : 0.0;
    return std::max(dt, 1.0);  // avoid divide-by-zero for same-millisecond samples
}

double segSpeed(const Path& p, size_t i) {
    if (!p.isSegment(i)) return 0;
    return segLength(p, i) / (segDtMs(p, i) / 1000.0);
}

void Analysis::reset() {
    cum_.clear();
    total_ = peak_ = activeMs_ = 0;
}

void Analysis::update(const Path& p) {
    size_t n = p.samples.size();
    if (cum_.size() > n) reset();
    for (size_t i = cum_.size(); i < n; ++i) {
        double prev = i ? cum_[i - 1] : 0.0;
        if (p.isSegment(i)) {
            double len = segLength(p, i);
            prev += len;
            total_ += len;
            activeMs_ += segDtMs(p, i);
            peak_ = std::max(peak_, segSpeed(p, i));
        }
        cum_.push_back(prev);
    }
}

Stats Analysis::stats(const Path& p) const {
    Stats s;
    s.points = p.samples.size();
    if (s.points >= 2) s.durationMs = double(p.samples.back().ms) - p.samples.front().ms;
    s.totalDist = total_;
    s.peakSpeed = peak_;
    s.avgSpeed = activeMs_ > 0 ? total_ / (activeMs_ / 1000.0) : 0.0;
    return s;
}

}  // namespace lt

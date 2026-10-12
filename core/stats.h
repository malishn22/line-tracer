#pragma once
#include "path.h"

namespace lt {

struct Stats {
    size_t points = 0;
    double durationMs = 0;
    double totalDist = 0;   // px, break segments excluded
    double avgSpeed = 0;    // px/s over active time (break gaps excluded)
    double peakSpeed = 0;   // px/s
};

double segLength(const Path& p, size_t i);  // 0 if i is not a segment
double segSpeed(const Path& p, size_t i);   // px/s, 0 if not a segment

// Incremental: call update() after appending samples; reset() if the path is replaced.
class Analysis {
public:
    void reset();
    void update(const Path& p);
    const std::vector<double>& cumDist() const { return cum_; }  // distance travelled up to sample i
    Stats stats(const Path& p) const;

private:
    std::vector<double> cum_;
    double total_ = 0, peak_ = 0, activeMs_ = 0;
};

}  // namespace lt

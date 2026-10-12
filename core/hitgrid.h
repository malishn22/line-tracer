#pragma once
#include <unordered_map>

#include "path.h"

namespace lt {

struct Hit {
    size_t index = SIZE_MAX;  // segment i (joins sample i-1 and i); SIZE_MAX = none
    double dist = 0;          // world px from the query point
    bool valid() const { return index != SIZE_MAX; }
};

// Uniform spatial grid over segments, in world (screen-pixel) coordinates.
class HitGrid {
public:
    explicit HitGrid(int cell = 32) : cell_(cell) {}
    void reset();
    void sync(const Path& p);  // index segments appended since the last call
    // Nearest segment with index < limit within maxDist of (x,y). Ties go to the lower index.
    Hit nearest(const Path& p, double x, double y, double maxDist, size_t limit = SIZE_MAX) const;

private:
    void insert(const Path& p, size_t i);
    int cell_;
    size_t indexed_ = 0;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

// Reference implementation for tests.
Hit bruteNearest(const Path& p, double x, double y, double maxDist, size_t limit = SIZE_MAX);

}  // namespace lt

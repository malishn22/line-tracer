// Raw recording data. No platform code in core/.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lt {

constexpr uint32_t FLAG_BREAK = 1;  // no line is drawn from the previous sample to this one

struct Sample {
    int32_t x, y;    // physical screen pixels
    uint32_t ms;     // milliseconds since recording start
    uint32_t flags;
};
static_assert(sizeof(Sample) == 16, "Sample must be 16 bytes (file format)");

struct Bounds {
    int32_t x = 0, y = 0, w = 1920, h = 1080;
};

struct Path {
    Bounds bounds;
    std::vector<Sample> samples;

    void add(int32_t x, int32_t y, uint32_t ms, uint32_t flags = 0) {
        samples.push_back(Sample{x, y, ms, flags});
    }
    // Segment i joins sample i-1 to sample i, unless i starts a new run.
    bool isSegment(size_t i) const {
        return i > 0 && i < samples.size() && !(samples[i].flags & FLAG_BREAK);
    }
};

}  // namespace lt

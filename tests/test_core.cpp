// Core tests on a synthetic recording. No framework: a failed CHECK prints and sets the exit code.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

#include "../core/fileio.h"
#include "../core/hitgrid.h"
#include "../core/png.h"
#include "../core/renderer.h"
#include "../core/stats.h"

using namespace lt;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

static const int BREAK_AT = 1500;

// Lissajous curve over 1920x1080, one sample per 4 ms, with a teleport (break) at BREAK_AT.
static Path synthetic(int n = 3000) {
    Path p;
    p.bounds = Bounds{0, 0, 1920, 1080};
    for (int i = 0; i < n; ++i) {
        double t = i * 0.01;
        double x = 960 + 900 * std::sin(3 * t + (i >= BREAK_AT ? 1.0 : 0.0));
        double y = 540 + 500 * std::sin(2 * t);
        p.add(int(std::lround(x)), int(std::lround(y)), uint32_t(i * 4), i == BREAK_AT ? FLAG_BREAK : 0);
    }
    return p;
}

static bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static double sum(const std::vector<float>& v) {
    double s = 0;
    for (float f : v) s += f;
    return s;
}

static void testIncrementalEqualsFull() {
    std::printf("incremental render == full render\n");
    Path p = synthetic();
    const ColorMode modes[3] = {ColorMode::White, ColorMode::Time, ColorMode::Speed};
    for (ColorMode m : modes) {
        Renderer full, inc;
        full.configure(800, 450, p.bounds, 8);
        inc.configure(800, 450, p.bounds, 8);
        full.setMode(m); inc.setMode(m);
        full.setTimeSpan(12000); inc.setTimeSpan(12000);
        full.drawTo(p, p.samples.size());
        // uneven chunks, including ones that straddle the break sample
        size_t cuts[] = {1, 2, 7, 100, 1499, 1500, 1501, 2222, 3000};
        for (size_t c : cuts) inc.drawTo(p, c);
        CHECK(sameBits(full.accum(), inc.accum()));
        CHECK(sum(full.accum()) > 1000.0);
        // scrubbing backwards then forwards again must reproduce the same image
        inc.drawTo(p, 1000);
        Renderer part;
        part.configure(800, 450, p.bounds, 8);
        part.setMode(m); part.setTimeSpan(12000);
        part.drawTo(p, 1000);
        CHECK(sameBits(part.accum(), inc.accum()));
        inc.drawTo(p, 3000);
        CHECK(sameBits(full.accum(), inc.accum()));
    }
}

static void testThickness() {
    std::printf("line thickness\n");
    // horizontal line at integer y: thickness 3 covers exactly rows 49..51, each as bright as a thin line
    Path h;
    h.bounds = Bounds{0, 0, 200, 100};
    h.add(20, 50, 0);
    h.add(180, 50, 4);
    Renderer thin, thick;
    thin.configure(200, 100, h.bounds, 0);
    thick.configure(200, 100, h.bounds, 0);
    thick.setThickness(3);
    thin.drawTo(h, 2);
    thick.drawTo(h, 2);
    auto px = [](const Renderer& r, int x, int y) { return r.accum()[(size_t(y) * 200 + x) * 3]; };
    CHECK(px(thin, 100, 50) == 1.0f && px(thin, 100, 49) == 0.0f);
    CHECK(px(thick, 100, 49) == 1.0f && px(thick, 100, 50) == 1.0f && px(thick, 100, 51) == 1.0f);
    CHECK(px(thick, 100, 48) == 0.0f && px(thick, 100, 52) == 0.0f);
    // energy scales with width on a diagonal / curvy path too (brightness per pixel stays constant)
    Path p = synthetic();
    Renderer a, b;
    a.configure(800, 450, p.bounds, 8);
    b.configure(800, 450, p.bounds, 8);
    b.setThickness(3);
    a.drawTo(p, p.samples.size());
    b.drawTo(p, p.samples.size());
    double ratio = sum(b.accum()) / sum(a.accum());
    CHECK(ratio > 2.7 && ratio < 3.3);
    // incremental == full still holds with thickness, and clamping works
    Renderer inc;
    inc.configure(800, 450, p.bounds, 8);
    inc.setThickness(3);
    size_t cuts[] = {5, 1499, 1501, 2500, 3000};
    for (size_t c : cuts) inc.drawTo(p, c);
    CHECK(sameBits(inc.accum(), b.accum()));
    inc.setThickness(99);
    CHECK(inc.thickness() == 8);
    inc.setThickness(0);
    CHECK(inc.thickness() == 1);
}

static void testBreak() {
    std::printf("break flag\n");
    Path p;
    p.bounds = Bounds{0, 0, 1000, 700};
    p.add(100, 100, 0);
    p.add(200, 100, 4);
    p.add(800, 600, 8, FLAG_BREAK);  // jump of ~700 px, must not be drawn
    p.add(900, 600, 12);
    CHECK(!p.isSegment(0));
    CHECK(p.isSegment(1));
    CHECK(!p.isSegment(2));
    CHECK(p.isSegment(3));

    Renderer full;
    full.configure(1000, 700, p.bounds, 0);
    full.drawTo(p, 4);
    // nothing along the would-be gap line (200,100)->(800,600) away from its endpoints
    bool gapClean = true;
    for (int x = 260; x <= 740; ++x) {
        double y = 100 + (x - 200) * (500.0 / 600.0);
        for (int dy = -2; dy <= 2; ++dy) {
            const float* px = &full.accum()[(size_t(int(y) + dy) * 1000 + x) * 3];
            if (px[0] != 0 || px[1] != 0 || px[2] != 0) gapClean = false;
        }
    }
    CHECK(gapClean);
    // image == run A + run B drawn separately (no cross-gap contribution)
    Path a = p, b = p;
    a.samples.resize(2);
    b.samples.erase(b.samples.begin(), b.samples.begin() + 2);
    b.samples[0].flags = 0;
    Renderer ra, rb;
    ra.configure(1000, 700, p.bounds, 0);
    rb.configure(1000, 700, p.bounds, 0);
    ra.drawTo(a, a.samples.size());
    rb.drawTo(b, b.samples.size());
    CHECK(std::fabs(sum(full.accum()) - (sum(ra.accum()) + sum(rb.accum()))) < 1e-3);

    Analysis an;
    an.update(p);
    CHECK(std::fabs(an.cumDist()[1] - 100.0) < 1e-9);
    CHECK(an.cumDist()[2] == an.cumDist()[1]);
    CHECK(std::fabs(an.cumDist()[3] - 200.0) < 1e-9);
    Stats s = an.stats(p);
    CHECK(std::fabs(s.totalDist - 200.0) < 1e-9);
    CHECK(segLength(p, 2) == 0 && segSpeed(p, 2) == 0);

    HitGrid g;
    g.sync(p);
    Hit h = g.nearest(p, 500, 350, 50);  // middle of the gap
    CHECK(!h.valid());
    h = g.nearest(p, 150, 103, 10);
    CHECK(h.valid() && h.index == 1);
    h = g.nearest(p, 850, 598, 10);
    CHECK(h.valid() && h.index == 3);
}

static void testHit() {
    std::printf("hit-test\n");
    // square: (0,0) -> (100,0) -> (100,100) -> (0,100)
    Path sq;
    sq.bounds = Bounds{0, 0, 200, 200};
    sq.add(0, 0, 0); sq.add(100, 0, 100); sq.add(100, 100, 200); sq.add(0, 100, 300);
    HitGrid g;
    g.sync(sq);
    Hit h = g.nearest(sq, 50, 3, 10);
    CHECK(h.valid() && h.index == 1 && std::fabs(h.dist - 3.0) < 1e-9);
    h = g.nearest(sq, 97, 50, 10);
    CHECK(h.valid() && h.index == 2 && std::fabs(h.dist - 3.0) < 1e-9);
    h = g.nearest(sq, 50, 50, 10);  // 50 px from everything
    CHECK(!h.valid());
    h = g.nearest(sq, 97, 50, 10, 2);  // segment 2 excluded by the playhead limit
    CHECK(!h.valid());
    h = g.nearest(sq, 100, 0, 5);  // shared corner: tie -> lower index
    CHECK(h.valid() && h.index == 1);

    // grid must agree with brute force on a big, break-containing path
    Path p = synthetic(20000);
    HitGrid grid;
    // incremental indexing in pieces
    Path partial = p;
    partial.samples.resize(7000);
    grid.sync(partial);
    grid.sync(p);
    std::mt19937 rng(42);
    int mismatches = 0, found = 0;
    for (int q = 0; q < 3000; ++q) {
        double x = std::uniform_real_distribution<double>(-50, 1970)(rng);
        double y = std::uniform_real_distribution<double>(-50, 1130)(rng);
        double md = (q % 3 == 0) ? 3.0 : (q % 3 == 1 ? 15.0 : 80.0);
        size_t limit = (q % 4 == 0) ? size_t(rng() % 20000) : SIZE_MAX;
        Hit a = grid.nearest(p, x, y, md, limit);
        Hit b = bruteNearest(p, x, y, md, limit);
        if (a.index != b.index || (a.valid() && a.dist != b.dist)) ++mismatches;
        if (b.valid()) ++found;
    }
    CHECK(mismatches == 0);
    CHECK(found > 300);  // the comparison above is not vacuous
}

static void testStats() {
    std::printf("stats\n");
    Path p;
    p.add(0, 0, 0);
    p.add(3, 4, 1000);    // 5 px in 1 s
    p.add(3, 10, 1500);   // 6 px in 0.5 s = 12 px/s
    Analysis an;
    an.update(p);
    Stats s = an.stats(p);
    CHECK(s.points == 3);
    CHECK(std::fabs(s.durationMs - 1500) < 1e-9);
    CHECK(std::fabs(s.totalDist - 11) < 1e-9);
    CHECK(std::fabs(s.peakSpeed - 12) < 1e-9);
    CHECK(std::fabs(s.avgSpeed - 11.0 / 1.5) < 1e-9);
    // incremental update gives the same answer as one-shot
    Analysis inc;
    Path q;
    for (size_t i = 0; i < p.samples.size(); ++i) { q.samples.push_back(p.samples[i]); inc.update(q); }
    CHECK(inc.cumDist() == an.cumDist());
}

static void testToneMap() {
    std::printf("tone map\n");
    const int W = 64, H = 4;
    std::vector<float> acc(W * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) acc[(y * W + x) * 3 + c] = float(x) * 0.5f;
    std::vector<uint32_t> out(W * H);
    toneMap(acc, W, H, 1.0f, 0.2f, 0.0f, out.data());
    bool mono = true;
    for (int x = 1; x < W; ++x) if ((out[x] & 0xFF) < (out[x - 1] & 0xFF)) mono = false;
    CHECK(mono);
    CHECK((out[0] & 0xFFFFFF) == 0);
    // 1 - exp(-0.2 * 10) = 0.8647 -> 220
    int v = int(out[20] & 0xFF);
    CHECK(std::abs(v - 220) <= 1);
    // brightness is display-only: doubling it equals doubling gain
    std::vector<uint32_t> b2(W * H), g2(W * H);
    toneMap(acc, W, H, 2.0f, 0.2f, 0.0f, b2.data());
    toneMap(acc, W, H, 1.0f, 0.4f, 0.0f, g2.data());
    CHECK(b2 == g2);
    // glow adds light around a lone bright pixel
    std::vector<float> dot(40 * 40 * 3, 0.0f);
    dot[(20 * 40 + 20) * 3] = 5.0f;
    std::vector<uint32_t> o0(1600), o1(1600);
    toneMap(dot, 40, 40, 1.0f, 0.2f, 0.0f, o0.data());
    toneMap(dot, 40, 40, 1.0f, 0.2f, 12.0f, o1.data());
    CHECK((o0[20 * 40 + 22] & 0xFF0000) == 0);
    CHECK((o1[20 * 40 + 22] & 0xFF0000) != 0);

    // The halo has a fixed size in pixels (two passes of radius 4 reach 8 px) whatever the image size.
    for (int N : {200, 800}) {
        std::vector<float> d(size_t(N) * N * 3, 0.0f);
        int c = N / 2;
        d[(size_t(c) * N + c) * 3] = 5.0f;
        std::vector<uint32_t> o(size_t(N) * N);
        toneMap(d, N, N, 1.0f, 0.2f, 12.0f, o.data());
        CHECK((o[size_t(c) * N + c + 8] & 0xFF0000) != 0);  // inside the halo
        CHECK((o[size_t(c) * N + c + 9] & 0xFF0000) == 0);  // just outside it
    }
}

static void testFileRoundTrip(const std::filesystem::path& dir) {
    std::printf("save/load round trip\n");
    Path p = synthetic();
    p.bounds = Bounds{-1920, 40, 1920, 1080};
    auto f = dir / "roundtrip.ltr";
    std::string err;
    CHECK(savePath(f, p, &err));
    Path q;
    CHECK(loadPath(f, q, &err));
    CHECK(q.bounds.x == p.bounds.x && q.bounds.y == p.bounds.y && q.bounds.w == p.bounds.w && q.bounds.h == p.bounds.h);
    CHECK(q.samples.size() == p.samples.size());
    CHECK(std::memcmp(q.samples.data(), p.samples.data(), p.samples.size() * sizeof(Sample)) == 0);
    CHECK(q.samples[BREAK_AT].flags == FLAG_BREAK);

    // empty path
    Path e;
    CHECK(savePath(dir / "empty.ltr", e) && loadPath(dir / "empty.ltr", q) && q.samples.empty());

    // corrupted magic
    {
        std::fstream s(f, std::ios::in | std::ios::out | std::ios::binary);
        s.put('X');
    }
    Path bad;
    CHECK(!loadPath(f, bad, &err));
    // truncated
    CHECK(savePath(f, p));
    std::filesystem::resize_file(f, std::filesystem::file_size(f) - 5);
    CHECK(!loadPath(f, bad, &err));
    CHECK(!loadPath(dir / "does_not_exist.ltr", bad, &err));
}

static void testPng(const std::filesystem::path& dir) {
    std::printf("png export\n");
    Path p = synthetic();
    ExportSettings s;
    int w = 0, h = 0;
    auto f = dir / "synthetic.png";
    CHECK(exportPng(f, p, s, &w, &h));
    CHECK(w == 3840 && h == 2160);  // 2x of 1920x1080, within the 4096 cap
    Path big = p;
    big.bounds = Bounds{0, 0, 3840, 2160};
    CHECK(exportPng(dir / "big.png", big, s, &w, &h));
    CHECK(w == 4096 && h == 2304);  // capped on the long edge
    std::filesystem::remove(dir / "big.png");
    std::ifstream in(f, std::ios::binary);
    unsigned char sig[8] = {};
    in.read(reinterpret_cast<char*>(sig), 8);
    CHECK(sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G');
    savePath(dir / "synthetic.ltr", p);
}

int main(int argc, char** argv) {
    std::filesystem::path dir = argc > 1 ? argv[1] : "build";
    std::filesystem::create_directories(dir);
    testIncrementalEqualsFull();
    testThickness();
    testBreak();
    testHit();
    testStats();
    testToneMap();
    testFileRoundTrip(dir);
    testPng(dir);
    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

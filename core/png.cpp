#include "png.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace lt {

namespace {

struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int n = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void put(uint32_t v, int bits) {  // LSB first
        acc |= v << n;
        n += bits;
        while (n >= 8) {
            out.push_back(uint8_t(acc & 0xFF));
            acc >>= 8;
            n -= 8;
        }
    }
    void flush() {
        if (n > 0) out.push_back(uint8_t(acc & 0xFF));
        acc = 0;
        n = 0;
    }
};

uint32_t rev(uint32_t c, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; ++i) r = (r << 1) | ((c >> i) & 1);
    return r;
}

void putSym(BitWriter& w, int sym) {  // fixed Huffman literal/length code
    if (sym < 144) w.put(rev(0x30 + sym, 8), 8);
    else if (sym < 256) w.put(rev(0x190 + sym - 144, 9), 9);
    else if (sym < 280) w.put(rev(sym - 256, 7), 7);
    else w.put(rev(0xC0 + sym - 280, 8), 8);
}

const int LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putMatch(BitWriter& w, int len, int dist) {
    int j = 28;
    while (LBASE[j] > len) --j;
    putSym(w, 257 + j);
    if (LEXTRA[j]) w.put(uint32_t(len - LBASE[j]), LEXTRA[j]);
    int k = 29;
    while (DBASE[k] > dist) --k;
    w.put(rev(uint32_t(k), 5), 5);
    if (DEXTRA[k]) w.put(uint32_t(dist - DBASE[k]), DEXTRA[k]);
}

std::vector<uint8_t> deflateFixed(const std::vector<uint8_t>& d) {
    std::vector<uint8_t> out;
    out.reserve(d.size() / 8 + 64);
    BitWriter w(out);
    w.put(1, 1);  // BFINAL
    w.put(1, 2);  // BTYPE = fixed Huffman
    const int n = int(d.size());
    const int HB = 15, MASK = 32767;
    std::vector<int> head(size_t(1) << HB, -1), prev(32768, -1);
    auto hash = [&](int i) {
        uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8) | d[i + 2];
        return (v * 2654435761u) >> (32 - HB);
    };
    auto insert = [&](int p) {
        if (p + 2 >= n) return;
        uint32_t h = hash(p);
        prev[p & MASK] = head[h];
        head[h] = p;
    };
    for (int i = 0; i < n;) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int cand = head[hash(i)], chain = 0;
            int maxl = std::min(258, n - i);
            while (cand >= 0 && i - cand <= 32768 && chain++ < 16) {
                int l = 0;
                while (l < maxl && d[cand + l] == d[i + l]) ++l;
                if (l > bestLen) {
                    bestLen = l;
                    bestDist = i - cand;
                    if (l == maxl) break;
                }
                int nx = prev[cand & MASK];
                if (nx >= cand) break;  // slot was overwritten by a newer position
                cand = nx;
            }
        }
        if (bestLen >= 3) {
            putMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + k);
            i += bestLen;
        } else {
            putSym(w, d[i]);
            insert(i);
            ++i;
        }
    }
    putSym(w, 256);
    w.flush();
    return out;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}

void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
    be32(o, uint32_t(data.size()));
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    be32(o, crc32(o.data() + start, o.size() - start));
}

}  // namespace

std::vector<uint8_t> encodePng(const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb + size_t(y) * w * 3, rgb + size_t(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    std::vector<uint8_t> body = deflateFixed(raw);
    z.insert(z.end(), body.begin(), body.end());
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, truecolour, deflate, no filter set, no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool writePng(const std::filesystem::path& file, const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> png = encodePng(rgb, w, h);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    f.flush();
    return f.good();
}

bool exportPng(const std::filesystem::path& file, const Path& p, const ExportSettings& s, int* outW, int* outH) {
    double bw = std::max(p.bounds.w, 1), bh = std::max(p.bounds.h, 1);
    double k = std::min(2.0, 4096.0 / std::max(bw, bh));
    int W = std::max(1, int(std::lround(bw * k))), H = std::max(1, int(std::lround(bh * k)));
    Renderer r;
    r.configure(W, H, p.bounds, 0);
    r.setMode(s.mode);
    r.setTimeSpan(s.timeSpanMs);
    r.setThickness(s.thickness);
    r.drawTo(p, p.samples.size());
    std::vector<uint32_t> px(size_t(W) * H);
    toneMap(r.accum(), W, H, s.brightness, s.gain, s.glow, px.data());
    std::vector<uint8_t> rgb(size_t(W) * H * 3);
    for (size_t i = 0; i < px.size(); ++i) {
        rgb[i * 3] = uint8_t(px[i] >> 16);
        rgb[i * 3 + 1] = uint8_t(px[i] >> 8);
        rgb[i * 3 + 2] = uint8_t(px[i]);
    }
    if (outW) *outW = W;
    if (outH) *outH = H;
    return writePng(file, rgb.data(), W, H);
}

}  // namespace lt

#include "fileio.h"

#include <cstring>
#include <fstream>

namespace lt {

namespace {
constexpr char MAGIC[4] = {'L', 'T', 'R', 'C'};
constexpr uint32_t VERSION = 1;
constexpr uint64_t HEADER = 4 + 4 + 16 + 8;

bool fail(std::string* err, const char* m) {
    if (err) *err = m;
    return false;
}
}  // namespace

bool savePath(const std::filesystem::path& file, const Path& p, std::string* err) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return fail(err, "cannot open file for writing");
    uint8_t hdr[HEADER];
    uint64_t count = p.samples.size();
    int32_t b[4] = {p.bounds.x, p.bounds.y, p.bounds.w, p.bounds.h};
    std::memcpy(hdr, MAGIC, 4);
    std::memcpy(hdr + 4, &VERSION, 4);
    std::memcpy(hdr + 8, b, 16);
    std::memcpy(hdr + 24, &count, 8);
    f.write(reinterpret_cast<const char*>(hdr), HEADER);
    if (count) f.write(reinterpret_cast<const char*>(p.samples.data()), std::streamsize(count * sizeof(Sample)));
    f.flush();
    return f.good() ? true : fail(err, "write failed");
}

bool loadPath(const std::filesystem::path& file, Path& p, std::string* err) {
    std::ifstream f(file, std::ios::binary | std::ios::ate);
    if (!f) return fail(err, "cannot open file");
    uint64_t size = uint64_t(f.tellg());
    f.seekg(0);
    uint8_t hdr[HEADER];
    if (size < HEADER || !f.read(reinterpret_cast<char*>(hdr), HEADER)) return fail(err, "file too short");
    if (std::memcmp(hdr, MAGIC, 4) != 0) return fail(err, "not a LineTrace file (bad magic)");
    uint32_t ver;
    std::memcpy(&ver, hdr + 4, 4);
    if (ver != VERSION) return fail(err, "unsupported version");
    int32_t b[4];
    uint64_t count;
    std::memcpy(b, hdr + 8, 16);
    std::memcpy(&count, hdr + 24, 8);
    if (size - HEADER != count * sizeof(Sample) || count > (size - HEADER) / sizeof(Sample))
        return fail(err, "file size does not match sample count (truncated?)");
    Path out;
    out.bounds = Bounds{b[0], b[1], b[2], b[3]};
    out.samples.resize(size_t(count));
    if (count && !f.read(reinterpret_cast<char*>(out.samples.data()), std::streamsize(count * sizeof(Sample))))
        return fail(err, "read failed");
    p = std::move(out);
    return true;
}

}  // namespace lt

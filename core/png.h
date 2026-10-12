#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

#include "path.h"
#include "renderer.h"

namespace lt {

// Minimal PNG encoder (8-bit RGB, LZ77 + fixed Huffman deflate). No third-party code.
std::vector<uint8_t> encodePng(const uint8_t* rgb, int w, int h);
bool writePng(const std::filesystem::path& file, const uint8_t* rgb, int w, int h);

struct ExportSettings {
    ColorMode mode = ColorMode::White;
    double timeSpanMs = 60000;
    float brightness = 1.0f, gain = 0.2f, glow = 4.0f;
    int thickness = 1;  // line width in output pixels
};

// Renders the whole path at up to 2x the bounds, long edge capped at 4096 px, and writes a PNG.
bool exportPng(const std::filesystem::path& file, const Path& p, const ExportSettings& s,
               int* outW = nullptr, int* outH = nullptr);

}  // namespace lt

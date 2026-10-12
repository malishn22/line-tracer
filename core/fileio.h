#pragma once
#include <filesystem>
#include <string>

#include "path.h"

namespace lt {

// File layout (little-endian): "LTRC" | u32 version=1 | i32 x,y,w,h | u64 count | count * Sample(16 B)
bool savePath(const std::filesystem::path& file, const Path& p, std::string* err = nullptr);
bool loadPath(const std::filesystem::path& file, Path& p, std::string* err = nullptr);

}  // namespace lt

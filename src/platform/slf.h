#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "wiz8/file_time.h"

namespace wiz8 {
struct AssetEntry {
    std::filesystem::path archive;
    std::uint32_t offset, length;
    DiskFileTime modified;
};
std::optional<AssetEntry> archive_entry(std::string_view game_path);
} // namespace wiz8

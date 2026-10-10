#pragma once
#include "wiz8/filesystem.h"

namespace wiz8
{
struct AssetEntry
{
    std::filesystem::path archive;
    std::uint32_t offset, length;
    DiskFileTime modified;
};
std::optional<AssetEntry> archive_entry(std::string_view game_path);
}

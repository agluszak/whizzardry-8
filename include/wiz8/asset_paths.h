#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace w8_native {
struct Roots {
    std::string assets;
    std::string user;
};
// Configure once before file I/O. The default user root is SDL_GetPrefPath().
void configure_paths(const Roots& roots);
Roots path_roots();
}

namespace wiz8 {
// Retail asset names use ASCII case folding and either slash spelling.
// Absolute paths, drive prefixes, NULs and parent traversal are rejected.
std::filesystem::path normalize_asset_path(std::string_view name);
std::filesystem::path asset_path(std::string_view name);
// User names retain their spelling; separators and relative-path safety are normalized.
std::filesystem::path user_path(std::string_view name);
}

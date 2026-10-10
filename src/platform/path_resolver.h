#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace w8_native
{
enum class PathDomain { game, host };

struct ResolvedPath
{
    std::filesystem::path readable;
    std::filesystem::path writable;
    std::filesystem::path readable_parent;
    std::vector<std::filesystem::path> directories;
    bool overlay = false;
    bool writable_source = false;
};

ResolvedPath resolve_path(std::string_view path, PathDomain domain = PathDomain::game);
} // namespace w8_native

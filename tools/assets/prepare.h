#pragma once
#include <filesystem>
#include <span>

namespace wiz8::preparation {
// Validates the entire SLF directory before writing any entries.
void extract_slf(const std::filesystem::path& archive, const std::filesystem::path& output);
// Output must not exist. Sources are installation first, then optional CD trees.
void prepare_assets(std::span<const std::filesystem::path> sources,
                    const std::filesystem::path& output);
}

#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

inline std::string make_temporary_directory(const char* prefix)
{
    std::random_device random;
    const auto base = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        const auto path = base / (std::string(prefix) + "-" + std::to_string(random()));
        std::error_code error;
        if (std::filesystem::create_directory(path, error))
            return path.generic_string();
        if (error && error != std::errc::file_exists)
            throw std::filesystem::filesystem_error("create test directory", path, error);
    }
    throw std::runtime_error("Cannot create unique test directory");
}

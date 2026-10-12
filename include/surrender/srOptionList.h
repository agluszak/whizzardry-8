#pragma once


#include <cstdint>
// The first two words are opaque; the JPEG exporter reads only the option string.
struct srImportOptions {
    std::uint32_t unknown_00;
    std::uint32_t unknown_04;
    const char* option_string;
};

struct srExportOptions {
    std::uint32_t unknown_00;
    std::uint32_t unknown_04;
    const char* option_string;
};


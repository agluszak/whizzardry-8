#pragma once

// The first two words are opaque; the JPEG exporter reads only the option string.
struct srImportOptions {
    w8_ulong unknown_00;
    w8_ulong unknown_04;
    const char* option_string;
};

struct srExportOptions {
    w8_ulong unknown_00;
    w8_ulong unknown_04;
    const char* option_string;
};

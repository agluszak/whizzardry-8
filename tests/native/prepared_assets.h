#pragma once
#include "prepare.h"
#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"

// Fixture setup only. The engine sees the same normalized loose files as an installation.
inline void extract_test_archive(std::string_view name)
{
    wiz8::preparation::extract_slf(wiz8::asset_path(name),
                                   wiz8::path_from_utf8(w8_native::path_roots().assets));
}

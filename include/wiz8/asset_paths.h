#pragma once

#include <string>

/* Asset namespace. Roots are initialized from WIZ8_ASSET_ROOT,
   WIZ8_USER_ROOT and WIZ8_CD1_ROOT..WIZ8_CD3_ROOT. A configured disc is exposed
   as D:, E: or F: with the label WIZ8_1, WIZ8_2 or WIZ8_3. */
namespace w8_native
{
struct Roots
{
    std::string assets;
    std::string user;
    std::string discs[3];
};
/* Set before starting any file I/O; primarily useful to an embedding shell. */
void configure_paths(const Roots& roots);
Roots path_roots();
std::string full_path(const char* path);
/* Use a populated legacy sibling only when the preferred SDL root is empty. */
std::string existing_legacy_user_root(const std::string& preferred);
int change_directory(const char* path);
std::string current_directory();
} // namespace w8_native

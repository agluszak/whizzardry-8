#pragma once

#include <string>
#include <string_view>
#include <vector>

void ReleaseLocalizedStrings();
void LoadLocalizedStrings(std::string_view path);

/* Local Code\Strings.cpp owns the decoded game string table. */
extern std::vector<std::string> g_localized_strings;
extern int giStringListLen;

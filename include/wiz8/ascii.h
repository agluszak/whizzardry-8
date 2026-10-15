#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

/* Retail asset, script and level names compare and normalize with ASCII-only
   case folding; bytes outside A-Z/a-z are compared as they are. */
namespace wiz8 {

constexpr char ascii_lower(char character)
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                : character;
}

constexpr char ascii_upper(char character)
{
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - ('a' - 'A'))
                                                : character;
}

constexpr bool iequals(std::string_view left, std::string_view right)
{
    return std::ranges::equal(left, right,
                              [](char a, char b) { return ascii_lower(a) == ascii_lower(b); });
}

/* Equality of at most the first `count` characters of each name. */
constexpr bool iequals_prefix(std::string_view left, std::string_view right, std::size_t count)
{
    return iequals(left.substr(0, std::min(count, left.size())),
                   right.substr(0, std::min(count, right.size())));
}

/* Uppercase a NUL-terminated name in place. */
inline void uppercase_ascii(char* text)
{
    for (; *text != '\0'; ++text) *text = ascii_upper(*text);
}

} // namespace wiz8

#include "wiz8/unicode.h"
#include "utf8/checked.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utf8proc.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <iterator>

namespace wiz8::text {
char32_t next(std::string_view text, std::size_t& offset)
{
    if (offset >= text.size()) return 0;
    auto it = text.begin() + offset;
    try {
        const auto cp = utf8::next(it, text.end());
        offset = it - text.begin();
        return cp;
    } catch (const utf8::exception&) {
        ++offset;
        return 0xfffd;
    }
}
std::size_t previous(std::string_view text, std::size_t offset)
{
    auto it = text.begin() + std::min(offset, text.size());
    if (it == text.begin()) return 0;
    const auto end = it;
    try { utf8::prior(it, text.begin()); }
    catch (const utf8::exception&) { it = end - 1; }
    return it - text.begin();
}
std::size_t byte_prefix(std::string_view text, std::size_t byte_limit)
{
    std::size_t offset = 0, end = 0;
    while (offset < text.size()) {
        next(text, offset);
        if (offset > byte_limit) break;
        end = offset;
    }
    return end;
}
std::size_t codepoint_prefix(std::string_view text, std::size_t count)
{
    std::size_t offset = 0;
    while (count-- && offset < text.size()) next(text, offset);
    return offset;
}
std::size_t next_grapheme(std::string_view text, std::size_t offset)
{
    if (offset >= text.size()) return text.size();
    utf8proc_int32_t state = 0;
    auto previous = next(text, offset);
    while (offset < text.size()) {
        const auto boundary = offset;
        const auto current = next(text, offset);
        if (utf8proc_grapheme_break_stateful(previous, current, &state)) return boundary;
        previous = current;
    }
    return text.size();
}
std::size_t previous_grapheme(std::string_view text, std::size_t offset)
{
    std::size_t previous = 0, current = 0;
    while (current < std::min(offset, text.size())) {
        previous = current;
        current = next_grapheme(text, current);
    }
    return previous;
}
std::size_t grapheme_count(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < text.size(); ++count) offset = next_grapheme(text, offset);
    return count;
}
void write_utf8_field(std::string_view text, std::span<char> field)
{
    if (field.empty())
        return;
    const auto size = byte_prefix(text, field.size() - 1);
    // Allow the source to overlap the field, then clear its unused tail.
    if (size)
        std::memmove(field.data(), text.data(), size);
    std::fill(field.begin() + size, field.end(), '\0');
}

std::string from_utf16(std::u16string_view text)
{
    std::string out;
    auto it = text.begin();
    while (it != text.end() && *it) {
        const auto start = it;
        char32_t cp;
        try { cp = utf8::next16(it, text.end()); }
        catch (const utf8::exception&) { cp = 0xfffd; }
        if (it == start) { ++it; cp = 0xfffd; }
        utf8::append(cp, std::back_inserter(out));
    }
    return out;
}
std::u16string to_utf16(std::string_view text)
{
    std::string valid;
    utf8::replace_invalid(text.begin(), text.end(), std::back_inserter(valid));
    std::u16string out;
    utf8::utf8to16(valid.begin(), valid.end(), std::back_inserter(out));
    return out;
}
std::string from_utf16le(std::span<const std::byte> bytes, bool encoded)
{
    if (bytes.size() % 2) throw std::runtime_error("Odd UTF-16LE byte count");
    std::u16string units;
    units.reserve(bytes.size()/2);
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        auto unit = static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[i]) |
                    (std::to_integer<unsigned>(bytes[i+1]) << 8));
        // FUNCTION: WIZ8 0x005300e0 (DecodeLocalizedText at the format boundary)
        if (encoded) unit = static_cast<std::uint16_t>(~unit + 0x9697);
        if (!unit) break;
        units += static_cast<char16_t>(unit);
    }
    return from_utf16(units);
}
void to_utf16le(std::string_view text, std::span<std::byte> bytes)
{
    if (bytes.size() % 2) throw std::runtime_error("Odd UTF-16LE byte count");
    std::fill(bytes.begin(), bytes.end(), std::byte{});
    auto units = to_utf16(text);
    // Retail fields are NUL terminated. Never split a surrogate pair.
    auto count = std::min(units.size(), bytes.size()/2 ? bytes.size()/2 - 1 : 0);
    if (count && units[count-1] >= 0xd800 && units[count-1] <= 0xdbff) --count;
    for (std::size_t i = 0; i < count; ++i) {
        bytes[2*i] = static_cast<std::byte>(units[i] & 255);
        bytes[2*i+1] = static_cast<std::byte>(units[i] >> 8);
    }
}
} // namespace wiz8::text

// FUNCTION: WIZ8 0x00402800
bool IsUppercaseCharacter(std::uint32_t character)
{
    return utf8proc_category(character) == UTF8PROC_CATEGORY_LU;
}

// FUNCTION: WIZ8 0x00402820
bool IsLowercaseCharacter(std::uint32_t character)
{
    const auto category = utf8proc_category(character); return category == UTF8PROC_CATEGORY_LL || category == UTF8PROC_CATEGORY_LO || category == UTF8PROC_CATEGORY_LT || category == UTF8PROC_CATEGORY_LM || category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_MC || category == UTF8PROC_CATEGORY_ME;
}

// FUNCTION: WIZ8 0x00402840
bool IsPunctuationCharacter(std::uint32_t character)
{
    const auto category = utf8proc_category(character); return (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_SO) || category == UTF8PROC_CATEGORY_CF;
}

// FUNCTION: WIZ8 0x00402880
int ToUppercaseCharacter(int character)
{
    return utf8proc_toupper(character);
}

// FUNCTION: WIZ8 0x004028A0
int ToLowercaseCharacter(int character)
{
    return utf8proc_tolower(character);
}

// FUNCTION: WIZ8 0x00402920
int CompareTextIgnoreAsciiCase(std::string_view first, std::string_view second)
{
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i) {
        const int difference = lower(first[i]) - lower(second[i]);
        if (difference) return difference;
    }
    return (first.size() > second.size()) - (first.size() < second.size());
}

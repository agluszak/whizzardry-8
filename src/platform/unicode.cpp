#include "wiz8/unicode.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace wiz8::text {
namespace {
constexpr char32_t replacement = 0xfffd;
void append(std::string& out, char32_t cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
}
char32_t next(std::string_view text, std::size_t& offset)
{
    if (offset >= text.size()) return 0;
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 0x80) return first;
    const unsigned length = first >= 0xc2 && first <= 0xdf ? 2 :
                            first >= 0xe0 && first <= 0xef ? 3 :
                            first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!length || length - 1 > text.size() - offset) return replacement;
    char32_t cp = first & ((1 << (7 - length)) - 1);
    for (unsigned i = 0; i < length - 1; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xc0) != 0x80) return replacement;
        cp = (cp << 6) | (byte & 63);
    }
    if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
        (length == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff)) return replacement;
    offset += length - 1;
    return cp;
}
std::size_t previous(std::string_view text, std::size_t offset)
{
    offset = std::min(offset, text.size());
    if (!offset) return 0;
    --offset;
    while (offset && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) --offset;
    return offset;
}
std::size_t prefix(std::string_view text, std::size_t limit)
{
    std::size_t offset = 0, end = 0;
    while (offset < text.size()) {
        next(text, offset);
        if (offset > limit) break;
        end = offset;
    }
    return end;
}
void copy(char* destination, std::size_t capacity, std::string_view text)
{
    if (!capacity) return;
    const auto size = prefix(text, capacity - 1);
    std::memmove(destination, text.data(), size);
    destination[size] = 0;
}
std::string from_utf16(std::u16string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size() && text[i]; ++i) {
        char32_t cp = text[i];
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (i + 1 < text.size() && text[i+1] >= 0xdc00 && text[i+1] <= 0xdfff)
                cp = 0x10000 + ((cp - 0xd800) << 10) + (text[++i] - 0xdc00);
            else cp = replacement;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) cp = replacement;
        append(out, cp);
    }
    return out;
}
std::u16string to_utf16(std::string_view text)
{
    std::u16string out;
    for (std::size_t offset = 0; offset < text.size();) {
        char32_t cp = next(text, offset);
        if (!cp) break;
        if (cp < 0x10000) out += static_cast<char16_t>(cp);
        else {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xd800 + (cp >> 10));
            out += static_cast<char16_t>(0xdc00 + (cp & 1023));
        }
    }
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
std::string retail_format(std::string text)
{
    // Retail wide printf used %s for UTF-16 and %s/%s for bytes. Both
    // domains have been decoded to UTF-8 before entering native formatting.
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') continue;
        if (i + 1 < text.size() && text[i+1] == '%') { ++i; continue; }
        std::size_t j = i + 1;
        while (j < text.size() && std::string_view("-+ #0.123456789*").find(text[j]) != std::string_view::npos) ++j;
        if (j == text.size()) break;
        if ((text[j] == 'h' || text[j] == 'l') && j+1 < text.size() &&
            (text[j+1] == 's' || text[j+1] == 'S')) text.erase(j, 1);
        if (text[j] == 'l' && j+1 < text.size() &&
            std::string_view("diuxXo").find(text[j+1]) != std::string_view::npos) text.erase(j, 1);
        if (text.compare(j, 3, "I64") == 0) text.replace(j, 3, "ll");
        if (text[j] == 'S') text[j] = 's';
        i = j;
    }
    return text;
}
} // namespace wiz8::text

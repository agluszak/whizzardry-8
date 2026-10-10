#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace wiz8::text {
// Unicode conversions are locale independent. Malformed sequences become
// U+FFFD; a bounded retail field ends at its first NUL or its declared extent.
std::string from_utf16(std::u16string_view text);
std::u16string to_utf16(std::string_view text);
std::string from_utf16le(std::span<const std::byte> bytes, bool encoded = false);
void to_utf16le(std::string_view text, std::span<std::byte> bytes);
std::string retail_format(std::string text);

// Advances over one complete UTF-8 scalar; malformed input advances one byte.
char32_t next(std::string_view text, std::size_t& offset);
std::size_t previous(std::string_view text, std::size_t offset);
std::size_t prefix(std::string_view text, std::size_t byte_limit);
void copy(char* destination, std::size_t capacity, std::string_view text);
} // namespace wiz8::text

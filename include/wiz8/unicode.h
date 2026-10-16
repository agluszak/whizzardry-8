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
// Fixed-size disk fields are zero-padded and NUL terminated.
void write_utf8_field(std::string_view text, std::span<char> field);

// Append one Unicode scalar; invalid values become U+FFFD.
void append(std::string& text, char32_t character);
// Advances over one complete UTF-8 scalar; malformed input advances one byte.
char32_t next(std::string_view text, std::size_t& offset);
std::size_t previous(std::string_view text, std::size_t offset);
std::size_t byte_prefix(std::string_view text, std::size_t byte_limit);
std::size_t codepoint_prefix(std::string_view text, std::size_t count);
std::size_t next_grapheme(std::string_view text, std::size_t byte_offset);
std::size_t previous_grapheme(std::string_view text, std::size_t byte_offset);
std::size_t grapheme_count(std::string_view text);
} // namespace wiz8::text

bool IsUppercaseCharacter(std::uint32_t character);
bool IsLowercaseCharacter(std::uint32_t character);
bool IsPunctuationCharacter(std::uint32_t character);
std::int32_t ToUppercaseCharacter(std::int32_t character);
std::int32_t ToLowercaseCharacter(std::int32_t character);
std::int32_t CompareTextIgnoreAsciiCase(std::string_view first, std::string_view second);

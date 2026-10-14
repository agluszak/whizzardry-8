#include "wiz8/local_code/Strings.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wiz8/filesystem.h"
#include "wiz8/string_database.h"
#include "wiz8/unicode.h"

// GLOBAL: WIZ8 0x0068c098
int giStringListLen;
// GLOBAL: WIZ8 0x0068c09c
std::vector<std::string> g_localized_strings;

// FUNCTION: WIZ8 0x0052FF80
std::string GetStringFromStringDatabase(std::string_view path, int index, unsigned int* metadata_00,
                                        unsigned int* metadata_04)
try {
    auto handle = wiz8::open_file(path);
    unsigned char header[5];
    handle->read_exact(header, sizeof(header));
    int count;
    handle->seek(-8, wiz8::SeekOrigin::end);
    handle->read_exact(&count, 4);
    if (index < 0 || count < 0 || index >= count) {
        return {};
    }
    int entry_offset;
    handle->seek(-static_cast<std::int64_t>((count - index) * 4LL + 8), wiz8::SeekOrigin::end);
    handle->read_exact(&entry_offset, 4);
    if (entry_offset < 5) {
        return {};
    }
    handle->seek(entry_offset, wiz8::SeekOrigin::begin);
    unsigned int first, second;
    handle->read_exact(&first, 4);
    handle->read_exact(&second, 4);
    int length;
    handle->read_exact(&length, 4);
    if (length < 0 || length > 2000) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
    handle->read_exact(bytes.data(), bytes.size());
    const auto text = wiz8::text::from_utf16le(bytes, header[4] != 0);
    if (metadata_00) {
        *metadata_00 = first;
    }
    if (metadata_04) {
        *metadata_04 = second;
    }
    return text;
} catch (const std::exception&) {
    return {};
}

// FUNCTION: WIZ8 0x00518500
void ReleaseLocalizedStrings()
{
    giStringListLen = 0;
    g_localized_strings.clear();
}

// Windows localization uses %S for UTF-16 strings and I64 for integers.
// Convert that vocabulary once during import; {fmt} handles runtime formatting.
static std::string ImportLocalizedPattern(std::string text)
{
    static const std::regex specifier(
        R"(%%|%([-+ #0]*[0-9]*(?:\.[0-9]+)?)(?:[hl])?S|%([-+ #0]*[0-9]*(?:\.[0-9]+)?)I64([diuoxX]))");
    std::string result;
    std::size_t position = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), specifier);
         it != std::sregex_iterator(); ++it) {
        const auto& match = *it;
        result.append(text, position, match.position() - position);
        if (match.str() == "%%") {
            result += "%%";
        } else if (match[3].matched) {
            result += "%" + match[2].str() + "ll" + match[3].str();
        } else {
            result += "%" + match[1].str() + "s";
        }
        position = match.position() + match.length();
    }
    result.append(text, position);
    return result;
}

// FUNCTION: WIZ8 0x00518360
void LoadLocalizedStrings(std::string_view path)
{
    auto handle = wiz8::open_file(path);
    int count;
    handle->read_exact(&count, 4);
    if (count <= 0 || count > 100000) {
        throw std::runtime_error("Invalid localization entry count");
    }
    std::vector<std::string> pending;
    pending.reserve(count);
    for (int index = 0; index < count; ++index) {
        int byte_count;
        handle->read_exact(&byte_count, 4);
        if (byte_count < 0 || byte_count > 1024 * 1024 || byte_count % 2) {
            throw std::runtime_error("Invalid localization text length");
        }
        std::vector<std::byte> bytes(byte_count);
        handle->read_exact(bytes.data(), bytes.size());
        pending.push_back(ImportLocalizedPattern(wiz8::text::from_utf16le(bytes, true)));
    }
    g_localized_strings = std::move(pending);
    giStringListLen = count;
}

#include "wiz8/sr_api.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/string_database.h"
#include "wiz8/filesystem.h"
#include "wiz8/unicode.h"
#include <vector>
#include <stdexcept>

int giStringListLen;
char** gppStringList;
namespace {
std::vector<std::string> localized_strings;
std::vector<char*> localized_views;
}

unsigned char GetStringFromStringDatabase(const char* path, int index, std::span<char> output,
                                          unsigned int* metadata_00, unsigned int* metadata_04)
try
{
    if (output.empty()) return 0;
    output[0] = 0;
    auto handle = wiz8::open_file(path);
    unsigned char header[5];
    handle->read_exact(header, sizeof(header));
    int count;
    handle->seek(-8, wiz8::SeekOrigin::end);
    handle->read_exact(&count, 4);
    if (index < 0 || count < 0 || index >= count) return 0;
    int entry_offset;
    handle->seek(-static_cast<std::int64_t>((count - index) * 4LL + 8), wiz8::SeekOrigin::end);
    handle->read_exact(&entry_offset, 4);
    if (entry_offset < 5) return 0;
    handle->seek(entry_offset, wiz8::SeekOrigin::begin);
    unsigned int first, second;
    handle->read_exact(&first, 4);
    handle->read_exact(&second, 4);
    int length;
    handle->read_exact(&length, 4);
    if (length < 0 || length > 2000) return 0;
    std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
    handle->read_exact(bytes.data(), bytes.size());
    const auto text = wiz8::text::from_utf16le(bytes, header[4] != 0);
    if (text.size() >= output.size()) return 0;
    wiz8::text::copy(output.data(), output.size(), text);
    if (metadata_00) *metadata_00 = first;
    if (metadata_04) *metadata_04 = second;
    return 1;
}
catch (const std::exception&) { return 0; }

void ReleaseLocalizedStrings()
{
    gppStringList = nullptr;
    giStringListLen = 0;
    localized_views.clear();
    localized_strings.clear();
}

void LoadLocalizedStrings(const char* path)
{
    auto handle = wiz8::open_file(path);
    int count;
    handle->read_exact(&count, 4);
    if (count <= 0 || count > 100000) throw std::runtime_error("Invalid localization entry count");
    std::vector<std::string> pending;
    pending.reserve(count);
    for (int index = 0; index < count; ++index) {
        int byte_count;
        handle->read_exact(&byte_count, 4);
        if (byte_count < 0 || byte_count > 1024 * 1024 || byte_count % 2)
            throw std::runtime_error("Invalid localization text length");
        std::vector<std::byte> bytes(byte_count);
        handle->read_exact(bytes.data(), bytes.size());
        pending.push_back(wiz8::text::retail_format(wiz8::text::from_utf16le(bytes, true)));
    }
    localized_strings = std::move(pending);
    localized_views.clear();
    localized_views.reserve(localized_strings.size());
    for (auto& text : localized_strings) localized_views.push_back(text.data());
    gppStringList = localized_views.data();
    giStringListLen = count;
}

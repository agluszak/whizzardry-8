#include "wiz8/local_code/Strings.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/string_database.h"
#include "wiz8/unicode.h"

std::string GetStringFromStringDatabase(std::string_view path, int index, unsigned int* metadata_00,
                                        unsigned int* metadata_04)
try {
    auto handle = wiz8::open_file(wiz8::asset_path(path));
    unsigned char header[5];
    handle->read_exact(header, sizeof(header));
    int count;
    handle->seek(-8, wiz8::SeekOrigin::end);
    handle->read_exact(&count, 4);
    if (index < 0 || count < 0 || index >= count) return {};
    int entry_offset;
    handle->seek(-static_cast<std::int64_t>((count - index) * 4LL + 8), wiz8::SeekOrigin::end);
    handle->read_exact(&entry_offset, 4);
    if (entry_offset < 5) return {};
    handle->seek(entry_offset, wiz8::SeekOrigin::begin);
    unsigned int first, second;
    handle->read_exact(&first, 4);
    handle->read_exact(&second, 4);
    int length;
    handle->read_exact(&length, 4);
    if (length < 0 || length > 2000) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
    handle->read_exact(bytes.data(), bytes.size());
    const auto text = wiz8::text::from_utf16le(bytes, header[4] != 0);
    if (metadata_00) *metadata_00 = first;
    if (metadata_04) *metadata_04 = second;
    return text;
} catch (const std::exception&) {
    return {};
}

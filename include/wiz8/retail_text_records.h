#pragma once

#include "wiz8/unicode.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/filesystem.h"
#include "wiz8/chunk.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace wiz8::retail {
// Runtime text buffers contain UTF-8. These descriptors are the explicit
// UTF-16LE format boundary. All accesses use bytes, including odd offsets.
struct TextField { std::size_t offset, capacity, units; };
template<class T> struct TextFields;
template<> struct TextFields<W8ItemDatabaseRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8ItemDatabaseRecord, display_name), sizeof(W8ItemDatabaseRecord::display_name), 30},
    };
};
template<> struct TextFields<W8SpellRuntimeRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8SpellRuntimeRecord, display_name), sizeof(W8SpellRuntimeRecord::display_name), 64},
    };
};
template<> struct TextFields<W8FactDatabaseRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8FactDatabaseRecord, alternate_description), sizeof(W8FactDatabaseRecord::alternate_description), 0x64},
        TextField{offsetof(W8FactDatabaseRecord, description), sizeof(W8FactDatabaseRecord::description), 0x6c},
    };
};
template<> struct TextFields<W8NpcCharacterTemplate> {
    static constexpr std::array fields = {
        TextField{offsetof(W8NpcCharacterTemplate, name), sizeof(W8NpcCharacterTemplate::name), 10},
        TextField{offsetof(W8NpcCharacterTemplate, name_part_2), sizeof(W8NpcCharacterTemplate::name_part_2), 40},
    };
};
template<> struct TextFields<W8NpcDatabaseRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8NpcDatabaseRecord, source_name), sizeof(W8NpcDatabaseRecord::source_name), 0x28},
        TextField{offsetof(W8NpcDatabaseRecord, character) + offsetof(W8NpcCharacterTemplate, name), sizeof(W8NpcCharacterTemplate::name), 10},
        TextField{offsetof(W8NpcDatabaseRecord, character) + offsetof(W8NpcCharacterTemplate, name_part_2), sizeof(W8NpcCharacterTemplate::name_part_2), 40},
    };
};
template<> struct TextFields<W8LevelDatabaseRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8LevelDatabaseRecord, display_name), sizeof(W8LevelDatabaseRecord::display_name), 30},
    };
};
template<> struct TextFields<W8MonsterRecord> {
    static constexpr std::array fields = {
        TextField{offsetof(W8MonsterRecord, name0), sizeof(W8MonsterRecord::name0), 24},
        TextField{offsetof(W8MonsterRecord, name1), sizeof(W8MonsterRecord::name1), 24},
        TextField{offsetof(W8MonsterRecord, name2), sizeof(W8MonsterRecord::name2), 24},
        TextField{offsetof(W8MonsterRecord, name3), sizeof(W8MonsterRecord::name3), 24},
    };
};
template<> struct TextFields<W8Character> {
    static constexpr std::array fields = {
        TextField{offsetof(W8Character, name), sizeof(W8Character::name), 10},
        TextField{offsetof(W8Character, name_part_2), sizeof(W8Character::name_part_2), 40},
    };
};
template<> struct TextFields<W8GlobalStatus> {
    static constexpr std::array fields = {
        TextField{offsetof(W8GlobalStatus, monster_name_buffer), sizeof(W8GlobalStatus::monster_name_buffer), 22},
    };
};

template<class T> constexpr std::size_t size = [] {
    std::size_t bytes = sizeof(T);
    for (auto field : TextFields<T>::fields) bytes -= field.capacity - 2 * field.units;
    return bytes;
}();

template<class T> void decode(std::span<const std::byte> disk, T& runtime)
{
    if (disk.size() > size<T>) throw std::runtime_error("Oversized retail record");
    // Older save versions omit trailing fields; zero-extend before decoding.
    std::array<std::byte, size<T>> image{};
    if (!disk.empty()) std::memcpy(image.data(), disk.data(), disk.size());
    auto* out = reinterpret_cast<std::byte*>(&runtime);
    std::size_t source = 0, destination = 0;
    for (auto field : TextFields<T>::fields) {
        auto count = field.offset - destination;
        std::memcpy(out + destination, image.data() + source, count);
        source += count;
        const auto text = text::from_utf16le(std::span(image).subspan(source, field.units * 2));
        std::memset(out + field.offset, 0, field.capacity);
        text::copy(reinterpret_cast<char*>(out + field.offset), field.capacity, text);
        source += field.units * 2;
        destination = field.offset + field.capacity;
    }
    std::memcpy(out + destination, image.data() + source, sizeof(T) - destination);
}
template<class T> auto encode(const T& runtime)
{
    std::array<std::byte, size<T>> image{};
    const auto* in = reinterpret_cast<const std::byte*>(&runtime);
    std::size_t source = 0, destination = 0;
    for (auto field : TextFields<T>::fields) {
        auto count = field.offset - source;
        std::memcpy(image.data() + destination, in + source, count);
        destination += count;
        const auto* text = reinterpret_cast<const char*>(in + field.offset);
        const auto* end = static_cast<const char*>(std::memchr(text, 0, field.capacity));
        text::to_utf16le(std::string_view(text, end ? end - text : field.capacity),
                        std::span(image).subspan(destination, field.units * 2));
        destination += field.units * 2;
        source = field.offset + field.capacity;
    }
    std::memcpy(image.data() + destination, in + source, sizeof(T) - source);
    return image;
}
template<class T> ReadResult read(File& file, T& runtime)
{
    std::array<std::byte, size<T>> image{};
    auto result = file.read(image.data(), image.size());
    if (result.bytes == image.size()) decode<T>(image, runtime);
    return result;
}
template<class T> void write(File& file, const T& runtime)
{
    auto image = encode(runtime);
    file.write(image.data(), image.size());
}
template<class T> bool read(W8Chunk& chunk, T& runtime, std::size_t bytes = size<T>)
{
    if (bytes > size<T>) return false;
    std::array<std::byte, size<T>> image{};
    if (!chunk.Read(image.data(), bytes, nullptr)) return false;
    decode<T>(image, runtime);
    return true;
}
template<class T> bool write(W8Chunk& chunk, const T& runtime)
{
    auto image = encode(runtime);
    return chunk.Write(image.data(), image.size(), nullptr);
}
} // namespace wiz8::retail

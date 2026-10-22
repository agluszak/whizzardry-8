#include <algorithm>
#include <array>
#include <clocale>
#include <cstddef>
#include <cwchar>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "retail_records.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/data/records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/character.h"
#include "wiz8/unicode.h"

static_assert(wiz8::retail::kMonsterRecordBytes == 0x297);
static_assert(wiz8::retail::kCharacterBytes == 0x1862);
static_assert(wiz8::retail::kNpcCharacterTemplateBytes == 0x206);
static_assert(wiz8::retail::kNpcDatabaseRecordBytes == 0x309);
static_assert(wiz8::retail::kItemDatabaseRecordBytes == 0x10d);
static_assert(wiz8::retail::kLevelDatabaseRecordBytes == 0xd8);
static_assert(wiz8::retail::kSpellRuntimeRecordBytes == 0x1bf);
static_assert(wiz8::retail::kFactDatabaseRecordBytes == 0x1d8);

static void conversion_contracts()
{
    // Host wchar_t and libc keep their native ABI while game strings are UTF-8.
#ifndef _WIN32
    static_assert(sizeof(wchar_t) == 4);
#endif
    REQUIRE(std::wcslen(L"host") == 4);
    std::setlocale(LC_ALL, "C");
    const std::string native = "Vi é 中 😀";
    const std::u16string retail = u"Vi é 中 😀";
    REQUIRE(wiz8::text::from_utf16(retail) == native);
    REQUIRE(wiz8::text::to_utf16(native) == retail);
    const std::u16string invalid = {0xd800, u'A', 0xdc00};
    REQUIRE(wiz8::text::from_utf16(invalid) == "�A�");
    REQUIRE(wiz8::text::to_utf16(std::string("\xf0\x80\x80\x80", 4)) == u"�");
    std::string encoded;
    for (char32_t cp : {U'é', U'中', U'😀', char32_t{0xd800}, char32_t{0x110000}})
        wiz8::text::append(encoded, cp);
    REQUIRE(encoded == "é中😀��");
    const std::string malformed = "A\xf0\x80\x80\x80";
    std::size_t offset = 1;
    REQUIRE(wiz8::text::next(malformed, offset) == 0xfffd);
    REQUIRE(offset == 2); // Traversal always makes progress one byte on invalid input.
    REQUIRE(wiz8::text::previous("é中😀", 9) == 5);
    REQUIRE(wiz8::text::previous("é中😀", 5) == 2);
    REQUIRE(wiz8::text::previous("é中😀", 2) == 0);
    REQUIRE(wiz8::text::previous(malformed, 2) == 1);
    REQUIRE(wiz8::text::codepoint_prefix("é中😀", 1) == 2);
    REQUIRE(wiz8::text::codepoint_prefix("é中😀", 2) == 5);
    const std::string clusters = "é👩‍👩‍👧‍👦中";
    REQUIRE(wiz8::text::grapheme_count(clusters) == 3);
    auto first = wiz8::text::next_grapheme(clusters, 0);
    REQUIRE(first == std::string("é").size());
    auto second = wiz8::text::next_grapheme(clusters, first);
    REQUIRE(second == clusters.size() - std::string("中").size());
    REQUIRE(wiz8::text::previous_grapheme(clusters, second) == first);
    std::array<std::byte, 32> storage{};
    // Odd offsets must never be accessed through char16_t* or wchar_t*.
    auto odd = std::span(storage).subspan(3, 24);
    wiz8::text::to_utf16le(native, odd);
    REQUIRE(wiz8::text::from_utf16le(odd) == native);
    std::array<std::byte, 6> small{};
    wiz8::text::to_utf16le("A😀", small);
    REQUIRE(wiz8::text::from_utf16le(small) == "A");
    char truncated[6];
    wiz8::text::write_utf8_field("ééé", truncated);
    REQUIRE(std::string(truncated) == "éé");
    REQUIRE((truncated[4] == '\0' && truncated[5] == '\0'));
    wiz8::text::write_utf8_field({}, truncated);
    REQUIRE(
        std::all_of(std::begin(truncated), std::end(truncated), [](char c) { return c == '\0'; }));
}

static void missile_record_contracts()
{
    W8MissileTableRecord missile{};
    missile.display_name = "火箭é😀";
    missile.cycle_name = "FIREBALL";
    missile.radius = 2.5f;
    missile.attack_mode = static_cast<W8AttackMode>(2);
    missile.spell_missile = true;
    missile.magnitude_base = 13;
    missile.weapon_sound_class = 7;
    std::fill(std::begin(missile.unknown_169), std::end(missile.unknown_169), 0x9a);
    const auto bytes = wiz8::retail::encode(missile);
    REQUIRE(bytes.size() == 0x1e5);
    REQUIRE((bytes[0x100] == std::byte{'F'} && bytes[0x154] == std::byte{1}));
    W8MissileTableRecord decoded{};
    wiz8::retail::decode<W8MissileTableRecord>(bytes, decoded);
    REQUIRE((decoded.display_name == missile.display_name && decoded.cycle_name == "FIREBALL"));
    REQUIRE((decoded.radius == 2.5f && decoded.magnitude_base == 13));
    REQUIRE(wiz8::retail::encode(decoded) == bytes);
}

static void record_contracts()
{
    W8Character original{};
    original.record_version = 2;
    original.name = "é中😀";
    original.name_part_2 = "é中😀";
    original.iRace = W8_RACE_ELF;
    auto bytes = wiz8::retail::encode(original);
    REQUIRE((bytes[5] == std::byte{0xe9} && bytes[6] == std::byte{0}));
    REQUIRE((bytes[25] == std::byte{0xe9} && bytes[26] == std::byte{}));
    W8Character decoded{};
    wiz8::retail::decode<W8Character>(bytes, decoded);
    REQUIRE(std::string(decoded.name) == original.name);
    REQUIRE(
        (std::string(decoded.name_part_2) == original.name_part_2 && decoded.iRace == W8_RACE_ELF));
    REQUIRE(wiz8::retail::encode(decoded) == bytes);
    wiz8::retail::decode<W8Character>(std::span(bytes).first(0x185c), decoded);
    REQUIRE(std::string(decoded.name) == "é中😀");
    // The full name fills 0x19..0x69: retail accepts 39 characters, and NPC full names
    // such as this one run that long.
    decoded.name_part_2 = "Madras, Gadgeteer to the Stars";
    bytes = wiz8::retail::encode(decoded);
    REQUIRE(bytes[0x19 + 29 * 2] == std::byte{'s'});
    wiz8::retail::decode<W8Character>(bytes, decoded);
    REQUIRE(decoded.name_part_2 == "Madras, Gadgeteer to the Stars");
    W8SpellRuntimeRecord spell{};
    spell.display_name = "é中😀";
    spell.spell_point_cost = 17;
    spell.effect_radius = 2.5f;
    auto spell_bytes = wiz8::retail::encode(spell);
    REQUIRE(spell_bytes[0x9b] == std::byte{0xe9});
    for (auto [offset, count] : std::array<std::pair<std::size_t, std::size_t>, 5>{
             {{0x40, 4}, {0x55, 1}, {0x11b, 4}, {0x125, 1}, {0x145, 2}}}) {
        for (std::size_t i = 0; i < count; ++i) spell_bytes[offset + i] = std::byte(0xa0 + i);
    }
    W8SpellRuntimeRecord spell_copy{};
    wiz8::retail::decode<W8SpellRuntimeRecord>(spell_bytes, spell_copy);
    REQUIRE(std::string(spell_copy.display_name) == spell.display_name);
    REQUIRE((spell_copy.spell_point_cost == 17 && spell_copy.effect_radius == 2.5f));
    REQUIRE(wiz8::retail::encode(spell_copy) == spell_bytes);
    spell_copy.duration = 42;
    auto spell_edited = wiz8::retail::encode(spell_copy);
    auto duration_offset = offsetof(wiz8::retail::SpellRuntimeRecord, duration);
    std::copy_n(spell_edited.begin() + duration_offset, sizeof(int),
                spell_bytes.begin() + duration_offset);
    REQUIRE(spell_edited == spell_bytes);
    W8NpcDatabaseRecord npc{};
    npc.source_name = "商人";
    npc.character.name = "é中😀";
    npc.character.name_part_2 = "Madras, Gadgeteer to the Stars";
    auto npc_bytes = wiz8::retail::encode(npc);
    W8NpcDatabaseRecord copy{};
    wiz8::retail::decode<W8NpcDatabaseRecord>(npc_bytes, copy);
    REQUIRE(std::string(copy.source_name) == "商人");
    REQUIRE(std::string(copy.character.name) == "é中😀");
    REQUIRE(copy.character.name_part_2 == "Madras, Gadgeteer to the Stars");
    REQUIRE(wiz8::retail::encode(copy) == npc_bytes);
}

TEST_CASE("UTF-8 and host Unicode conversion", "[unicode]")
{
    conversion_contracts();
}

TEST_CASE("Unicode retail records", "[unicode][serialization]")
{
    record_contracts();
}

TEST_CASE("Unicode missile records", "[unicode][serialization]")
{
    missile_record_contracts();
}

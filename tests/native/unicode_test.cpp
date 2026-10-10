#include "wiz8/unicode.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/string_database.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "temporary_directory.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <string>
#include <cwchar>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static_assert(wiz8::retail::size<W8MonsterRecord> == 0x297);
static_assert(wiz8::retail::size<W8Character> == 0x1862);
static_assert(wiz8::retail::size<W8NpcCharacterTemplate> == 0x206);
static_assert(wiz8::retail::size<W8NpcDatabaseRecord> == 0x309);
static_assert(wiz8::retail::size<W8ItemDatabaseRecord> == 0x10d);
static_assert(wiz8::retail::size<W8LevelDatabaseRecord> == 0xd8);
static_assert(wiz8::retail::size<W8SpellRuntimeRecord> == 0x1bf);
static_assert(wiz8::retail::size<W8FactDatabaseRecord> == 0x1d8);

static void conversion_contracts()
{
    // Host wchar_t and libc keep their native ABI while game strings are UTF-8.
#ifndef _WIN32
    static_assert(sizeof(wchar_t) == 4);
#endif
    CHECK(std::wcslen(L"host") == 4);
    std::setlocale(LC_ALL, "C");
    const std::string native = "Vi é 中 😀";
    const std::u16string retail = u"Vi é 中 😀";
    CHECK(wiz8::text::from_utf16(retail) == native);
    CHECK(wiz8::text::to_utf16(native) == retail);
    const std::u16string invalid = {0xd800, u'A', 0xdc00};
    CHECK(wiz8::text::from_utf16(invalid) == "�A�");
    CHECK(wiz8::text::to_utf16(std::string("\xf0\x80\x80\x80", 4)) == u"����");
    std::array<std::byte, 32> storage{};
    // Odd offsets must never be accessed through char16_t* or wchar_t*.
    auto odd = std::span(storage).subspan(3, 24);
    wiz8::text::to_utf16le(native, odd);
    CHECK(wiz8::text::from_utf16le(odd) == native);
    std::array<std::byte, 6> small{};
    wiz8::text::to_utf16le("A😀", small);
    CHECK(wiz8::text::from_utf16le(small) == "A");
    char truncated[4];
    wiz8::text::copy(truncated, sizeof(truncated), "éé");
    CHECK(std::string(truncated) == "é");
    CHECK(wiz8::text::retail_format("%s %S %hs %ls %%S") == "%s %s %s %s %%S");
    CHECK(wiz8::text::retail_format("%ld %I64d %llu") == "%d %lld %llu");
}

static void record_contracts()
{
    W8Character original{};
    original.record_version = 2;
    std::strcpy(original.name, "é中😀");
    std::strcpy(original.name_part_2, "Long secondary name é中😀");
    original.iRace = W8_RACE_ELF;
    auto bytes = wiz8::retail::encode(original);
    CHECK(bytes[5] == std::byte{0xe9} && bytes[6] == std::byte{0});
    CHECK(bytes[25] == std::byte{'L'} && bytes[26] == std::byte{});
    W8Character decoded{};
    wiz8::retail::decode<W8Character>(bytes, decoded);
    CHECK(std::string(decoded.name) == original.name);
    CHECK(std::string(decoded.name_part_2) == original.name_part_2 && decoded.iRace == W8_RACE_ELF);
    CHECK(wiz8::retail::encode(decoded) == bytes);
    wiz8::retail::decode<W8Character>(std::span(bytes).first(0x185c), decoded);
    CHECK(std::string(decoded.name) == "é中😀");
    W8SpellRuntimeRecord spell{};
    std::strcpy(spell.display_name, "é中😀");
    spell.spell_point_cost = 17;
    spell.effect_radius = 2.5f;
    auto spell_bytes = wiz8::retail::encode(spell);
    CHECK(spell_bytes[0x9b] == std::byte{0xe9});
    W8SpellRuntimeRecord spell_copy{};
    wiz8::retail::decode<W8SpellRuntimeRecord>(spell_bytes, spell_copy);
    CHECK(std::string(spell_copy.display_name) == spell.display_name);
    CHECK(spell_copy.spell_point_cost == 17 && spell_copy.effect_radius == 2.5f);
    CHECK(wiz8::retail::encode(spell_copy) == spell_bytes);
    W8NpcDatabaseRecord npc{};
    std::strcpy(npc.source_name, "商人");
    std::strcpy(npc.character.name, "é中😀");
    auto npc_bytes = wiz8::retail::encode(npc);
    W8NpcDatabaseRecord copy{};
    wiz8::retail::decode<W8NpcDatabaseRecord>(npc_bytes, copy);
    CHECK(std::string(copy.source_name) == "商人");
    CHECK(std::string(copy.character.name) == "é中😀");
}

int main()
{
    conversion_contracts();
    record_contracts();
    std::puts("ok");
}

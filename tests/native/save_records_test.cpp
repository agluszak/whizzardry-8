#include <limits>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_time.h>
#include <catch2/catch_test_macros.hpp>

#include "asset_paths.h"
#include "catch2/catch_message.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/encounter_tables.h"
#include "layouts/item_tables.h"
#include "layouts/levels.h"
#include "layouts/npc_dialogue.h"
#include "retail_records.h"
#include "save_payload.h"
#include "scoped_combat.h"
#include "unicode.h"
#include "wiz8/checked_integer.h"
#include "wiz8/compat/native.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/fact_state.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/render/mesh_instance.h" // IWYU pragma: keep - complete model type for unique_ptr destruction.
#include "wiz8/render/quality.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/string_database.h"
#include "wiz8/world/world.h"
/* Exercise recovered NPCT loading with retail addresses and native handles as
   on-disk presence markers. No installed game data or display is required. */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wiz8/file_time.h>
#include <wiz8/filesystem.h>

#include "file_faults.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/chunk.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/script.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/rules/character.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/xstatus.h"

extern std::vector<std::unique_ptr<W8NpcState>> g_npc_states;

static std::vector<W8NpcState*> NpcStatePointers()
{
    std::vector<W8NpcState*> pointers;
    for (const auto& state : g_npc_states) pointers.push_back(state.get());
    return pointers;
}

template<class T> static void write(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    REQUIRE(out.good());
}

static void character_file_contracts(const std::filesystem::path& assets,
                                     const std::filesystem::path& user)
{
    // The checked-in retail character supplies real nonzero gameplay and
    // unknown fields, independent of the synthetic Unicode fixtures.
    {
        const auto fixture = std::filesystem::path(WIZ8_CHARACTER_FIXTURE);
        std::ifstream file(fixture, std::ios::binary);
        std::uint32_t extent;
        file.read(reinterpret_cast<char*>(&extent), sizeof(extent));
        REQUIRE((file.good() && extent == wiz8::retail::kCharacterBytes));
        std::array<std::byte, wiz8::retail::kCharacterBytes> bytes;
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        REQUIRE(file.good());
        W8Character character{};
        wiz8::retail::decode<W8Character>(bytes, character);
        REQUIRE((character.name == "vi" && character.name_part_2 == "probe"));
        REQUIRE(wiz8::retail::encode(character) == bytes);
        // Native skills have padding; the retail stride and unknown bytes must
        // remain exact across both adjacent records.
        constexpr std::uint32_t points = 0x12345678, level = 0x23456789;
        character.skills[0].points = points;
        character.skills[1].level = level;
        character.skills[0].unknown_14[0] = 0xa5;
        character.Hand[1].attacks = 7;
        character.conditions[1].source_monster = 42;
        auto expected = bytes;
        std::memcpy(expected.data() + 0x19d + 2, &points, sizeof(points));
        std::memcpy(expected.data() + 0x19d + 0x26 + 6, &level, sizeof(level));
        expected[0x19d + 0x14] = std::byte{0xa5};
        const unsigned int attacks = 7;
        const int source_monster = 42;
        std::memcpy(expected.data() + 0x1149 + 0x5b + 0x15, &attacks, sizeof(attacks));
        std::memcpy(expected.data() + 0x1817 + 0x11 + 4, &source_monster, sizeof(source_monster));
        REQUIRE(wiz8::retail::encode(character) == expected);
        wiz8::retail::decode<W8Character>(expected, character);
        REQUIRE(character.skills[0].points == points);
        REQUIRE(character.skills[1].level == level);
        REQUIRE(character.skills[0].unknown_14[0] == 0xa5);
        REQUIRE(character.Hand[1].attacks == 7);
        REQUIRE(character.conditions[1].source_monster == 42);
        character.name = "改名";
        const auto edited = wiz8::retail::encode(character);
        REQUIRE(std::equal(bytes.begin() + 0x25, bytes.begin() + 0x69, edited.begin() + 0x25));
    }
    g_status.game_started = false;
    REQUIRE(VerifyDataSubdirs());
    for (const char* name : {"A", "Vi", "Sir Bob", "Ninechars", "é中😀", "中文名字测试"}) {
        W8Character character = {};
        character.name = name;
        character.record_version = 2;
        character.uiExpLevel = 7;
        std::string path;
        path = BuildCharacterPath(name, -1);
        const std::string filename = std::string(name) + ".CHR";
        REQUIRE(SaveCharacter(&character, -1));
        REQUIRE(character.record_version == 2);
        auto expected = character;
        expected.record_version = 1;
        const auto saved = wiz8::user_path(path);
        REQUIRE(std::filesystem::file_size(saved) == 4 + wiz8::retail::kCharacterBytes);
        REQUIRE(!std::filesystem::exists(assets / "saves" / "characters" / filename));
        std::ifstream encoded(saved, std::ios::binary);
        unsigned int size = 0;
        encoded.read(reinterpret_cast<char*>(&size), sizeof(size));
        REQUIRE((encoded.good() && size == wiz8::retail::kCharacterBytes));
        encoded.close();
        W8Character loaded = {};
        REQUIRE(LoadCharacter(filename.c_str(), &loaded, -1));
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(expected));
        std::size_t operations;
        {
            FileFaults trace;
            REQUIRE(SaveCharacter(&character, -1));
            operations = trace.operations;
        }
        W8Character replacement = character;
        replacement.uiExpLevel = 99;
        for (std::size_t operation = 1; operation <= operations; ++operation) {
            {
                FileFaults fault(operation);
                REQUIRE(!SaveCharacter(&replacement, -1));
                REQUIRE(fault.fired);
            }
            REQUIRE(LoadCharacter(filename.c_str(), &loaded, -1));
            REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(expected));
        }
        character.uiExpLevel = 8;
        expected.uiExpLevel = 8;
        REQUIRE(SaveCharacter(&character, -1));
        REQUIRE(LoadCharacter(filename.c_str(), &loaded, -1));
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(expected));
    }

    // I/O errors report failure. A directory at the destination cannot be opened as a file.
    W8Character blocked{};
    blocked.name = "Blocked";
    std::filesystem::create_directory(user / "saves" / "characters" / "Blocked.CHR");
    REQUIRE(!SaveCharacter(&blocked, -1));

    W8Character npc = {};
    npc.name = "Vi";
    std::string path;
    path = BuildCharacterPath(npc.name, 0);

    npc.record_version = 1;
    std::filesystem::create_directories(assets / "saves/npcs");
    {
        auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
        wiz8::retail::write_sized(*file, npc);
    }
    W8Character loaded = {};
    REQUIRE(LoadCharacter("vi.CHR", &loaded, 0));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(npc));

    g_status.game_started = true;
    g_status.flags[0] = 0;
    path = BuildCharacterPath(npc.name, 0);

    g_status.flags[0] = 1;
    path = BuildCharacterPath(npc.name, 0);

    path = BuildCharacterPath(npc.name, -1);

    g_status.flags[0] = 0;
    g_status.game_started = false;
}

static void unicode_database_contracts()
{
    wiz8::create_directory(wiz8::asset_path("Data/Databases"));
    const unsigned count = 2;
    auto items =
        wiz8::open_file(wiz8::asset_path("data/databases/items.dbs"), wiz8::OpenMode::replace);
    items->write(&count, 4);
    for (unsigned i = 0; i < count; ++i) {
        W8ItemDatabaseRecord item{};
        item.display_name = i ? "药水😀" : "Épée";
        item.value = 100 + i;
        const auto image = wiz8::retail::encode(item);
        items->write(image.data(), image.size());
    }
    items->close();
    REQUIRE(InitializeItemDatabase());
    REQUIRE(g_item_records.size() == 2);
    REQUIRE(std::string(g_item_records[0].display_name) == "Épée");
    REQUIRE((std::string(g_item_records[1].display_name) == "药水😀" &&
             g_item_records[1].value == 101));
    DestroyItemDatabase();
    auto monsters =
        wiz8::open_file(wiz8::asset_path("data/databases/monsters.dbs"), wiz8::OpenMode::replace);
    monsters->write(&count, 4);
    for (unsigned i = 0; i < count; ++i) {
        W8MonsterRecord monster{};
        monster.name0 = i ? "怪物😀" : "Élan";
        monster.name1 = i ? "怪物们" : "Élans";
        const auto image = wiz8::retail::encode(monster);
        monsters->write(image.data(), image.size());
    }
    monsters->close();
    std::unique_ptr<W8MonsterRecord[]> records;
    REQUIRE(LoadMonsterDatabase(&records));
    REQUIRE((records && std::string(records[1].name0) == "怪物😀"));
    W8MonsterRecord single{};
    REQUIRE(LoadMonsterDatabaseRecord(1, &single));
    REQUIRE(std::string(single.name0) == "怪物😀");
    W8MonsterRecord range[2]{};
    REQUIRE(LoadMonsterDatabaseRange(0, 1, range));
    REQUIRE((std::string(range[0].name1) == "Élans" && std::string(range[1].name1) == "怪物们"));
}

static void unicode_file_contracts()
{
    wiz8::create_directory(wiz8::asset_path("Data/Strings"));
    auto retail_bytes = [](std::string_view text, bool encrypted) {
        const auto units = wiz8::text::to_utf16(text);
        std::vector<std::byte> bytes((units.size() + 1) * 2);
        wiz8::text::to_utf16le(text, bytes);
        if (encrypted) {
            for (std::size_t index = 0; index < bytes.size(); index += 2) {
                auto unit = std::to_integer<unsigned>(bytes[index]) |
                            (std::to_integer<unsigned>(bytes[index + 1]) << 8);
                unit = static_cast<unsigned short>(~(unit - 0x9697));
                bytes[index] = static_cast<std::byte>(unit & 255);
                bytes[index + 1] = static_cast<std::byte>(unit >> 8);
            }
        }
        return bytes;
    };
    const std::string name = "é中😀";
    for (bool encrypted : {false, true}) {
        auto file =
            wiz8::open_file(wiz8::asset_path("Data/Strings/unicode.msg"), wiz8::OpenMode::replace);
        const unsigned char header[5] = {0, 0, 0, 0, static_cast<unsigned char>(encrypted)};
        file->write(header, sizeof(header));
        const auto bytes = retail_bytes(name, encrypted);
        const auto length = wiz8::checked_integer<int>(bytes.size() / 2);
        const unsigned first = 123, second = 456;
        file->write(&first, 4);
        file->write(&second, 4);
        file->write(&length, 4);
        file->write(bytes.data(), bytes.size());
        const int offset = 5, count = 1, tail = 0;
        file->write(&offset, 4);
        file->write(&count, 4);
        file->write(&tail, 4);
        file->close();
        std::string output;
        unsigned metadata0 = 0, metadata1 = 0;
        REQUIRE((!(output = GetStringFromStringDatabase("Data/Strings/unicode.msg", 0, &metadata0,
                                                        &metadata1))
                      .empty()));
        REQUIRE((std::string(output) == name && metadata0 == 123 && metadata1 == 456));
        REQUIRE(!(!(output = GetStringFromStringDatabase("Data/Strings/unicode.msg", -1, nullptr,
                                                         nullptr))
                       .empty()));
        REQUIRE(output.empty());
        std::string complete;
        REQUIRE((complete = GetStringFromStringDatabase("Data/Strings/unicode.msg", 0, nullptr,
                                                        nullptr)) == name);
    }
    auto file =
        wiz8::open_file(wiz8::asset_path("Data/Strings/unicode.dat"), wiz8::OpenMode::replace);
    const int count = 1;
    const auto bytes = retail_bytes("é中😀 %S %hs %s", true);
    const auto length = wiz8::checked_integer<int>(bytes.size());
    file->write(&count, 4);
    file->write(&length, 4);
    file->write(bytes.data(), bytes.size());
    file->close();
    LoadLocalizedStrings("Data/Strings/unicode.dat");
    REQUIRE((giStringListLen == 1 && std::string(g_localized_strings[0]) == "é中😀 %s %hs %s"));
    ReleaseLocalizedStrings();
    REQUIRE((g_localized_strings.empty() && !giStringListLen));

    ReleaseMessageStorage();
    memset(g_status.text_box_lines_used, 0, sizeof(g_status.text_box_lines_used));
    g_status.text_box_lines_used[0] = 1;
    const std::string message = "é 中 😀";
    auto& line = g_message_storage[0][0];
    line.text = message;
    g_status.text_box_lines_shown[0] = 1;
    line.clock = w8_get_ticks() + 600000;
    line.saved_remaining_ms = 87;
    line.entries.push_back({0, 1, 0, 5, W8_NOTICE_WORD_SELECTED, true});
    line.length = 6; // Prefix through 中, stored as three UTF-16 units.
    line.highlight_start = 3; // two UTF-16 units
    line.highlight_stop = 6; // three UTF-16 units
    file = wiz8::open_file(wiz8::user_path("saves/text.bin"), wiz8::OpenMode::replace);
    REQUIRE(SaveMessageStorage(*file));
    REQUIRE(line.saved_remaining_ms == 87);
    file->close();
    ReleaseMessageStorage();
    g_status.save_version = 9.0f;
    file = wiz8::open_file(wiz8::user_path("saves/text.bin"));
    // The fixed record starts after format and region count; its obsolete
    // pointer word must never contain a live allocation address.
    std::array<std::byte, 8 + 0x24> header{};
    file->read_exact(header.data(), header.size());
    for (std::size_t i = 8 + 0x18; i < 8 + 0x1c; ++i) REQUIRE(header[i] == std::byte{});
    file->seek(0, wiz8::SeekOrigin::begin);
    REQUIRE(ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadMessageStorage(chunk); }));
    REQUIRE(std::string(g_message_storage[0][0].text) == message);
    REQUIRE(g_message_storage[0][0].saved_remaining_ms > 87);
    g_status.text_box_lines_shown[0] = 0;
    REQUIRE(g_message_storage[0][0].entries.empty());
    REQUIRE(g_message_storage[0][0].length == 6);
    REQUIRE((g_message_storage[0][0].highlight_start == 3 &&
             g_message_storage[0][0].highlight_stop == 6));
    ReleaseMessageStorage();
}

static void transcript_contracts()
{
    W8NpcInteractionState state{};
    auto* previous = g_npc_interaction_state;
    g_npc_interaction_state = &state;
    auto entry = std::make_unique<W8DialogueTranscriptRecord>();
    entry->text = "é中😀";
    entry->category = W8_DIALOGUE_CATEGORY_PLACES;
    state.dialogue_transcript.push_back(std::move(entry));
    state.dialogue_category_filter = W8_DIALOGUE_CATEGORY_MISC;
    state.transcript_sorted = true;
    auto file = wiz8::open_file(wiz8::user_path("saves/transcript.bin"), wiz8::OpenMode::replace);
    REQUIRE(SaveNpcDialogueTranscript(*file));
    file->close();
    file = wiz8::open_file(wiz8::user_path("saves/transcript.bin"));
    std::array<std::byte, 20> bytes{};
    file->read_exact(bytes.data(), bytes.size());
    REQUIRE((bytes[0] == std::byte{2} && bytes[1] == std::byte{1}));
    REQUIRE(bytes[5] == std::byte{4}); // Four UTF-16 units, not nine UTF-8 bytes.
    REQUIRE((bytes[9] == std::byte{0xe9} && bytes[10] == std::byte{0}));
    REQUIRE((bytes[13] == std::byte{0x3d} && bytes[14] == std::byte{0xd8}));
    file->seek(0, wiz8::SeekOrigin::begin);
    REQUIRE(
        ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadNpcDialogueTranscript(chunk); }));
    REQUIRE(state.dialogue_transcript.size() == 1);
    REQUIRE(state.dialogue_transcript[0]->text == "é中😀");
    REQUIRE(state.dialogue_transcript[0]->category == W8_DIALOGUE_CATEGORY_PLACES);
    REQUIRE(
        (state.dialogue_category_filter == W8_DIALOGUE_CATEGORY_MISC && state.transcript_sorted));
    ClearNpcDialogueTranscript();
    file->close();
    g_npc_interaction_state = previous;
}

static void keyword_file_contracts()
{
    wiz8::create_directory(wiz8::asset_path("Data/Strings"));
    auto write_keywords = [](const char* path, const std::string& text) {
        auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
        file->write(text.data(), text.size());
        file->close();
    };
    const char* english = "data/strings/english_keywords.txt";
    const char* translated = "data/strings/translated_keywords.txt";
    write_keywords(english, "header\r\n01234567890  hello / world /\r\n01234567890bye/last");
    ReloadKeywordLists();
    REQUIRE((!g_keyword_lists_loaded && g_keyword_lists.size() == 0));
    write_keywords(translated, "header\n01234567890bonjour/monde/\n01234567890au revoir/final");
    ReloadKeywordLists();
    REQUIRE((g_keyword_lists_loaded && g_keyword_lists.size() == 2));
    const auto& first = g_keyword_lists[0];
    REQUIRE(first.size() == 2);
    REQUIRE(first[0] == std::vector<std::string>({"hello", "world"}));
    REQUIRE(first[1] == std::vector<std::string>({"bye", "last"}));
    REQUIRE(TranslateDialogueKeyword("BONJOUR") == "hello");
    REQUIRE(TranslateDialogueKeyword("unknown") == "unknown");
    // Translated fields without an English counterpart remain unchanged.
    write_keywords(english, "header\n01234567890hello/");
    ReloadKeywordLists();
    REQUIRE(TranslateDialogueKeyword("monde") == "monde");
    REQUIRE(TranslateDialogueKeyword("final") == "final");
    ClearKeywordLists();
    REQUIRE((!g_keyword_lists_loaded && g_keyword_lists.size() == 0));
    write_keywords(english,
                   "header\n01234567890partial/row/\n01234567890" + std::string("\0bad", 4));
    ReloadKeywordLists();
    REQUIRE((!g_keyword_lists_loaded && g_keyword_lists.size() == 0));
    write_keywords(english, "header\n01234567890" + std::string(1000, 'x'));
    ReloadKeywordLists();
    REQUIRE((!g_keyword_lists_loaded && g_keyword_lists.size() == 0));
    REQUIRE(wiz8::remove_file(wiz8::asset_path(english)));
    ReloadKeywordLists();
    REQUIRE((!g_keyword_lists_loaded && g_keyword_lists.size() == 0));
}

static void path_record_contracts()
{
    constexpr auto filename = "data/path-records.bin";
    auto write_header = [](wiz8::File& file, unsigned char version, int count) {
        const unsigned char kind = 0;
        const float position = 0;
        const unsigned int unknown = 17;
        file.write(&kind, 1);
        file.write(&version, 1);
        file.write(&position, 4);
        file.write(&unknown, 4);
        file.write(&count, 4);
    };
    for (unsigned char version : {1, 2}) {
        {
            auto file = wiz8::open_file(wiz8::user_path(filename), wiz8::OpenMode::replace);
            write_header(*file, version, 2);
            for (int i = 0; i < 2; ++i) {
                const float point[] = {static_cast<float>(i), 0, 0};
                const float angle_axis[] = {0, 0, 1, 0};
                const float scale[] = {2, 3, 4};
                file->write(point, sizeof(point));
                file->write(angle_axis, sizeof(angle_axis));
                if (version == 2) file->write(scale, sizeof(scale));
            }
        }
        auto file = wiz8::open_file(wiz8::user_path(filename));
        auto path = LoadPathAI(*file);
        REQUIRE(path);
        REQUIRE((path->nodes.size() == 2 && path->rotations.size() == 2));
        REQUIRE((path->nodes[1].x == 500 && path->total_length == 500));
        REQUIRE((path->unknown_08 == 17 && path->rotations[0] == glm::mat3(1)));
        REQUIRE((path->scales.size() == (version == 2 ? 2 : 0)));
        if (version == 2) REQUIRE((path->scales[1].y == 3));
        path->timed = true;
        auto copy = std::optional<KeyframeTrack>(std::in_place, *path);
        auto restarted = path->restarted();
        REQUIRE((!restarted.timed));
        REQUIRE((copy->timed && copy->nodes.data() != path->nodes.data()));
        REQUIRE((copy->rotations.data() != path->rotations.data()));
        if (version == 2) REQUIRE((copy->scales.data() != path->scales.data()));
        copy->nodes[0].x = 99;
        REQUIRE((path->nodes[0].x == 0));
        PathAIClearOwned((copy ? &*copy : nullptr));
        PathAIClearOwned((copy ? &*copy : nullptr));
        REQUIRE((copy->nodes.empty() && copy->rotations.empty() && copy->scales.empty()));
        glm::vec3 point(7, 8, 9);
        PathAIPosition(nullptr, &point);
        PathAIPosition((copy ? &*copy : nullptr), &point);
        REQUIRE(point == glm::vec3(7, 8, 9));
        copy->looping = true;
        REQUIRE((!PathAINextPoint((copy ? &*copy : nullptr), &point)));
        REQUIRE((PathAINextPoint((path ? &*path : nullptr), &point) && point.x == 0));
        REQUIRE((PathAINextPoint((path ? &*path : nullptr), &point) && point.x == 500));
        REQUIRE((!PathAINextPoint((path ? &*path : nullptr), &point)));
        path->animated = true;
        path->point_index = 0;
        path->interpolation_fraction = 0.25f;
        PathAIPosition((path ? &*path : nullptr), &point);
        REQUIRE((point.x == 125));
    }
    for (int count : {-1, 0, 1, std::numeric_limits<int>::max()}) {
        {
            auto file = wiz8::open_file(wiz8::user_path(filename), wiz8::OpenMode::replace);
            write_header(*file, 2, count); // No point payload: count 1 is truncated.
        }
        auto file = wiz8::open_file(wiz8::user_path(filename));
        if (count == 0)
            REQUIRE_FALSE(LoadPathAI(*file));
        else
            REQUIRE_THROWS(LoadPathAI(*file));
    }
    REQUIRE((wiz8::remove_file(wiz8::user_path(filename))));
}

static void animation_record_contracts()
{
    constexpr auto filename = "data/animation-records.dat";
    for (unsigned char flag : {0, 255}) {
        {
            auto file = wiz8::open_file(wiz8::user_path(filename), wiz8::OpenMode::replace);
            const unsigned char header[] = {11, 0, flag, 0, 0, 0, flag};
            file->write(header, sizeof(header));
            const float speed = 15, chance = 1;
            file->write(&speed, sizeof(speed));
            const unsigned char frames[] = {0, 1, flag};
            file->write(frames, sizeof(frames));
            file->write(&chance, sizeof(chance));
            const unsigned char reserved[50]{};
            file->write(reserved, sizeof(reserved));
            const unsigned char count = 2;
            file->write(&count, 1);
            for (int i = 0; i < 2; ++i) {
                const float bounds[] = {-1, -2, -3, 1, 2, 3};
                file->write(bounds, sizeof(bounds));
            }
            const unsigned char zero = 0;
            file->write(&zero, 1); // No lights.
            if (!flag) file->write(&zero, 1); // No standalone path.
            file->write(&zero, 1); // Version 10's reserved byte.
        }
        auto file = wiz8::open_file(wiz8::user_path(filename));
        LevelLoadContext info{};
        info.hFile = file.get();
        auto animation = std::make_unique<Animation>();
        REQUIRE((AnimObjReadFromFile(&info, animation.get(), 0, nullptr, 0)));
        REQUIRE((animation->animation_playing == (flag != 0)));
        REQUIRE((animation->random_play == (flag != 0)));
        REQUIRE((((animation.get())->path_lists) == (flag != 0)));
        REQUIRE((animation->frame_bounds.size() == 2));
        const auto bounds = AnimObjGetBounds(animation.get(), 0, 1);
        REQUIRE(bounds);
        REQUIRE((bounds.value().minimum.y == -1000 && bounds.value().maximum.z == 1500));
        REQUIRE_FALSE(AnimObjGetBounds(nullptr, 0, 0));
        REQUIRE_FALSE(AnimObjGetBounds(animation.get(), -1, 1));
        REQUIRE_FALSE(AnimObjGetBounds(animation.get(), 3, 1));
        const auto wrapped_bounds = AnimObjGetBounds(animation.get(), 0, 0x101);
        REQUIRE(wrapped_bounds);
        REQUIRE(wrapped_bounds->minimum == bounds->minimum);
        REQUIRE(wrapped_bounds->maximum == bounds->maximum);
        animation->path = std::optional<KeyframeTrack>(std::in_place);
        PathAIAddPoint(*animation->path, glm::vec3(1, 2, 3));
        animation->paths[0].push_back(animation->path);
        animation->entries[0] = std::make_unique<AnimationMesh>();
        animation->entries[0]->source_file = std::string(300, 'x');
        animation->entries[0]->bitmap_directory = "Textures/";
        animation->entries[0]->meshes.resize(2); // Deferred frames have no instances yet.
        animation->meshes[0].push_back(std::make_unique<AnimationMesh>(*animation->entries[0]));
        auto copy = std::make_unique<Animation>(*animation);
        REQUIRE((copy->entries[0] != animation->entries[0]));
        REQUIRE((copy->meshes[0][0] != animation->meshes[0][0]));
        REQUIRE((copy->entries[0]->meshes.size() == 2));
        REQUIRE((copy->frame_bounds.data() != animation->frame_bounds.data()));
        REQUIRE(copy->path);
        copy->path->nodes[0].x = 9;
        REQUIRE(animation->path->nodes[0].x == 1);
        REQUIRE(copy->paths[0][0]);
        copy->paths[0][0]->nodes[0].z = 7;
        REQUIRE(animation->paths[0][0]->nodes[0].z == 3);
        animation.reset();
        REQUIRE((copy->entries[0]->source_file == std::string(300, 'x')));
        REQUIRE((copy->meshes[0][0]->bitmap_directory == "Textures/"));
        REQUIRE((copy->paths[0][0]->nodes[0].z == 7));
        const auto copy_bounds = AnimObjGetBounds(copy.get(), 0, 0);
        REQUIRE(copy_bounds);
        REQUIRE((copy_bounds.value().minimum.x == -500 && copy_bounds.value().maximum.x == 500));
        copy->path_lists = true;
        REQUIRE((!AnimObjGetBounds(copy.get(), 0, 2)));
    }
    REQUIRE((wiz8::remove_file(wiz8::user_path(filename))));
}

static void animated_mesh_cache_contracts()
{
    REQUIRE((g_animesh_cache_list.empty()));
    const int old_limit = g_animesh_cache_limit;
    const int old_bytes = g_animesh_cache_bytes;
    const int old_stamp = g_animesh_cache_stamp;
    REQUIRE_FALSE(GetAniMeshBounds(nullptr));
    {
        AnimationMesh mesh;
        mesh.flags.radius_loaded = true;
        mesh.bounds_minimum = {-1, -2, -3};
        mesh.bounds_maximum = {4, 5, 6};
        auto bounds = GetAniMeshBounds(&mesh);
        REQUIRE(bounds);
        REQUIRE(bounds.value().minimum == glm::vec3(-1, -2, -3));
        REQUIRE(bounds.value().maximum == glm::vec3(4, 5, 6));
        REQUIRE(mesh.last_used == old_stamp);
        REQUIRE(g_animesh_cache_stamp == old_stamp + 1);
        REQUIRE_FALSE(mesh.flags.loaded);
        bounds.value().minimum.x = -100;
        const auto snapshot = GetAniMeshBounds(&mesh);
        REQUIRE(snapshot);
        REQUIRE(snapshot.value().minimum.x == -1);
        REQUIRE(mesh.last_used == old_stamp + 1);
    }
    constexpr auto filename = "Data/empty-animation-mesh.dat";
    {
        auto file = wiz8::open_file(wiz8::asset_path(filename), wiz8::OpenMode::replace);
        const unsigned char frame_count = 0;
        file->write(&frame_count, 1);
    }
    {
        AnimationMesh mesh;
        mesh.source_file = wiz8::asset_path(filename);
        REQUIRE_FALSE(GetAniMeshBounds(&mesh));
        REQUIRE_FALSE(mesh.flags.radius_loaded);
        REQUIRE(g_animesh_cache_stamp == old_stamp + 2);
    }
    REQUIRE(wiz8::remove_file(wiz8::asset_path(filename)));
    g_animesh_cache_stamp = old_stamp;
    {
        AnimationMesh original;
        REQUIRE((original.meshes.empty() && original.source_file.empty()));
        REQUIRE((original.bounds_minimum == glm::vec3(0)));
        original.flags.loaded = true;
        original.flags.keep_loaded = true;
        original.flags.cache_protected = true;
        original.loaded_bytes = 32;
        g_animesh_cache_bytes += 32;
        g_animesh_cache_list.push_back(&original);
        {
            AnimationMesh copy(original);
            REQUIRE((g_animesh_cache_bytes == old_bytes + 64));
            REQUIRE((g_animesh_cache_list.size() == 2));
            g_animesh_cache_limit = old_bytes;
            EnforceAniMeshMemoryLimit(&original); // All candidates protected: no invalid erase.
            REQUIRE(copy.flags.loaded);
            copy.flags.cache_protected = false;
            EnforceAniMeshMemoryLimit(&original);
            REQUIRE((!copy.flags.loaded && original.flags.loaded));
            REQUIRE((g_animesh_cache_bytes == old_bytes + 32));
            REQUIRE((g_animesh_cache_list.size() == 1));
        }
    }
    REQUIRE((g_animesh_cache_bytes == old_bytes && g_animesh_cache_list.empty()));
    g_animesh_cache_limit = old_limit;
}

static void graphical_object_ownership_contracts()
{
    Missile original;
    original.visual.m_pAI = std::optional<KeyframeTrack>(std::in_place);
    PathAIAddPoint(*original.visual.m_pAI, glm::vec3(1, 2, 3));
    auto event = W8SoundEvent(W8_SOUND_EVENT_FRAME, 1, 2, 3, "step.wav", true);
    event.pitch = 3;
    original.visual.AddSoundEvent(std::move(event));
    for (int i = 0; i < 20; ++i) {
        original.visual.AddSoundEvent(
            W8SoundEvent(W8_SOUND_EVENT_CYCLE, i, 0, 0, "other.wav", false));
    }
    Missile copy(original);
    REQUIRE((copy.visual.m_pAI->nodes.data() != original.visual.m_pAI->nodes.data()));
    REQUIRE((copy.visual.m_pAI->nodes[0].y == 2));
    REQUIRE((copy.visual.sound_events.size() == original.visual.sound_events.size()));
    REQUIRE(copy.visual.sound_events.front().pitch == 3);
    REQUIRE((copy.visual.sound_events.front().wave_name == "step.wav"));
    REQUIRE((copy.visual.sound_events.front().looping &&
             copy.visual.sound_events.front().sound_handle == nullptr));
    original.visual.sound_events.clear();
    REQUIRE((copy.visual.sound_events.front().wave_name == "step.wav"));
    Missile missile;
    auto* ai = &missile.flight_ai.emplace();
    ai->speed_per_step = 5;
    Missile missile_copy(missile);
    auto* cloned = &*missile_copy.flight_ai;
    REQUIRE((cloned != ai && cloned->speed_per_step == 5 && cloned->missile == &missile_copy));
}

static void script_record_contracts()
{
    constexpr auto path = "data/script-records.txt";
    const std::string prefix(31, 'x');
    {
        auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
        const auto text = "* comment\r\n\r\nStart:\r\n  MOVE 10\r\nstart:\nWAIT 5\n" + prefix +
                          "first:\nEND\n" + prefix + "second:\nFINAL";
        file->write(text.data(), text.size());
    }
    Script script;
    REQUIRE((script.Load(path)));
    REQUIRE((script.lines.size() == 4 && script.labels.size() == 3));
    REQUIRE((script.lines[0].text == "  MOVE 10" && script.lines[3].text == "FINAL"));
    REQUIRE((script.FindLabelLine("START") == 0 && script.FindLabelLine(prefix + "first") == 2 &&
             script.FindLabelLine(prefix + "second") == 3));
    REQUIRE((script.FindLabelLine("missing") == -1 && script.FindLabelLine({}) == -1));
    REQUIRE((script.GetSourceLine(0) == 4 && script.GetSourceLine(3) == 10));
    REQUIRE((script.GetSourceLine(-1) == -1 && script.GetSourceLine(4) == -1));
    // Copies own their text and remain valid after the original is cleared.
    auto copy = script;
    script.Clear();
    script.Clear();
    REQUIRE((script.lines.empty() && script.labels.empty()));
    REQUIRE((copy.lines[0].text == "  MOVE 10" && copy.FindLabelLine("start") == 0));
    REQUIRE((script.Load(path) && script.lines.size() == 4));
    REQUIRE((wiz8::remove_file(wiz8::asset_path(path))));
    REQUIRE((!script.Load(path)));
}

static void encounter_table_contracts()
{
    wiz8::create_directory(wiz8::asset_path("Data/Databases"));
    constexpr auto path = "data/databases/encountertables.dbs";
    for (unsigned short version : {1, 2}) {
        {
            auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
            const int count = 1;
            const char name[256] = "Wilderness";
            W8EncounterTableDiskHeader header = {4, "Forest", 7, version, 2};
            const unsigned short species[] = {12, 34};
            const unsigned char rarity[] = {3, 70};
            const unsigned char time[] = {W8_ENCOUNTER_DAY, W8_ENCOUNTER_ANY_TIME};
            const unsigned char challenge[] = {5, 20};
            const W8EncounterScriptName scripts[] = {{"first.msf"}, {"second.msf"}};
            const unsigned char flags = 0x42;
            file->write(&count, sizeof(count));
            file->write(name, sizeof(name));
            file->write(&count, sizeof(count));
            file->write(&header, sizeof(header));
            file->write(species, sizeof(species));
            file->write(rarity, sizeof(rarity));
            file->write(time, sizeof(time));
            file->write(challenge, sizeof(challenge));
            file->write(scripts, sizeof(scripts));
            if (version == 2) file->write(&flags, sizeof(flags));
        }
        REQUIRE(InitializeEncounterTables());
        REQUIRE(g_encounter_names == std::vector<std::string>{"Wilderness"});
        REQUIRE(g_encounter_tables.size() == 1);
        const auto* table = GetEncounterTable(0);
        REQUIRE((table && !strcmp(table->name, "Forest") && table->category == 7));
        REQUIRE((table->version_two_flags == (version == 1 ? 0 : 0x42)));
        REQUIRE(table->entries.size() == 2);
        const auto& first = table->entries[0];
        const auto& second = table->entries[1];
        REQUIRE((first.species == 12 && first.rarity_class == 3 &&
                 first.time_condition == W8_ENCOUNTER_DAY && first.challenge_level == 5 &&
                 !strcmp(first.script.value, "first.msf")));
        REQUIRE((second.species == 34 && second.rarity_class == 70 &&
                 second.time_condition == W8_ENCOUNTER_ANY_TIME && second.challenge_level == 20 &&
                 !strcmp(second.script.value, "second.msf")));
        REQUIRE((!GetEncounterTable(-1) && !GetEncounterTable(1)));
    }
    // Failed loads must not publish partially populated parallel data.
    {
        auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
        const int count = 1;
        file->write(&count, sizeof(count));
    }
    REQUIRE(!InitializeEncounterTables());
    REQUIRE((g_encounter_tables.empty() && g_encounter_names.empty()));
    REQUIRE(g_encounter_tables_level == -1);
    UnloadEncounterTables();
    REQUIRE(wiz8::remove_file(wiz8::asset_path(path)));
}

static void current_game_character_contracts()
{
    W8Chunk chunks;
    REQUIRE(chunks.OpenWrite(wiz8::user_path("saves\\CurrentGame.SAV")));
    chunks.Close();
    W8Character first{}, second{}, loaded{};
    first.name = "Vi";
    first.uiExpLevel = 7;
    second.name = "Bob";
    second.uiExpLevel = 9;
    REQUIRE(SaveCharacterToCurrentGame("vi.CHR", 0, &first));
    REQUIRE(SaveCharacterToCurrentGame("bob.CHR", -1, &second));
    REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(first));
    first.uiExpLevel = 8;
    REQUIRE(SaveCharacterToCurrentGame("vi.CHR", 0, &first));
    REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(first));
    REQUIRE(LoadCharacterFromCurrentGame("bob.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(second));
    REQUIRE(
        (chunks.OpenRead(wiz8::user_path("saves\\CurrentGame.SAV")) && chunks.ChunkCount() == 2));
    int visited = 0;
    chunks.ForEachChunk([&](unsigned id) {
        REQUIRE(id == 0x52414843);
        REQUIRE(chunks.CurrentChunkExtent() == 64 + 4 + W8_CHARACTER_SERIALIZED_SIZE);
        ++visited;
    });
    REQUIRE(visited == 2);
    chunks.Close();
    REQUIRE(!SaveCharacterToCurrentGame(std::string(64, 'x').c_str(), 0, &first));
    REQUIRE(
        (chunks.OpenRead(wiz8::user_path("saves\\CurrentGame.SAV")) && chunks.ChunkCount() == 2));
    chunks.Close();

    // Fail every read, write, seek, flush, close and publication in turn.
    // Even after chunk headers have been patched, the published save is unchanged.
    std::size_t operations;
    {
        FileFaults trace;
        REQUIRE(SaveCharacterToCurrentGame("vi.CHR", 0, &first));
        operations = trace.operations;
    }
    W8Character replacement = first;
    replacement.uiExpLevel = 99;
    auto snapshot = [] {
        auto file = wiz8::open_file(wiz8::user_path("saves\\CurrentGame.SAV"));
        std::string bytes(file->size(), '\0');
        file->read_exact(bytes.data(), bytes.size());
        return bytes;
    };
    const auto before = snapshot();
    // Abandoning an unfinished RIFF must never publish over the old record.
    {
        wiz8::StagedFile staged(wiz8::user_path("saves\\CurrentGame.SAV"));
        W8Chunk unfinished;
        REQUIRE(unfinished.OpenWrite(staged.file()));
        REQUIRE_THROWS(
            unfinished.WriteChunk(0x52414843, [] { throw std::runtime_error("abandoned"); }));
        bool rejected = false;
        try {
            unfinished.Close();
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE(rejected);
    }
    REQUIRE(snapshot() == before);

    for (std::size_t operation = 1; operation <= operations; ++operation) {
        {
            FileFaults fault(operation);
            REQUIRE(!SaveCharacterToCurrentGame("vi.CHR", 0, &replacement));
            REQUIRE(fault.fired);
        }
        REQUIRE(snapshot() == before);
        REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(first));
    }
    for (unsigned level = 10; level < 20; ++level) {
        first.uiExpLevel = level;
        REQUIRE(SaveCharacterToCurrentGame("vi.CHR", 0, &first));
        REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(first));
        REQUIRE((chunks.OpenRead(wiz8::user_path("saves\\CurrentGame.SAV")) &&
                 chunks.ChunkCount() == 2));
        chunks.Close();
    }

    // A payload that reaches into the next chunk must leave the destination intact.
    REQUIRE(chunks.OpenWrite(wiz8::user_path("saves\\CurrentGame.SAV")));
    char name[64] = "vi.CHR";
    const unsigned size = W8_CHARACTER_SERIALIZED_SIZE;
    // Only the first byte of the declared payload is present.
    chunks.WriteChunk(0x52414843, [&] {
        chunks.WriteValue(name);
        chunks.WriteValue(size);
        chunks.WriteBytes(&first, 1);
    });
    chunks.WriteChunk(0x444150, [&] { wiz8::retail::write(chunks, second); });
    chunks.Close();
    loaded = second;
    REQUIRE(!LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(second));
    // A malformed newer payload does not hide the last complete character.
    REQUIRE(chunks.OpenWrite(wiz8::user_path("saves\\CurrentGame.SAV")));
    chunks.WriteChunk(0x52414843, [&] {
        chunks.WriteValue(name);
        chunks.WriteValue(size);
        wiz8::retail::write(chunks, first);
    });
    chunks.WriteChunk(0x52414843, [&] {
        chunks.WriteValue(name);
        chunks.WriteValue(size);
        chunks.WriteBytes(&replacement, 1);
    });
    chunks.Close();
    REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(first));
    REQUIRE(SaveCharacterToCurrentGame("vi.CHR", 0, &replacement));
    REQUIRE(LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(replacement));
}

static void save_file_contracts(const std::filesystem::path& user)
{
    using namespace std::chrono;
    const auto epoch = wiz8::file_time_from_sdl(0);
    REQUIRE(epoch.ticks() == 116444736000000000ULL);
    REQUIRE(wiz8::file_time_from_sdl(-1).ticks() == epoch.ticks() - 1);
    REQUIRE(wiz8::file_time_with_legacy_local_bias(epoch, 19800).ticks() ==
            epoch.ticks() + 19800ULL * 10000000);
    REQUIRE(wiz8::file_time_with_legacy_local_bias(epoch, -18000).ticks() ==
            epoch.ticks() - 18000ULL * 10000000);
    REQUIRE(wiz8::file_time_with_legacy_local_bias({}, -1).ticks() == UINT64_MAX - 9999999);

    const auto path = wiz8::user_path("saves/timestamp.SAV");
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    REQUIRE(file);
    SDL_PathInfo info{};
    REQUIRE(SDL_GetPathInfo(wiz8::path_to_utf8(user / "saves" / "timestamp.SAV").c_str(), &info));
    const int offset = wiz8::current_utc_offset_seconds();
    auto created = file->times().created;
    REQUIRE(created.ticks() == wiz8::file_time_with_legacy_local_bias(
                                   wiz8::file_time_from_sdl(info.create_time), offset)
                                   .ticks());

    // SaveGame's unchanged masks apply independently to the packed dwords.
    constexpr unsigned masks[] = {0x6b24e9f0u, 0xe77c28c1u};
    static_assert(sizeof(g_status.save_filetime_xor) == 8);
    unsigned words[] = {created.low ^ masks[0], created.high ^ masks[1]};
    file->write(words, sizeof(words));
    REQUIRE(file->tell() == 8);
    std::filesystem::last_write_time(user / "saves" / "timestamp.SAV",
                                     std::filesystem::file_time_type::clock::now() - hours(48));
    auto later = file->times().created;
    REQUIRE(later.ticks() == created.ticks());
    file.reset();
    std::ifstream encoded(user / "saves" / "timestamp.SAV", std::ios::binary);
    unsigned stored[2]{};
    encoded.read(reinterpret_cast<char*>(stored), sizeof(stored));
    REQUIRE((encoded.gcount() == 8 && stored[0] == words[0] && stored[1] == words[1]));
    encoded.close();
    REQUIRE(((stored[0] ^ masks[0]) == created.low && (stored[1] ^ masks[1]) == created.high));
    file = wiz8::open_file(path);
    const auto times = file->times();
    later = times.created;
    REQUIRE(SDL_GetPathInfo(wiz8::path_to_utf8(user / "saves" / "timestamp.SAV").c_str(), &info));
    // Reader fallback is current SDL create_time (POSIX ctime, not birth).
    REQUIRE(later.ticks() == wiz8::file_time_with_legacy_local_bias(
                                 wiz8::file_time_from_sdl(info.create_time), offset)
                                 .ticks());
    REQUIRE(times.modified.ticks() == wiz8::file_time_with_legacy_local_bias(
                                          wiz8::file_time_from_sdl(info.modify_time), offset)
                                          .ticks());
    file.reset();
    // Fixed epoch pair encodes the same eight bytes; no record layout changed.
    const unsigned epoch_masked[] = {epoch.low ^ masks[0], epoch.high ^ masks[1]};
    REQUIRE((epoch_masked[0] == 0xbe1a69f0u && epoch_masked[1] == 0xe6e1991fu));

    W8GlobalStatus status{};
    const auto now = std::filesystem::file_time_type::clock::from_sys(
        sys_days{year{2024} / January / 15} + hours{12});
    for (int slot = 1; slot <= 3; ++slot) {
        const auto name = "Quick " + std::to_string(slot) + ".SAV";
        W8Chunk save;
        REQUIRE(save.OpenWrite(user / "saves" / name));
        SaveGlobalStatus(&save, &status);
        save.Close();
        std::filesystem::last_write_time(user / "saves" / name, now - hours(slot == 1   ? 0
                                                                            : slot == 2 ? 4
                                                                                        : 2));
    }
    REQUIRE(SelectQuickSaveSlotForWrite() == "Quick 2");
    REQUIRE(FindStartupQuickSave() == "Quick 1");
    std::vector<std::unique_ptr<W8SaveSlot>> slots;
    REQUIRE(EnumerateSaveSlots(slots));
    REQUIRE(slots.size() == 3); // The unrelated invalid timestamp.SAV is excluded.
    REQUIRE(slots[0]->name == "Quick 1");
    REQUIRE(slots[1]->name == "Quick 3");
    REQUIRE(slots[2]->name == "Quick 2");
    for (const auto& slot : slots) {
        const auto metadata = wiz8::file_status(user / "saves" / (slot->name + ".SAV"));
        REQUIRE(metadata);
        REQUIRE(slot->write_time == metadata.value().info.modify_time);
        SDL_DateTime local{};
        REQUIRE(SDL_TimeToDateTime(slot->write_time, &local, true));
        REQUIRE(slot->timestamp.year == local.year);
        REQUIRE(slot->timestamp.day == local.day);
        REQUIRE(slot->timestamp.hour == local.hour);
        REQUIRE(slot->timestamp.utc_offset == local.utc_offset);
    }
    char oldest[] = "saves\\Quick 2.SAV", newest[] = "saves\\Quick 1.SAV";
    REQUIRE(wiz8::remove_file(wiz8::user_path(oldest)));
    REQUIRE(SelectQuickSaveSlotForWrite() == "Quick 2");
    REQUIRE(wiz8::remove_file(wiz8::user_path(newest)));
    // Missing Quick 1 must not leave the candidate timestamp uninitialized.
    REQUIRE(FindStartupQuickSave() == "Quick 3");
    char third[] = "saves\\Quick 3.SAV";
    REQUIRE(wiz8::remove_file(wiz8::user_path(third)));
    REQUIRE(!FindStartupQuickSave());
    std::ofstream fallback(user / "saves" / "Quick.SAV", std::ios::binary);
    write(fallback, 7u);
    fallback.close();
    REQUIRE(FindStartupQuickSave() == "Quick");

    const auto riff = wiz8::user_path("saves/riff.SAV");
    W8Chunk chunks;
    REQUIRE(chunks.OpenWrite(riff));
    chunks.WriteChunk(0x454d4954u, [&] { chunks.WriteValue(words); }); // TIME
    chunks.Close();
    REQUIRE((chunks.OpenRead(riff) && chunks.ChunkCount() == 1));
    chunks.ForEachChunk([&](unsigned id) {
        REQUIRE(id == 0x454d4954u);
        chunks.ReadValue(stored);
    });
    REQUIRE((stored[0] == words[0] && stored[1] == words[1]));
    chunks.Close();
}

static void global_status_record()
{
    W8GlobalStatus status{};
    auto& rows = status.party.rows;
    for (int slot = 0; slot < 8; ++slot) {
        rows[slot].fOccupied = true;
        rows[slot].npc_index = slot < 2 ? slot : -1;
        rows[slot].pending_action = W8_ACTION_ATTACK;
        rows[slot].committed_action = {W8_ACTION_CAST_SPELL, 20 + slot};
        rows[slot].committed_action.detail_block.spell.power_level = slot % 7 + 1;
        rows[slot].committed_action.target = {W8_TARGET_KIND_CHARACTER, slot, -1, -1, {}, {}};
        rows[slot].spell = {W8_ACTION_CAST_SPELL, 40 + slot};
        rows[slot].spell.detail_block.spell.power_level = 7;
        rows[slot].attack_mode[0] = 100 + slot;
        rows[slot].movement_fatigue = 1.25f + slot;
        rows[slot].party_order_index = slot;
        rows[slot].weapon_swap_pending = slot % 2;
    }
    rows[0].pending_action = W8_ACTION_USE_ITEM;
    rows[0].item_detail.item_use.item = {W8ItemStorage::Backpack, 0, 3};
    status.serialized = {0xa5, {0x5a, 0x17}};
    status.party.gold = 0x12345678;
    status.party.item_pool[0] = {.iItemNo = 12, .stack_count = 3};
    status.party.item_count = 1;
    status.uiTurnsElapsed = 0x87654321;
    status.pending_move_location.position = glm::vec3(1, 2, 3);
    for (const int level : {0, W8_LEVEL_COUNT - 1}) {
        status.level_progress[level] = {true, 0x1234, -7, 0x89abcdef, -9, 0x01020304, {}};
        std::fill(std::begin(status.level_progress[level].unknown_11),
                  std::end(status.level_progress[level].unknown_11), 0xa5);
    }
    const auto path = wiz8::user_path("gsta.bin");
    W8Chunk chunks;
    REQUIRE(chunks.OpenWrite(path));
    SaveGlobalStatus(&chunks, &status);
    chunks.Close();
    const auto status_image = wiz8::retail::encode(status);
    // Visit the single GSTA chunk.
    const auto read_status_chunk = [&](const auto& visit) {
        REQUIRE(chunks.OpenRead(path));
        int visited = 0;
        chunks.ForEachChunk([&](unsigned id) {
            REQUIRE(id == 0x41545347);
            visit();
            ++visited;
        });
        chunks.Close();
        REQUIRE(visited == 1);
    };
    read_status_chunk([&] {
        REQUIRE(chunks.ReadValue<unsigned>() == 0x49c2);
        unsigned char bytes[0x49c2];
        chunks.ReadValue(bytes);
        REQUIRE(!memcmp(bytes, status_image.data(), sizeof(bytes)));
        std::array<unsigned char, 0x21> expected_progress{
            1, 0x34, 0x12, 0xf9, 0xff, 0xef, 0xcd, 0xab, 0x89, 0xf7, 0xff, 0xff, 0xff, 4, 3, 2, 1};
        std::fill(expected_progress.begin() + 0x11, expected_progress.end(), 0xa5);
        for (const int level : {0, W8_LEVEL_COUNT - 1})
            REQUIRE(std::equal(expected_progress.begin(), expected_progress.end(),
                               bytes + 0x1a04 + level * 0x21));
        REQUIRE(bytes[0x242f] == 0xa5);
        REQUIRE(bytes[0x49be] == 0x5a);
        REQUIRE(bytes[0x49bf] == 0x17);
        std::array<int, 3> members;
        memcpy(members.data(), bytes + 0x0d, sizeof(members));
        REQUIRE((members == std::array<int, 3>{6, 2, 8}));
        unsigned gold, turns;
        memcpy(&gold, bytes + 0x19, sizeof(gold));
        memcpy(&turns, bytes + 0x19d8, sizeof(turns));
        REQUIRE((gold == 0x12345678 && turns == 0x87654321));
        float position[3];
        memcpy(position, bytes + 0x22a7, sizeof(position));
        REQUIRE((position[0] == 1 && position[1] == 2 && position[2] == 3));
        // Verify each record's prefix and stride independently of native sizeof.
        for (int slot = 0; slot < 8; ++slot) {
            REQUIRE(chunks.ReadValue<unsigned>() == 0x1862);
            chunks.ReadValue<std::array<std::byte, 0x1862>>();
        }
        for (int slot = 0; slot < 8; ++slot) {
            REQUIRE(chunks.ReadValue<unsigned>() == 0x106);
            const auto party_bytes = chunks.ReadValue<std::array<std::byte, 0x106>>();
            int attack_mode;
            float fatigue;
            std::memcpy(&attack_mode, party_bytes.data() + 5, 4);
            std::memcpy(&fatigue, party_bytes.data() + 0xf6, 4);
            REQUIRE((party_bytes[0] == std::byte{1} && attack_mode == 100 + slot));
            REQUIRE(fatigue == 1.25f + slot);
            REQUIRE(party_bytes[0x105] == std::byte(slot % 2));
        }
    });
    W8GlobalStatus restored{};
    auto& restored_rows = restored.party.rows;
    const auto live_name = g_status.party.characters[0].name;
    read_status_chunk([&] { restored = DecodeGameStatus(chunks); });
    REQUIRE(g_status.party.characters[0].name == live_name);
    auto independent = restored;
    independent.party.characters[0].name = "independent party";
    independent.party.rows[0].attack_mode[0] = -123;
    independent.party.item_pool[0].stack_count = 9;
    independent.party.gold = 1;
    REQUIRE(restored.party.characters[0].name != independent.party.characters[0].name);
    REQUIRE(restored.party.rows[0].attack_mode[0] == 100);
    REQUIRE(restored.party.gold == status.party.gold);
    REQUIRE(restored.party.item_pool[0].stack_count == 3);
    REQUIRE(restored.party.item_count == 1);
    const auto restored_image = wiz8::retail::encode(restored);
    REQUIRE(std::equal(status_image.begin() + 0x1a04, status_image.begin() + 0x2013,
                       restored_image.begin() + 0x1a04));
    for (auto offset : {0x242f, 0x49be, 0x49bf})
        REQUIRE(restored_image[offset] == status_image[offset]);
    for (int slot = 0; slot < 8; ++slot) {
        REQUIRE(restored_rows[slot].fOccupied);
        const auto& action = restored_rows[slot].committed_action;
        REQUIRE(action.kind == W8_ACTION_CAST_SPELL);
        REQUIRE(action.detail == 20 + slot);
        REQUIRE(action.detail_block.spell.power_level == slot % 7 + 1);
        REQUIRE(action.target.iType == W8_TARGET_KIND_CHARACTER);
        REQUIRE(action.target.iChar == slot);
        REQUIRE(restored_rows[slot].spell.detail == 40 + slot);
        REQUIRE(restored_rows[slot].spell.detail_block.spell.power_level == 7);
        REQUIRE(restored_rows[slot].attack_mode[0] == 100 + slot);
        REQUIRE(restored_rows[slot].movement_fatigue == 1.25f + slot);
        REQUIRE(restored_rows[slot].weapon_swap_pending == bool(slot % 2));
    }
    struct RestoreStatus {
        W8GlobalStatus previous = g_status;
        ~RestoreStatus() { g_status = std::move(previous); }
    } restore;
    const auto location = restored.party.rows[0].pending_action_detail.item_use.item;
    REQUIRE(restored.party.itemAt(location) == &restored.party.characters[0].backpack[3]);
    ActivateGameStatus(std::move(restored));
    REQUIRE(g_status.party.itemAt(location) == &g_status.party.characters[0].backpack[3]);
}

static void camera_angles()
{
    const struct RestoreCamera {
        PartyCamera previous = std::exchange(partyCamera(), PartyCamera{});
        ~RestoreCamera() { partyCamera() = previous; }
    } restore;
    CameraPose state{};
    state.orientation = cameraOrientation();
    REQUIRE(state.orientation.yaw == 0);
    REQUIRE(state.orientation.pitch == 0);
    state.position = {1, 2, 3};
    state.orientation.yaw = 0.5f;
    state.orientation.pitch = 0.25f;
    const float opaque_yaw = 123.0f, opaque_pitch = 456.0f;
    std::memcpy(state.retail.yaw.data() + 16, &opaque_yaw, 4);
    std::memcpy(state.retail.pitch.data() + 16, &opaque_pitch, 4);
    state.orientation = setCameraOrientation(state.orientation);
    REQUIRE((partyCamera().m_yaw == 0.5f && partyCamera().m_pitch == 0.25f));
    REQUIRE((state.orientation.yaw == 0.5f && state.orientation.pitch == 0.25f));
    const auto encoded = wiz8::retail::encode(state);
    const std::array<float, 15> expected{1, 2, 3, .25f, 0, 0, 0, 0, 456, .5f, 0, 0, 0, 0, 123};
    REQUIRE(std::ranges::equal(encoded, std::as_bytes(std::span(expected))));
    CameraPose decoded{};
    wiz8::retail::decode(encoded, decoded);
    REQUIRE(decoded.position == state.position);
    REQUIRE(decoded.orientation.yaw == state.orientation.yaw);
    REQUIRE(decoded.retail.yaw == state.retail.yaw);
    REQUIRE(decoded.orientation.pitch == state.orientation.pitch);
    REQUIRE(decoded.retail.pitch == state.retail.pitch);
}

#include <catch2/generators/catch_generators.hpp>

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct NpcGlobalGuard {
    std::vector<W8NpcDatabaseRecord> previous_records = std::exchange(g_npc_records, {});

    ~NpcGlobalGuard()
    {
        ReleaseNpcStates();
        g_npc_records = std::move(previous_records);
    }
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct CombatRecordGuard {
    std::vector<W8SpellRuntimeRecord> previous_spells = std::exchange(g_spell_records, {});
    W8Party previous_party = std::exchange(g_status.party, W8Party{});

    ~CombatRecordGuard()
    {
        g_spell_records = std::move(previous_spells);
        g_status.party = std::move(previous_party);
    }
};

struct SaveFixture {
    ScopedTemporaryDirectory directory{"wiz8-save-records"};
    std::filesystem::path root = directory.path;
    std::filesystem::path assets = root / "assets";
    std::filesystem::path user = root / "user";
    ScopedAssetRoots asset_roots;

    SaveFixture()
    {
        std::filesystem::create_directories(assets / "data");
        std::filesystem::create_directories(user / "data");
        std::filesystem::create_directories(user / "saves");
        asset_roots.configure({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user)});
    }
};

TEST_CASE("Camera angles retain their explicit save representation", "[save][records]")
{
    camera_angles();
}

TEST_CASE_METHOD(SaveFixture, "Quality files retain reserved slots and live option positions",
                 "[save][configuration]")
{
    InitializeRenderQuality();
    const struct Cleanup {
        ~Cleanup() { InitializeRenderQuality(); }
    } cleanup;
    SetRenderOption(W8_RENDER_OPTION_RESERVED_5, 1);
    SetRenderOption(W8_RENDER_OPTION_RESERVED_12, 1);
    EnableRenderOption(W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS);
    EnableRenderOption(W8_RENDER_OPTION_FOOTSTEP_SOUND);
    const auto path = wiz8::user_path("quality.bin");
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    REQUIRE(SaveRenderOptions(*file));
    file->close();
    file = wiz8::open_file(path);
    REQUIRE(file->size() == 21);
    int version;
    std::array<unsigned char, 17> options;
    file->read_exact(&version, sizeof(version));
    file->read_exact(options.data(), options.size());
    REQUIRE(version == 1);
    REQUIRE(options[5] == 0);
    REQUIRE(options[12] == 0);
    REQUIRE(options[14] == 1);
    REQUIRE(options[15] == 1);
}

TEST_CASE("Effect slots retain their packed disk representation", "[save][records]")
{
    const std::array<std::byte, 0x11> bytes = {
        std::byte{1}, std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{3},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{4}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{5}, std::byte{0}, std::byte{0}, std::byte{0}};
    W8EffectSlot slot{};
    wiz8::retail::decode<W8EffectSlot>(bytes, slot);
    REQUIRE(slot.active);
    REQUIRE(slot.effect_id == 2);
    REQUIRE(slot.amount == 3);
    REQUIRE(slot.percent == 4);
    REQUIRE(slot.duration == 5);
    REQUIRE(wiz8::retail::encode(slot) == bytes);
}

TEST_CASE("Unknown fact IDs never index native fact storage", "[save][records]")
{
    const auto before = std::to_array(g_fact_values);
    for (const auto raw : {-1, 1000, std::numeric_limits<std::int32_t>::min(),
                           std::numeric_limits<std::int32_t>::max()}) {
        const auto fact = static_cast<W8FactId>(raw);
        REQUIRE(GetFact(fact) == 0);
        REQUIRE(EvaluateFact(fact) == 0);
        SetFact(fact, 1, true);
    }
    REQUIRE(std::ranges::equal(g_fact_values, before));
}

TEST_CASE("Radius-effect updates accept empty and short effect arrays", "[effects]")
{
    std::array<W8EffectSlot, 6> effects{};
    REQUIRE_NOTHROW(TickRadiusBlastEffectSlots({}));
    REQUIRE_NOTHROW(TickRadiusBlastEffectSlots(effects));
}

TEST_CASE("Combat condition queries stay within both effect arrays", "[effects]")
{
    ScopedCombat combat;
    REQUIRE_FALSE(CombatHasCondition(0x3e));
    g_combat.state()->effect_slots[8].Activate(0x3e, 1, 1);
    REQUIRE(CombatHasCondition(0x3e));
    g_combat.state()->effect_slots[8].Reset();
    g_combat.state()->effect_slots0[5].Activate(0x3e, 1, 1);
    REQUIRE(CombatHasCondition(0x3e));
    g_combat.state()->effect_slots0[5].Reset();
    // In-flight missile state next to the short array is not an effect slot.
    g_combat.state()->missile_hit_result = W8_MISSILE_HIT_DEFLECTED;
    g_combat.state()->TargetHit.iMonsterID = 2;
    REQUIRE_FALSE(CombatHasCondition(0x3e));
}

TEST_CASE("Global status record", "[save][records]")
{
    global_status_record();
}

TEST_CASE_METHOD(SaveFixture, "NPC state versions and failure recovery", "[save][npc]")
{
    NpcGlobalGuard globals;
    const auto path = root / wiz8::path_from_utf8("npc-雪.bin");
    // All three database nodes are supplied by the serialized states below.
    g_npc_records.resize(3);
    for (unsigned char index = 0; index < 3; ++index) g_npc_records[index].kind = index;
    // A small word that a process-local handle table could have resolved.
    const unsigned int live_handle = 1;
    const unsigned char version =
        GENERATE(static_cast<unsigned char>(2), static_cast<unsigned char>(3));
    CAPTURE(version);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        write(out, version);
        write(out, 3u);
        const unsigned int markers[] = {0x12345678u, live_handle, 0};
        for (unsigned int i = 0; i < 3; ++i) {
            wiz8::retail::NpcState disk = {};
            disk.name_style = static_cast<unsigned char>(i);
            disk.character = markers[i];
            write(out, disk);
            if (markers[i]) {
                W8Character character = {};
                character.name.assign(1, static_cast<char>('A' + i));
                character.record_version = 2;
                if (version == 3) {
                    write(out, static_cast<unsigned int>(wiz8::retail::kCharacterBytes));
                }
                const auto image = wiz8::retail::encode(character);
                out.write(reinterpret_cast<const char*>(image.data()),
                          version == 2 ? 0x185c : image.size());
                REQUIRE(out.good());
            }
        }
        // Version >= 2 follows the states with an item count for each NPC.
        for (unsigned int i = 0; i < 3; ++i) {
            write(out, 0u);
        }
        write(out, 0xdecafbadU);
        out.close();
        auto file = wiz8::open_file(path);
        REQUIRE(file);
        ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadNpcStates(chunk); });
        REQUIRE(static_cast<int>(g_npc_states.size()) == 3);
        for (unsigned int i = 0; i < 3; ++i) {
            W8NpcState* npc = g_npc_states[i].get();
            REQUIRE(npc->record == &g_npc_records[i]);
            REQUIRE((npc->items.empty() && npc->script_file == 0));
            if (i < 2) {
                REQUIRE(npc->character.has_value());
                REQUIRE(npc->character.value().name[0] == static_cast<char>('A' + i));
            } else {
                REQUIRE(!npc->character);
            }
        }
        unsigned int trailer;
        file->read_exact(&trailer, sizeof(trailer));
        REQUIRE(trailer == 0xdecafbadU);
        file.reset();
        // Round-trip the supported NPCT version through its actual writer.
        file = wiz8::open_file(root / "roundtrip.npct", wiz8::OpenMode::replace);
        REQUIRE(SaveNpcStates(*file));
        file->close();
        file = wiz8::open_file(root / "roundtrip.npct");
        ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadNpcStates(chunk); });
        REQUIRE(g_npc_states.size() == 3);
        for (int index = 0; index < 3; ++index) {
            auto* npc = g_npc_states[index].get();
            REQUIRE((npc->record == &g_npc_records[index] && npc->items.empty()));
            if (index < 2)
                REQUIRE((npc->character && npc->character->name[0] == L'A' + index));
            else
                REQUIRE(!npc->character);
        }
        file.reset();
        const auto previous = NpcStatePointers();
        auto* previous_npc = previous[0];
        for (int malformed = 0; malformed < 4; ++malformed) {
            CAPTURE(version, malformed);
            std::ofstream bad(path, std::ios::binary | std::ios::trunc);
            write(bad, static_cast<unsigned char>(3));
            write(bad, malformed == 0 ? UINT32_MAX : 1u);
            wiz8::retail::NpcState disk{};
            disk.name_style = malformed == 1 ? 255 : 0;
            if (malformed != 2) write(bad, disk);
            if (malformed == 3) write(bad, UINT32_MAX); // Missing stock entries.
            bad.close();
            file = wiz8::open_file(path);
            bool rejected = false;
            try {
                ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadNpcStates(chunk); });
            } catch (const std::exception&) {
                rejected = true;
            }
            REQUIRE((rejected && NpcStatePointers() == previous));
            REQUIRE(g_npc_states[0].get() == previous_npc);
            REQUIRE(previous_npc->character.value().name[0] == L'A');
            file.reset();
        }
        REQUIRE((!GetNpcState(-1) && !GetNpcState(3)));
        ReleaseNpcBinding(3); // one past the end is not a live binding
        auto* kept = GetNpcState(1);
        auto* retired = g_npc_states[0].get();
        retired->binding_unavailable = true;
        REQUIRE(!GetNpcState(0));
        auto* replacement = CreateNpcRuntimeNode(0);
        REQUIRE((g_npc_states.size() == 3 && GetNpcState(0) == replacement));
        REQUIRE((replacement->partner_index == 0 && GetNpcState(1) == kept));
        REQUIRE((retired->binding_unavailable && retired->character.value().name[0] == L'A'));
        ReleaseNpcStates();
        ReleaseNpcStates();
        REQUIRE(g_npc_states.empty());
    }
}

TEST_CASE_METHOD(SaveFixture, "Character file records", "[save][records]")
{
    character_file_contracts(assets, user);
}

TEST_CASE_METHOD(SaveFixture, "Current-game character replacement", "[save][records]")
{
    current_game_character_contracts();
}

TEST_CASE_METHOD(SaveFixture, "NPC script records and truncated streams", "[save][npc]")
{
    // A small word that a process-local handle table could have resolved.
    const unsigned int live_handle = 1;
    const auto script_path = assets / "data" / "dialogue.nsf";
    const unsigned int marker = GENERATE_COPY(0x12345678u, live_handle, 0u);
    CAPTURE(marker);
    {
        std::ofstream out(script_path, std::ios::binary | std::ios::trunc);
        wiz8::retail::nsf::Header disk = {};
        disk.quote_count = 3;
        memcpy(&disk.name, &marker, sizeof(marker));
        write(out, disk);
        if (marker) {
            write(out, static_cast<unsigned short>(4));
            out.write("name", 4);
        }
        wiz8::retail::nsf::Quote quote = {};
        quote.entry_count = 1;
        const unsigned int present = marker ? marker : 0x87654321u;
        memcpy(&quote.subquotes, &present, sizeof(present));
        write(out, quote);
        write(out, static_cast<unsigned char>(2));
        write(out, static_cast<unsigned short>(4));
        const char16_t text[] = {u'é', u'中', 0xd83d, 0xde00};
        out.write(reinterpret_cast<const char*>(text), sizeof(text));
        write(out, static_cast<unsigned short>(0));
        wiz8::retail::nsf::Entry entry = {};
        entry.sub_entry_count = 2;
        write(out, entry);
        wiz8::retail::nsf::SubEntry sub = {};
        sub.operand = 11;
        memcpy(&sub.text, &present, sizeof(present));
        write(out, sub);
        write(out, static_cast<unsigned short>(4));
        out.write("line", 4);
        sub.text = 0;
        sub.operand = 22;
        write(out, sub);
        quote = {};
        memcpy(&quote.entries, &live_handle, sizeof(live_handle));
        write(out, quote);
        quote.entry_count = 1;
        write(out, quote);
        entry = {};
        entry.operand0 = 0x1234;
        memcpy(&entry.sub_entries, &live_handle, sizeof(live_handle));
        write(out, entry);
        REQUIRE(out.good());
        out.close();
        char filename[] = "data\\dialogue.nsf";
        auto file = LoadNpcScriptFile(filename);
        REQUIRE((file && file->quote_count == 3));
        REQUIRE((marker ? file->name == "name" : file->name.empty()));
        W8NpcScriptQuote* quotes = file->quotes.data();
        REQUIRE((quotes[0].subquote_count == 2 && quotes[0].subquotes[0] == "é中😀"));
        REQUIRE(quotes[0].subquotes[1].empty());
        W8NpcQuoteSubEntry* subs = quotes[0].entries[0].sub_entries.data();
        REQUIRE((subs[0].operand == 11 && subs[0].text == "line"));
        REQUIRE((subs[1].operand == 22 && subs[1].text.empty()));
        REQUIRE((quotes[1].entries.empty() && quotes[1].subquotes.empty()));
        REQUIRE(
            (quotes[2].entries[0].operand0 == 0x1234 && quotes[2].entries[0].sub_entries.empty()));
        file.reset();
        std::ifstream encoded(script_path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(encoded)), {});
        encoded.close();
        for (std::size_t end = 0; end < bytes.size(); ++end) {
            std::ofstream truncated(script_path, std::ios::binary | std::ios::trunc);
            truncated.write(bytes.data(), end);
            truncated.close();
            REQUIRE(LoadNpcScriptFile(filename) == nullptr);
        }
    }
}

TEST_CASE_METHOD(SaveFixture, "AI conditions and save slot naming", "[save][records]")
{
    CombatRecordGuard globals;
    // The AI used to decay the odd-offset condition array into unsigned int*.
    auto& character = g_status.party.characters[0];
    character = {};
    g_spell_records.resize(W8_SPELL_COUNT);
    g_spell_records[W8_SPELL_SLEEP].target_type = W8_TARGET_TYPE_ENEMY;
    W8CombatSlot target = {};
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = 0;
    REQUIRE(reinterpret_cast<uintptr_t>(character.uiCondition) % alignof(unsigned int) == 0);
    REQUIRE(MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    character.uiCondition[W8_CONDITION_ASLEEP] = 1;
    REQUIRE(!MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    REQUIRE(BuildLevelStatusPath(56) == "Levels\\Test\\DefaultLevel.STS");
    for (int index = 0; index < 10; ++index) {
        const auto name = index == 0 ? std::string("Ending") : "Ending" + std::to_string(index);
        auto file =
            wiz8::open_file(wiz8::user_path("saves/" + name + ".SAV"), wiz8::OpenMode::replace);
        file->close();
    }
    REQUIRE(FindFreeEndingSaveName() == std::optional<std::string>{"Ending10"});
    for (int index = 10; index < 20; ++index) {
        auto file =
            wiz8::open_file(wiz8::user_path("saves/Ending" + std::to_string(index) + ".SAV"),
                            wiz8::OpenMode::replace);
        file->close();
    }
    REQUIRE(FindFreeEndingSaveName() == std::optional<std::string>{"Ending20"});
    auto ending = wiz8::open_file(wiz8::user_path("saves/Ending20.SAV"), wiz8::OpenMode::replace);
    ending->close();
    REQUIRE(!FindFreeEndingSaveName());
}

TEST_CASE_METHOD(SaveFixture, "Save file timestamps", "[save][filesystem]")
{
    save_file_contracts(user);
}

TEST_CASE_METHOD(SaveFixture, "NPC dialogue transcripts", "[save][records]")
{
    transcript_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Keyword files", "[save][records]")
{
    keyword_file_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Unicode text files", "[save][records]")
{
    unicode_file_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Unicode database records", "[save][records]")
{
    unicode_database_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Encounter table records", "[save][records]")
{
    encounter_table_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Script record parsing", "[save][records]")
{
    script_record_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Path records", "[save][records]")
{
    path_record_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Animation records", "[save][records]")
{
    animation_record_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Graphical object ownership", "[save][records]")
{
    graphical_object_ownership_contracts();
}

TEST_CASE_METHOD(SaveFixture, "Animated mesh cache", "[save][records]")
{
    animated_mesh_cache_contracts();
}

TEST_CASE("Monster group opaque disk fields survive gameplay edits and old saves",
          "[save][records]")
{
    W8MonsterGroup group{};
    auto bytes = wiz8::retail::encode(group);
    REQUIRE(bytes[0x2b] == std::byte{3});
    bytes[0x2b] = std::byte{0xa5};
    bytes[offsetof(wiz8::retail::MonsterGroup, active_member_count)] = std::byte{7};
    for (std::size_t i = 0xd4; i < bytes.size(); ++i) bytes[i] = std::byte(i);
    wiz8::retail::decode<W8MonsterGroup>(bytes, group);
    REQUIRE(wiz8::retail::encode(group) == bytes);
    auto copy = group;
    copy.group_id = 17;
    auto edited = wiz8::retail::encode(copy);
    REQUIRE(edited[0x2b] == bytes[0x2b]);
    REQUIRE(std::equal(bytes.begin() + 0xd4, bytes.end(), edited.begin() + 0xd4));
    REQUIRE(group.group_id == 0);
    // A truncated historical record zero-extends absent opaque bytes.
    wiz8::retail::decode<W8MonsterGroup>(std::span(bytes).first(0xd4), group);
    auto old_saved = wiz8::retail::encode(group);
    REQUIRE(old_saved[0x2b] == bytes[0x2b]);
    REQUIRE(std::all_of(old_saved.begin() + 0xd4, old_saved.end(),
                        [](std::byte byte) { return byte == std::byte{}; }));
    const auto leader_offset = offsetof(wiz8::retail::MonsterGroup, leader_location_id);
    std::fill_n(bytes.begin() + leader_offset, 4, std::byte{0xcd});
    wiz8::retail::decode<W8MonsterGroup>(bytes, group);
    REQUIRE(group.leader_location_id == 0);
}

TEST_CASE("Level asset paths select preprocessed levels and sky fallbacks", "[levels][filesystem]")
{
    SaveFixture fixture;
    const auto touch = [&](const char* name) {
        auto path = fixture.assets / wiz8::normalize_asset_path(name);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        REQUIRE(file.good());
    };
    W8LevelInfo info;
    REQUIRE_FALSE(LevelBuildInfoByID(-1, &info));
    REQUIRE_FALSE(LevelBuildInfoByID(57, &info));
    REQUIRE_FALSE(LevelBuildInfoByID(47, &info));
    touch("Levels/Test/level1.oct");
    REQUIRE_FALSE(LevelBuildInfoByID(47, &info));
    touch("Levels/Test/level1.pvl");
    REQUIRE(LevelBuildInfoByID(47, &info));
    REQUIRE(wiz8::path_from_utf8(info.level_path) ==
            std::filesystem::path("Levels/Test/level1.LVL"));
    REQUIRE(info.sky_path.empty());
    touch("Levels/Test/sky1.LVL");
    REQUIRE(LevelBuildInfoByID(47, &info));
    REQUIRE(wiz8::path_from_utf8(info.sky_path) == std::filesystem::path("Levels/Test/sky1.LVL"));
    touch("Levels/Arnika/Arnika2.LVL");
    REQUIRE_FALSE(LevelBuildInfoByID(0, &info));
    touch("Levels/Test/DefaultSky.LVL");
    REQUIRE(LevelBuildInfoByID(0, &info));
    REQUIRE(wiz8::path_from_utf8(info.sky_path) ==
            std::filesystem::path("Levels/Test/DefaultSky.LVL"));
    touch("Levels/Arnika/RapaxSky.LVL");
    REQUIRE(LevelBuildInfoByID(0, &info));
    REQUIRE(wiz8::path_from_utf8(info.sky_path) ==
            std::filesystem::path("Levels/Arnika/RapaxSky.LVL"));
    touch("Levels/Bayjin/Bayjin1.LVL");
    REQUIRE(LevelBuildInfoByID(2, &info));
    REQUIRE(info.sky_path.empty());
}

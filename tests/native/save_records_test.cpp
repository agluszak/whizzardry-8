#include <limits>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_time.h>
#include <catch2/catch_test_macros.hpp>

#include "asset_paths.h"
#include "catch2/catch_message.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "retail_records.h"
#include "scoped_combat.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "unicode.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/checked_integer.h"
#include "wiz8/data/encounter_records.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/difficulty.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/layouts/encounter_tables.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/message_log.h"
#include "wiz8/npc_interaction_state.h"
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
#include "wiz8/save_codec.h"
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
#include "wiz8/combat/combat.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/script.h"
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

template<class T> static void write(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    REQUIRE(out.good());
}

static void character_file_contracts(const std::filesystem::path& assets,
                                     const std::filesystem::path& user)
{
    const ScopedGame game;
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
    CurrentGame().status.game_started = false;
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

    CurrentGame().status.game_started = true;
    CurrentGame().status.flags[0] = 0;
    path = BuildCharacterPath(npc.name, 0);

    CurrentGame().status.flags[0] = 1;
    path = BuildCharacterPath(npc.name, 0);

    path = BuildCharacterPath(npc.name, -1);

    CurrentGame().status.flags[0] = 0;
    CurrentGame().status.game_started = false;
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
    const auto item_records = ReadItemDatabase();
    REQUIRE(item_records.size() == 2);
    REQUIRE(std::string(item_records[0].display_name) == "Épée");
    REQUIRE(
        (std::string(item_records[1].display_name) == "药水😀" && item_records[1].value == 101));
    auto monsters =
        wiz8::open_file(wiz8::asset_path("data/databases/monsters.dbs"), wiz8::OpenMode::replace);
    monsters->write(&count, 4);
    for (unsigned i = 0; i < count; ++i) {
        W8MonsterRecord monster{};
        monster.name0 = i ? "怪物😀#2" : "Élan";
        monster.name1 = i ? "怪物们" : "Élans";
        const auto image = wiz8::retail::encode(monster);
        monsters->write(image.data(), image.size());
    }
    monsters->close();
    // Names drop the editor's suffix after '#'.
    const auto monster_records = ReadMonsterDatabase();
    REQUIRE(monster_records.size() == 2);
    REQUIRE((monster_records[0].name1 == "Élans" && monster_records[1].name0 == "怪物😀"));
    REQUIRE(monster_records[1].name1 == "怪物们");
}

static void unicode_file_contracts()
{
    ScopedGame game;
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
    const auto strings = ReadStringTable("Data/Strings/unicode.dat");
    REQUIRE((strings.size() == 1 && strings[0] == "é中😀 %s %hs %s"));

    const std::string message = "é 中 😀";
    wiz8::save::MessageBoxes boxes;
    auto& line = boxes[0].emplace_back();
    line.text = message;
    line.saved_remaining_ms = 87;
    line.entries.push_back({0, 1, 0, 5, W8_NOTICE_WORD_SELECTED, true});
    line.length = 6; // Prefix through 中, stored as three UTF-16 units.
    line.highlight_start = 3; // two UTF-16 units
    line.highlight_stop = 6; // three UTF-16 units
    const auto payload = wiz8::save::encode(boxes);
    // The fixed record follows the format and the first box's count. Its first word counts
    // UTF-16 units with the terminator, and its obsolete pointer word is always zero.
    REQUIRE(payload.size() == 8 + 0x24 + 3 * 4 + (6 + 1) * 2);
    REQUIRE(payload[8] == std::byte{7});
    for (std::size_t i = 8 + 0x18; i < 8 + 0x1c; ++i) REQUIRE(payload[i] == std::byte{});
    const auto decoded = wiz8::save::decode_message_boxes(payload, 9.0f);
    REQUIRE((decoded[0].size() == 1 && decoded[1].empty() && decoded[3].empty()));
    const auto& loaded = decoded[0][0];
    REQUIRE((loaded.text == message && loaded.saved_remaining_ms == 87));
    REQUIRE((loaded.entries.empty() && loaded.length == 6));
    REQUIRE((loaded.highlight_start == 3 && loaded.highlight_stop == 6));
    // A status record older than 1.1 had three boxes, so the payload holds three counts.
    auto three_boxes = payload;
    three_boxes.resize(three_boxes.size() - 4);
    REQUIRE(wiz8::save::decode_message_boxes(three_boxes, 1.0f)[0][0].text == message);
    REQUIRE_THROWS(wiz8::save::decode_message_boxes(three_boxes, 9.0f));
}

static void transcript_contracts()
{
    wiz8::save::DialogueTranscript transcript;
    transcript.keywords.push_back({"é中😀", W8_DIALOGUE_CATEGORY_PLACES});
    transcript.view = {W8_DIALOGUE_CATEGORY_MISC, true};
    const auto bytes = wiz8::save::encode(transcript);
    REQUIRE((bytes[0] == std::byte{2} && bytes[1] == std::byte{1}));
    REQUIRE(bytes[5] == std::byte{4}); // Four UTF-16 units, not nine UTF-8 bytes.
    REQUIRE((bytes[9] == std::byte{0xe9} && bytes[10] == std::byte{0}));
    REQUIRE((bytes[13] == std::byte{0x3d} && bytes[14] == std::byte{0xd8}));
    const auto decoded = wiz8::save::decode_dialogue_transcript(bytes);
    REQUIRE(decoded.keywords.size() == 1);
    REQUIRE(decoded.keywords[0].text == "é中😀");
    REQUIRE(decoded.keywords[0].category == W8_DIALOGUE_CATEGORY_PLACES);
    REQUIRE((decoded.view && decoded.view->category_filter == W8_DIALOGUE_CATEGORY_MISC &&
             decoded.view->sorted));
    // Version 1 has no view settings.
    auto version_1 = bytes;
    version_1[0] = std::byte{1};
    version_1.resize(version_1.size() - 2);
    REQUIRE(!wiz8::save::decode_dialogue_transcript(version_1).view);
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
    const auto none = [] {
        const auto keywords = ReadDialogueKeywords();
        return keywords.english.empty() && keywords.translated.empty();
    };
    write_keywords(english, "header\r\n01234567890  hello / world /\r\n01234567890bye/last");
    REQUIRE(none());
    write_keywords(translated, "header\n01234567890bonjour/monde/\n01234567890au revoir/final");
    {
        const ScopedGameData installed(GameData{.keywords = ReadDialogueKeywords()});
        const auto& first = Data().keywords.english;
        REQUIRE(first.size() == 2);
        REQUIRE(first[0] == std::vector<std::string>({"hello", "world"}));
        REQUIRE(first[1] == std::vector<std::string>({"bye", "last"}));
        REQUIRE(TranslateDialogueKeyword("BONJOUR") == "hello");
        REQUIRE(TranslateDialogueKeyword("unknown") == "unknown");
    }
    // Translated fields without an English counterpart remain unchanged.
    write_keywords(english, "header\n01234567890hello/");
    {
        const ScopedGameData installed(GameData{.keywords = ReadDialogueKeywords()});
        REQUIRE(TranslateDialogueKeyword("monde") == "monde");
        REQUIRE(TranslateDialogueKeyword("final") == "final");
    }
    write_keywords(english,
                   "header\n01234567890partial/row/\n01234567890" + std::string("\0bad", 4));
    REQUIRE(none());
    write_keywords(english, "header\n01234567890" + std::string(1000, 'x'));
    REQUIRE(none());
    REQUIRE(wiz8::remove_file(wiz8::asset_path(english)));
    REQUIRE(none());
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
        REQUIRE((AnimObjReadFromFile(&info, animation.get(), nullptr)));
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
        animation->entries[0]->frames.resize(2);
        animation->meshes[0].push_back(std::make_unique<AnimationMesh>(*animation->entries[0]));
        auto copy = std::make_unique<Animation>(*animation);
        REQUIRE((copy->entries[0] != animation->entries[0]));
        REQUIRE((copy->meshes[0][0] != animation->meshes[0][0]));
        REQUIRE((copy->entries[0]->frames.size() == 2));
        REQUIRE((copy->frame_bounds.data() != animation->frame_bounds.data()));
        REQUIRE(copy->path);
        copy->path->nodes[0].x = 9;
        REQUIRE(animation->path->nodes[0].x == 1);
        REQUIRE(copy->paths[0][0]);
        copy->paths[0][0]->nodes[0].z = 7;
        REQUIRE(animation->paths[0][0]->nodes[0].z == 3);
        animation.reset();
        REQUIRE((copy->entries[0]->source_file == std::string(300, 'x')));
        REQUIRE((copy->meshes[0][0]->source_file == std::string(300, 'x')));
        REQUIRE((copy->paths[0][0]->nodes[0].z == 7));
        const auto copy_bounds = AnimObjGetBounds(copy.get(), 0, 0);
        REQUIRE(copy_bounds);
        REQUIRE((copy_bounds.value().minimum.x == -500 && copy_bounds.value().maximum.x == 500));
        copy->path_lists = true;
        REQUIRE((!AnimObjGetBounds(copy.get(), 0, 2)));
    }
    REQUIRE((wiz8::remove_file(wiz8::user_path(filename))));
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
        const auto tables = ReadEncounterTables();
        REQUIRE(tables.size() == 1);
        const auto& table = tables[0];
        REQUIRE(table.name == "Forest");
        REQUIRE(table.entries.size() == 2);
        const auto& first = table.entries[0];
        const auto& second = table.entries[1];
        REQUIRE((first.species == 12 && first.rarity_class == 3 &&
                 first.time_condition == W8_ENCOUNTER_DAY && first.challenge_level == 5 &&
                 first.script == "first.msf"));
        REQUIRE((second.species == 34 && second.rarity_class == 70 &&
                 second.time_condition == W8_ENCOUNTER_ANY_TIME && second.challenge_level == 20 &&
                 second.script == "second.msf"));
        const ScopedGameData installed(GameData{.encounter_tables = tables});
        REQUIRE((GetEncounterTable(0) == &Data().encounter_tables[0] && !GetEncounterTable(-1) &&
                 !GetEncounterTable(1)));
        REQUIRE(FindEncounterTableByName("forest") == 0);
    }
    {
        auto file = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::replace);
        const int count = 1;
        file->write(&count, sizeof(count));
    }
    REQUIRE_THROWS(ReadEncounterTables());
    REQUIRE(wiz8::remove_file(wiz8::asset_path(path)));
}

static void stored_character_contracts()
{
    const ScopedGame game;
    game.game.status.game_started = true;
    W8Character first{}, second{}, loaded{};
    first.name = "Vi";
    first.uiExpLevel = 7;
    second.name = "Bob";
    second.uiExpLevel = 9;
    // A started game keeps character files as its own stored characters.
    REQUIRE((SaveCharacter(&first, -1) && SaveCharacter(&second, -1)));
    first.uiExpLevel = 8;
    REQUIRE(SaveCharacter(&first, -1));
    REQUIRE(game.game.stored_characters.size() == 2);
    REQUIRE(LoadCharacter("VI.chr", &loaded, -1));
    REQUIRE(loaded.uiExpLevel == 8);
    REQUIRE((LoadCharacter("Bob.CHR", &loaded, -1) && loaded.uiExpLevel == 9));
    REQUIRE(!LoadCharacter("Nobody.CHR", &loaded, -1));
    W8Character unnamed = first;
    unnamed.name = std::string(60, 'x');
    REQUIRE(!SaveCharacter(&unnamed, -1));

    // A CHAR section: the file name in 64 bytes, then the size-prefixed record.
    const auto& stored = game.game.stored_characters.front();
    const auto payload = wiz8::save::encode(stored);
    REQUIRE(payload.size() == 64 + 4 + wiz8::retail::kCharacterBytes);
    const auto decoded = wiz8::save::decode_stored_character(payload);
    REQUIRE(decoded.file_name == "Vi.CHR");
    REQUIRE(wiz8::retail::encode(decoded.character) == wiz8::retail::encode(stored.character));
    // A record that reaches past its section, or a name without a terminator, is malformed.
    REQUIRE_THROWS(wiz8::save::decode_stored_character(std::span(payload).first(64 + 4 + 1)));
    auto unterminated = payload;
    std::ranges::fill(std::span(unterminated).first(64), std::byte{'x'});
    REQUIRE_THROWS(wiz8::save::decode_stored_character(unterminated));
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
    static_assert(sizeof(CurrentGame().status.retail.save_filetime_xor) == 8);
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

    const auto status_payload = wiz8::save::encode(wiz8::save::GameState{});
    const auto now = std::filesystem::file_time_type::clock::from_sys(
        sys_days{year{2024} / January / 15} + hours{12});
    for (int slot = 1; slot <= 3; ++slot) {
        const auto name = "Quick " + std::to_string(slot) + ".SAV";
        W8Chunk save;
        REQUIRE(save.OpenWrite(user / "saves" / name));
        save.WriteChunk(W8ChunkId("GSTA"),
                        [&] { save.WriteBytes(status_payload.data(), status_payload.size()); });
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

static std::uint32_t payload_word(std::span<const std::byte> payload, std::size_t offset)
{
    std::uint32_t word;
    std::memcpy(&word, payload.data() + offset, sizeof(word));
    return word;
}

// The GSTA payload: the status record, eight characters and eight party rows, each sized.
constexpr std::size_t kStatusRecordBytes = 0x49c2;
constexpr std::size_t kPartyRowBytes = 0x106;
constexpr std::size_t kFirstCharacterOffset = 4 + kStatusRecordBytes;
constexpr std::size_t kFirstRowOffset =
    kFirstCharacterOffset + 8 * (4 + wiz8::retail::kCharacterBytes);

static wiz8::save::GameState populated_game_state()
{
    wiz8::save::GameState saved{};
    auto& [status, party] = saved;
    auto& rows = party.rows;
    for (int slot = 0; slot < 8; ++slot) {
        party.members[slot].emplace().npc_index = slot < 2 ? slot : -1;
        rows[slot].executing.kind = W8_ACTION_ATTACK;
        rows[slot].committed = {W8_ACTION_CAST_SPELL, 20 + slot};
        rows[slot].committed.detail_block.spell.power_level = slot % 7 + 1;
        rows[slot].committed.target = {W8_TARGET_KIND_CHARACTER, slot, -1, -1, {}, {}};
        rows[slot].remembered.spell = {W8_ACTION_CAST_SPELL, 40 + slot};
        rows[slot].remembered.spell.detail_block.spell.power_level = 7;
        rows[slot].executing.attack_mode[0] = 100 + slot;
        party.members[slot]->movement_fatigue = 1.25f + slot;
        party.members[slot]->order_index = slot;
        rows[slot].weapon_swap_pending = slot % 2;
    }
    rows[0].executing.kind = W8_ACTION_USE_ITEM;
    rows[0].remembered.item_detail.item_use.item = {W8ItemStorage::Backpack, 0, 3};
    status.retail.save_version = 1.1f;
    status.retail.padding_242f = 0xa5;
    status.retail.padding_49be = {0x5a, 0x17};
    status.retail.unknown_2013.fill(0x3c);
    status.retail.save_filetime_xor = {0x01234567, 0x89abcdef};
    status.text_box_lines_used = {1, 2, 3, 4};
    status.text_box_lines_shown = {1, 1, 2, 3};
    status.monster_name_buffer = "Rattkin";
    status.flags[3] = 1;
    status.spell_usage[2].cast_count = 5;
    status.item_spell_usage[7].usable_cast_count = 9;
    status.wait_state = W8_GAME_WAIT_STATE_RESTING;
    status.world_clock = 0x80000003u;
    status.search_mode = true;
    party.gold = 0x12345678;
    party.item_pool[0] = {.iItemNo = 12, .stack_count = 3, .identified = true};
    party.item_count = 1;
    status.uiTurnsElapsed = 0x87654321;
    status.pending_move_location.position = glm::vec3(1, 2, 3);
    status.pending_move_location.orientation = {.yaw = 0.5f, .pitch = 0.25f};
    for (const int level : {0, W8_LEVEL_COUNT - 1}) {
        status.level_progress[level] = {true, 0x1234, -7, 0x89abcdef, -9, 0x01020304, {}};
        std::fill(std::begin(status.level_progress[level].unknown_11),
                  std::end(status.level_progress[level].unknown_11), 0xa5);
    }
    return saved;
}

static void global_status_record()
{
    const auto saved = populated_game_state();
    const auto payload = wiz8::save::encode(saved);
    // Verify each record's prefix and stride independently of native sizeof.
    REQUIRE(payload.size() == kFirstRowOffset + 8 * (4 + kPartyRowBytes));
    REQUIRE(payload_word(payload, 0) == kStatusRecordBytes);
    const auto bytes = std::span(payload).subspan(4, kStatusRecordBytes);
    const auto byte_at = [&](std::size_t offset) {
        return std::to_integer<unsigned>(bytes[offset]);
    };
    std::array<unsigned char, 0x21> expected_progress{
        1, 0x34, 0x12, 0xf9, 0xff, 0xef, 0xcd, 0xab, 0x89, 0xf7, 0xff, 0xff, 0xff, 4, 3, 2, 1};
    std::fill(expected_progress.begin() + 0x11, expected_progress.end(), 0xa5);
    for (const int level : {0, W8_LEVEL_COUNT - 1})
        for (std::size_t index = 0; index < expected_progress.size(); ++index)
            REQUIRE(byte_at(0x1a04 + level * 0x21 + index) == expected_progress[index]);
    REQUIRE(byte_at(0x242f) == 0xa5);
    REQUIRE(byte_at(0x49be) == 0x5a);
    REQUIRE(byte_at(0x49bf) == 0x17);
    std::array<int, 3> members;
    std::memcpy(members.data(), bytes.data() + 0x0d, sizeof(members));
    REQUIRE((members == std::array<int, 3>{6, 2, 8}));
    REQUIRE(payload_word(bytes, 0x19) == 0x12345678u);
    REQUIRE(payload_word(bytes, 0x18d8) == 0x80000003u);
    REQUIRE(payload_word(bytes, 0x19d8) == 0x87654321u);
    // The camera keeps the retail field order: position, then pitch, then yaw.
    float camera[15];
    std::memcpy(camera, bytes.data() + 0x22a7, sizeof(camera));
    REQUIRE((camera[0] == 1 && camera[1] == 2 && camera[2] == 3));
    REQUIRE((camera[3] == 0.25f && camera[9] == 0.5f));
    for (int slot = 0; slot < 8; ++slot)
        REQUIRE(payload_word(payload,
                             kFirstCharacterOffset + slot * (4 + wiz8::retail::kCharacterBytes)) ==
                wiz8::retail::kCharacterBytes);
    for (int slot = 0; slot < 8; ++slot) {
        const auto offset = kFirstRowOffset + slot * (4 + kPartyRowBytes);
        REQUIRE(payload_word(payload, offset) == kPartyRowBytes);
        const auto row = std::span(payload).subspan(offset + 4, kPartyRowBytes);
        float fatigue;
        std::memcpy(&fatigue, row.data() + 0xf6, sizeof(fatigue));
        REQUIRE((row[0] == std::byte{1} && payload_word(row, 5) == 100u + slot));
        REQUIRE(fatigue == 1.25f + slot);
        REQUIRE(row[0x105] == std::byte(slot % 2));
    }

    // decode(encode(x)) == x, and the decoded state encodes to the same bytes.
    const auto restored = wiz8::save::decode_game_state(payload);
    REQUIRE(restored.status == saved.status);
    REQUIRE(wiz8::save::encode(restored) == payload);
    REQUIRE(restored.party.gold == saved.party.gold);
    REQUIRE(restored.party.item_pool[0] == saved.party.item_pool[0]);
    REQUIRE(restored.party.item_count == 1);
    const auto& restored_rows = restored.party.rows;
    for (int slot = 0; slot < 8; ++slot) {
        REQUIRE(restored.party.members[slot].has_value());
        const auto& action = restored_rows[slot].committed;
        REQUIRE(action.kind == W8_ACTION_CAST_SPELL);
        REQUIRE(action.detail == 20 + slot);
        REQUIRE(action.detail_block.spell.power_level == slot % 7 + 1);
        REQUIRE(action.target.iType == W8_TARGET_KIND_CHARACTER);
        REQUIRE(action.target.iChar == slot);
        REQUIRE(restored_rows[slot].remembered.spell.detail == 40 + slot);
        REQUIRE(restored_rows[slot].remembered.spell.detail_block.spell.power_level == 7);
        REQUIRE(restored_rows[slot].executing.attack_mode[0] == 100 + slot);
        REQUIRE(restored.party.members[slot]->movement_fatigue == 1.25f + slot);
        REQUIRE(restored_rows[slot].weapon_swap_pending == bool(slot % 2));
    }
    const auto location = restored.party.rows[0].executing.detail_block.item_use.item;
    REQUIRE(restored.party.itemAt(location) == &restored.party.characters[0].backpack[3]);

    // Unidentified bytes and a fixture character survive decode and encode unchanged.
    auto edited = payload;
    constexpr std::pair<unsigned, unsigned> unknown_runs[] = {
        {0x187b, 0x55}, {0x1904, 0xd4},  {0x19dc, 0x28}, {0x243f, 5},
        {0x40c2, 0xc},  {0x47de, 0x194}, {0x497b, 4}};
    for (const auto& [offset, size] : unknown_runs)
        for (unsigned index = 0; index < size; ++index)
            edited[4 + offset + index] = std::byte(0x40 + index % 0x31);
    std::ifstream fixture(WIZ8_CHARACTER_FIXTURE, std::ios::binary);
    fixture.read(reinterpret_cast<char*>(edited.data() + kFirstCharacterOffset),
                 static_cast<std::streamsize>(4 + wiz8::retail::kCharacterBytes));
    REQUIRE(fixture.good());
    const auto decoded = wiz8::save::decode_game_state(edited);
    REQUIRE(decoded.party.characters[0].name == "vi");
    REQUIRE(wiz8::save::encode(decoded) == edited);

    // A payload cut inside a record is malformed.
    const auto truncated = std::span(payload).first(kFirstRowOffset + 4 + kPartyRowBytes - 1);
    REQUIRE_THROWS_AS(wiz8::save::decode_game_state(truncated), std::runtime_error);
}

static void level_status_header_record()
{
    wiz8::save::LevelStatusHeader header{.next_group_id = 7,
                                         .next_monster_location_id = 8,
                                         .next_world_item_id = 9,
                                         .next_trigger_id = 10,
                                         .turns_elapsed = 0x01020304};
    header.status_prefix.fill(0x11);
    header.status_suffix.fill(0x22);
    header.unknown_tail.fill(0x33);
    const auto payload = wiz8::save::encode(header);
    REQUIRE(payload.size() == 0x314);
    float version;
    std::memcpy(&version, payload.data(), sizeof(version));
    REQUIRE(version == 2.0f);
    REQUIRE(payload_word(payload, 4) == 7u);
    // The turn counter sits inside the status block, after its prefix.
    REQUIRE(payload_word(payload, 0x14 + 0xd4) == 0x01020304u);
    REQUIRE(wiz8::save::decode_level_status_header(payload) == header);
    auto other_version = payload;
    other_version[3] = std::byte{0};
    REQUIRE_THROWS_AS(wiz8::save::decode_level_status_header(other_version), std::runtime_error);
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

// Three NPC database records, each of the kind of its index.
static GameData ThreeNpcs()
{
    GameData data{.npcs = std::vector<W8NpcDatabaseRecord>(3)};
    for (unsigned char index = 0; index < 3; ++index) data.npcs[index].kind = index;
    return data;
}

struct NpcGlobalGuard {
    const ScopedGameData data{ThreeNpcs()};
    const ScopedGame game;
};

// The spell table, with Sleep aimed at enemies.
static GameData SleepSpell()
{
    GameData data{.spells = std::vector<W8SpellRuntimeRecord>(W8_SPELL_COUNT)};
    data.spells[W8_SPELL_SLEEP].target_type = W8_TARGET_TYPE_ENEMY;
    return data;
}

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct CombatRecordGuard {
    const ScopedGameData data{SleepSpell()};
    W8Party previous_party = std::exchange(CurrentGame().party, W8Party{});

    ~CombatRecordGuard() { CurrentGame().party = std::move(previous_party); }
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
    const ScopedGame game;
    const auto before = CurrentGame().story.facts;
    for (const auto raw : {-1, 1000, std::numeric_limits<std::int32_t>::min(),
                           std::numeric_limits<std::int32_t>::max()}) {
        const auto fact = static_cast<W8FactId>(raw);
        REQUIRE(GetFact(fact) == 0);
        REQUIRE(EvaluateFact(fact) == 0);
        SetFact(fact, 1, true);
    }
    REQUIRE(CurrentGame().story.facts == before);
}

TEST_CASE("Faction sections keep every byte", "[save][records]")
{
    // The relation matrix, then one fourteen-byte disposition record per faction.
    constexpr std::size_t relation_bytes = W8_FACTION_COUNT * W8_FACTION_COUNT, record_bytes = 14;
    std::vector<std::byte> bytes(relation_bytes + W8_FACTION_COUNT * record_bytes);
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(index * 7 + 3);
    for (std::size_t faction = 0; faction < W8_FACTION_COUNT; ++faction)
        bytes[relation_bytes + faction * record_bytes + 10] = static_cast<std::byte>(faction & 1);
    const auto factions = wiz8::save::decode_factions(bytes);
    const auto& mook = factions.states[W8_FACTION_MOOK];
    std::int32_t offense_count;
    std::memcpy(&offense_count, bytes.data() + relation_bytes + W8_FACTION_MOOK * record_bytes + 1,
                sizeof(offense_count));
    REQUIRE((mook.offense_count == offense_count && !mook.encountered));
    REQUIRE(factions.states[W8_FACTION_PARTY].encountered);
    REQUIRE(wiz8::save::encode(factions) == bytes);
    bytes.pop_back();
    REQUIRE_THROWS(wiz8::save::decode_factions(bytes));
}

TEST_CASE_METHOD(SaveFixture, "Ambient sound states read the records retail stored",
                 "[save][records]")
{
    // Retail counts the whole emitter list but skips its empty slots, so the
    // count can exceed the stored records. Names match without regard to case.
    const auto path = user / "saves" / "ambient.SAV";
    W8Chunk save;
    REQUIRE(save.OpenWrite(path));
    save.WriteChunk(W8ChunkId("AMBS"), [&] {
        save.WriteValue(std::uint8_t{1});
        save.WriteValue(std::uint32_t{3});
        std::array<char, 128> name{};
        std::ranges::copy(std::string_view("soundWaterfall01"), name.begin());
        save.WriteValue(name);
        save.WriteValue(std::uint8_t{1});
    });
    save.Close();
    REQUIRE(save.OpenRead(path));
    std::vector<W8AmbientSoundState> states;
    save.ForEachChunk([&](unsigned) { states = ReadAmbientSoundStates(save); });
    save.Close();
    REQUIRE(states.size() == 1);
    REQUIRE((states[0].name == "soundWaterfall01" && states[0].stopped));

    World world;
    world.audio.emitters.push_back(std::make_unique<W8AmbientSound>());
    world.audio.emitters[0]->name = "SOUNDWATERFALL01";
    ApplyAmbientSoundStates(world, states);
    REQUIRE(world.audio.emitters[0]->stopped);
}

TEST_CASE("Monster-control sections keep the retail record", "[save][records]")
{
    W8SpellEffectEntry effect{};
    effect.kind = static_cast<W8SpellId>(0x26);
    effect.turns_remaining = 5;
    effect.sustained = true;
    effect.definition.power_level = 3;
    effect.monster_ids = {4, 5}; // Rebuilt while the effect runs, not saved.
    const auto payload = wiz8::save::encode(effect);
    // Spell, turns, two sources and two targets, four flags and the definition.
    REQUIRE(payload.size() == 4 + 4 + 2 * (0x34 + 0x20) + 4 + 0x30);
    const auto decoded = wiz8::save::decode_monster_control(payload);
    REQUIRE((decoded.kind == effect.kind && decoded.turns_remaining == 5 && decoded.sustained &&
             !decoded.recast && decoded.definition.power_level == 3 &&
             decoded.monster_ids.empty()));
    REQUIRE(wiz8::save::encode(decoded) == payload);
    REQUIRE_THROWS(
        wiz8::save::decode_monster_control(std::span(payload).first(payload.size() - 1)));
}

TEST_CASE("Settings records keep the retail layout and unnamed bytes", "[save][configuration]")
{
    W8GameSettings settings{};
    settings.tooltips_enabled = true;
    settings.difficulty = W8_DIFFICULTY_EXPERT;
    settings.music_volume = 77;
    settings.intro_seen = true;
    settings.unknown_051[0] = 0x5a;
    const auto record = EncodeGameSettings(settings);
    REQUIRE(record.size() == 0xa4);
    REQUIRE((record[0x0c] == std::byte{1} && record[0x0d] == std::byte{W8_DIFFICULTY_EXPERT} &&
             record[0x2f] == std::byte{77} && record[0x46] == std::byte{1} &&
             record[0x51] == std::byte{0x5a}));
    const auto decoded = DecodeGameSettings(record);
    REQUIRE((decoded.difficulty == W8_DIFFICULTY_EXPERT && decoded.music_volume == 77 &&
             decoded.intro_seen && decoded.unknown_051[0] == 0x5a));
    REQUIRE(EncodeGameSettings(decoded) == record);
    REQUIRE_THROWS(DecodeGameSettings(std::span(record).first(0xa3)));
}

TEST_CASE("Radius-effect updates accept empty and short effect arrays", "[effects]")
{
    const ScopedGame game;
    std::array<W8EffectSlot, 6> effects{};
    REQUIRE_NOTHROW(TickRadiusBlastEffectSlots({}));
    REQUIRE_NOTHROW(TickRadiusBlastEffectSlots(effects));
}

TEST_CASE("Combat condition queries stay within both effect arrays", "[effects]")
{
    const ScopedGame game;
    ScopedCombat combat{true};
    REQUIRE_FALSE(CombatHasCondition(0x3e));
    CurrentGame().combat->effect_slots[8].Activate(0x3e, 1, 1);
    REQUIRE(CombatHasCondition(0x3e));
    CurrentGame().combat->effect_slots[8].Reset();
    CurrentGame().combat->effect_slots0[5].Activate(0x3e, 1, 1);
    REQUIRE(CombatHasCondition(0x3e));
    CurrentGame().combat->effect_slots0[5].Reset();
    // In-flight missile state next to the short array is not an effect slot.
    CurrentGame().combat->missile_hit_result = W8_MISSILE_HIT_DEFLECTED;
    CurrentGame().combat->TargetHit.iMonsterID = 2;
    REQUIRE_FALSE(CombatHasCondition(0x3e));
}

TEST_CASE("Retail flag bytes decode to bool by value", "[save][records]")
{
    // Retail writers stored flags as arbitrary nonzero bytes; copying such a
    // byte into a bool is undefined, so it must decode as "nonzero".
    std::array<std::byte, sizeof(wiz8::retail::NpcState)> image{};
    image[offsetof(wiz8::retail::NpcState, has_monster)] = std::byte{2};
    W8NpcState npc{};
    wiz8::retail::decode<W8NpcState>(image, npc);
    REQUIRE(npc.has_monster);
    REQUIRE_FALSE(npc.is_grouped);
    const auto encoded = wiz8::retail::encode(npc);
    REQUIRE(encoded[offsetof(wiz8::retail::NpcState, has_monster)] == std::byte{1});
}

TEST_CASE("Global status record", "[save][records]")
{
    global_status_record();
}

TEST_CASE("Level status header record", "[save][records]")
{
    level_status_header_record();
}

template<class T> static void append_bytes(std::vector<std::byte>& bytes, const T& value)
{
    const auto view = std::as_bytes(std::span(&value, 1));
    bytes.insert(bytes.end(), view.begin(), view.end());
}

TEST_CASE_METHOD(SaveFixture, "NPC state versions and malformed sections", "[save][npc]")
{
    NpcGlobalGuard globals;
    // A small word that a process-local handle table could have resolved.
    const unsigned int live_handle = 1;
    const unsigned char version =
        GENERATE(static_cast<unsigned char>(2), static_cast<unsigned char>(3));
    CAPTURE(version);
    {
        std::vector<std::byte> bytes;
        append_bytes(bytes, version);
        append_bytes(bytes, 3u);
        const unsigned int markers[] = {0x12345678u, live_handle, 0};
        for (unsigned int i = 0; i < 3; ++i) {
            wiz8::retail::NpcState disk = {};
            disk.name_style = static_cast<unsigned char>(i);
            disk.character = markers[i];
            append_bytes(bytes, disk);
            if (markers[i]) {
                W8Character character = {};
                character.name.assign(1, static_cast<char>('A' + i));
                character.record_version = 2;
                if (version == 3)
                    append_bytes(bytes, static_cast<unsigned int>(wiz8::retail::kCharacterBytes));
                const auto image = wiz8::retail::encode(character);
                bytes.insert(bytes.end(), image.begin(),
                             image.begin() + (version == 2 ? 0x185c : image.size()));
            }
        }
        // Version >= 2 follows the states with an item count for each NPC.
        for (unsigned int i = 0; i < 3; ++i) append_bytes(bytes, 0u);
        InstallNpcStates(wiz8::save::decode_npc_states(bytes, Data().npcs.size()));
        REQUIRE(static_cast<int>(CurrentGame().story.npcs.size()) == 3);
        for (unsigned int i = 0; i < 3; ++i) {
            W8NpcState* npc = CurrentGame().story.npcs[i].get();
            REQUIRE(npc->record == &Data().npcs[i]);
            REQUIRE((npc->items.empty() && npc->script_file == 0));
            if (i < 2) {
                REQUIRE(npc->character.has_value());
                REQUIRE(npc->character.value().name[0] == static_cast<char>('A' + i));
            } else {
                REQUIRE(!npc->character);
            }
        }
        // Round-trip the current version through its encoder.
        CurrentGame().story.npcs[1]->items.push_back(
            {.available_at = 7, .item = {}, .quantity = 2});
        InstallNpcStates(wiz8::save::decode_npc_states(wiz8::save::encode(CurrentGame().story.npcs),
                                                       Data().npcs.size()));
        REQUIRE(CurrentGame().story.npcs.size() == 3);
        for (int index = 0; index < 3; ++index) {
            auto* npc = CurrentGame().story.npcs[index].get();
            REQUIRE(npc->record == &Data().npcs[index]);
            if (index < 2)
                REQUIRE((npc->character && npc->character->name[0] == L'A' + index));
            else
                REQUIRE(!npc->character);
        }
        const auto& stock = CurrentGame().story.npcs[1]->items;
        REQUIRE((stock.size() == 1 && stock[0].available_at == 7 && stock[0].quantity == 2));
        for (int malformed = 0; malformed < 4; ++malformed) {
            CAPTURE(malformed);
            std::vector<std::byte> bad;
            append_bytes(bad, static_cast<unsigned char>(3));
            append_bytes(bad, malformed == 0 ? UINT32_MAX : 1u);
            wiz8::retail::NpcState disk{};
            disk.name_style = malformed == 1 ? 255 : 0;
            if (malformed != 2) append_bytes(bad, disk);
            if (malformed == 3) append_bytes(bad, UINT32_MAX); // Missing stock entries.
            REQUIRE_THROWS(wiz8::save::decode_npc_states(bad, Data().npcs.size()));
        }
        REQUIRE((!GetNpcState(-1) && !GetNpcState(3)));
        ReleaseNpcBinding(3); // one past the end is not a live binding
        auto* kept = GetNpcState(1);
        // Work still holding a released NPC shares it; reusing its slot leaves it intact.
        const auto retired = CurrentGame().story.npcs[0];
        retired->binding_unavailable = true;
        REQUIRE(!GetNpcState(0));
        auto* replacement = CreateNpcRuntimeNode(0);
        REQUIRE((CurrentGame().story.npcs.size() == 3 && GetNpcState(0) == replacement));
        REQUIRE((replacement->partner_index == 0 && GetNpcState(1) == kept));
        REQUIRE((retired->binding_unavailable && retired->character.value().name[0] == L'A'));
    }
}

TEST_CASE_METHOD(SaveFixture, "Character file records", "[save][records]")
{
    character_file_contracts(assets, user);
}

TEST_CASE("Stored characters and their CHAR sections", "[save][records]")
{
    stored_character_contracts();
}

TEST_CASE_METHOD(SaveFixture, "NPC script records and truncated streams", "[save][npc]")
{
    const ScopedGame game;
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
    const ScopedGame game;
    CombatRecordGuard globals;
    // The AI used to decay the odd-offset condition array into unsigned int*.
    auto& character = CurrentGame().party.characters[0];
    character = {};
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

TEST_CASE("Monster group opaque disk fields survive gameplay edits and old saves",
          "[save][records]")
{
    W8MonsterGroup group{};
    group.spawn_time = 0x80000003u;
    auto bytes = wiz8::retail::encode(group);
    REQUIRE(payload_word(bytes, offsetof(wiz8::retail::MonsterGroup, spawn_time)) == 0x80000003u);
    REQUIRE(bytes[0x2b] == std::byte{3});
    bytes[0x2b] = std::byte{0xa5};
    bytes[offsetof(wiz8::retail::MonsterGroup, active_member_count)] = std::byte{7};
    for (std::size_t i = 0xd4; i < bytes.size(); ++i) bytes[i] = std::byte(i);
    wiz8::retail::decode<W8MonsterGroup>(bytes, group);
    REQUIRE(group.spawn_time == 0x80000003u);
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

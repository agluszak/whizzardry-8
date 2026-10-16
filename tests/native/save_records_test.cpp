#include <SDL3/SDL_filesystem.h>
#include <catch2/catch_test_macros.hpp>

#include "asset_paths.h"
#include "catch2/catch_message.hpp"
#include "conditions.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/encounter_tables.h"
#include "layouts/gameplay_databases.h"
#include "layouts/item_tables.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "retail_records.h"
#include "unicode.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/stModelInstance.h" // IWYU pragma: keep - complete model type for unique_ptr destruction.
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/string_database.h"
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wiz8/file_time.h>
#include <wiz8/filesystem.h>

#include "file_faults.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/spell_ids.h"
#include "wiz8/xstatus.h"

extern std::vector<W8NpcState*> g_npc_states;
extern void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status);

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
        character.name = "改名";
        const auto edited = wiz8::retail::encode(character);
        REQUIRE(std::equal(bytes.begin() + 0x25, bytes.begin() + 0x69, edited.begin() + 0x25));
    }
    g_status.game_started = false;
    REQUIRE(VerifyDataSubdirs());
    for (const char* name : {"A", "Vi", "Sir Bob", "Ninechars", "é中😀", "中文名字测试"}) {
        W8Character character = {};
        character.name = name;
        character.uiExpLevel = 7;
        std::string path;
        path = BuildCharacterPath(name, -1);
        const std::string filename = std::string(name) + ".CHR";
        REQUIRE(SaveCharacter(&character, -1));
        REQUIRE(character.record_version == 1);
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
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(character));
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
            REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(character));
        }
        character.uiExpLevel = 8;
        REQUIRE(SaveCharacter(&character, -1));
        REQUIRE(LoadCharacter(filename.c_str(), &loaded, -1));
        REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(character));
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
    REQUIRE(gXStatus.uiItemsInDatabase == 2);
    REQUIRE(std::string(g_item_records[0].display_name) == "Épée");
    REQUIRE((std::string(g_item_records[1].display_name) == "药水😀" &&
             g_item_records[1].value == 101));
    DestroyItemDatabase();
    g_item_records = nullptr;
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
        const int length = bytes.size() / 2;
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
    const int length = bytes.size();
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
    line.entries.push_back({0, 1, 0, 5, W8_NOTICE_WORD_SELECTED, true});
    line.length = 6; // Prefix through 中, stored as three UTF-16 units.
    line.highlight_start = 3; // two UTF-16 units
    line.highlight_stop = 6; // three UTF-16 units
    file = wiz8::open_file(wiz8::user_path("saves/text.bin"), wiz8::OpenMode::replace);
    REQUIRE(SaveMessageStorage(*file));
    file->close();
    ReleaseMessageStorage();
    g_status.buffers.save_version = 9.0f;
    file = wiz8::open_file(wiz8::user_path("saves/text.bin"));
    // The fixed record starts after format and region count; its obsolete
    // pointer word must never contain a live allocation address.
    std::array<std::byte, 8 + 0x24> header{};
    file->read_exact(header.data(), header.size());
    for (std::size_t i = 8 + 0x18; i < 8 + 0x1c; ++i) REQUIRE(header[i] == std::byte{});
    file->seek(0, wiz8::SeekOrigin::begin);
    REQUIRE(LoadMessageStorage(*file));
    REQUIRE(std::string(g_message_storage[0][0].text) == message);
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
    entry->category = 2;
    state.dialogue_transcript.push_back(std::move(entry));
    state.dialogue_category_filter = 3;
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
    REQUIRE(LoadNpcDialogueTranscript(*file));
    REQUIRE(state.dialogue_transcript.size() == 1);
    REQUIRE(state.dialogue_transcript[0]->text == "é中😀");
    REQUIRE(state.dialogue_transcript[0]->category == 2);
    REQUIRE((state.dialogue_category_filter == 3 && state.transcript_sorted));
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
        W8PathAI* loaded = nullptr;
        REQUIRE((LoadPathAI(&loaded, file.get()) && loaded));
        std::unique_ptr<W8PathAI> path(loaded);
        REQUIRE((path->nodes.size() == 2 && path->rotations.size() == 2));
        REQUIRE((path->nodes[1].x == 500 && path->total_length == 500));
        REQUIRE((path->unknown_08 == 17 && path->rotations[0] == glm::mat3(1)));
        REQUIRE((path->scales.size() == (version == 2 ? 2 : 0)));
        if (version == 2) REQUIRE((path->scales[1].y == 3));
        path->timed = true;
        auto copy = std::make_unique<W8PathAI>(*path);
        auto restarted = path->Clone();
        REQUIRE((!static_cast<W8PathAI*>(restarted.get())->timed));
        REQUIRE((copy->timed && copy->nodes.data() != path->nodes.data()));
        REQUIRE((copy->rotations.data() != path->rotations.data()));
        if (version == 2) REQUIRE((copy->scales.data() != path->scales.data()));
        copy->nodes[0].x = 99;
        REQUIRE((path->nodes[0].x == 0));
        PathAIClearOwned(copy.get());
        PathAIClearOwned(copy.get());
        REQUIRE((copy->nodes.empty() && copy->rotations.empty() && copy->scales.empty()));
        glm::vec3 point(7, 8, 9);
        PathAIPosition(nullptr, &point);
        PathAIPosition(copy.get(), &point);
        REQUIRE(point == glm::vec3(7, 8, 9));
        copy->looping = true;
        REQUIRE((!PathAINextPoint(copy.get(), &point)));
        REQUIRE((PathAINextPoint(path.get(), &point) && point.x == 0));
        REQUIRE((PathAINextPoint(path.get(), &point) && point.x == 500));
        REQUIRE((!PathAINextPoint(path.get(), &point)));
        path->animated = true;
        path->point_index = 0;
        path->interpolation_fraction = 0.25f;
        PathAIPosition(path.get(), &point);
        REQUIRE((point.x == 125));
    }
    for (int count : {-1, 0, 1}) {
        {
            auto file = wiz8::open_file(wiz8::user_path(filename), wiz8::OpenMode::replace);
            write_header(*file, 2, count); // No point payload: count 1 is truncated.
        }
        auto file = wiz8::open_file(wiz8::user_path(filename));
        W8PathAI existing{};
        auto* output = &existing;
        REQUIRE((LoadPathAI(&output, file.get()) == (count == 0)));
        REQUIRE((output == (count == 0 ? nullptr : &existing)));
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
        W8ReadLevelInfo info{};
        info.hFile = file.get();
        auto animation = std::make_unique<W8AnimObj>();
        REQUIRE((AnimObjReadFromFile(&info, animation.get(), 0, nullptr, 0)));
        REQUIRE((animation->animation_playing == (flag != 0)));
        REQUIRE((animation->random_play == (flag != 0)));
        REQUIRE((AnimationIsRunning(animation.get()) == (flag != 0)));
        REQUIRE((animation->frame_bounds.size() == 2));
        const auto bounds = AnimObjGetBounds(animation.get(), 0, 1);
        REQUIRE(bounds);
        REQUIRE((bounds->minimum.y == -1000 && bounds->maximum.z == 1500));
        REQUIRE_FALSE(AnimObjGetBounds(nullptr, 0, 0));
        REQUIRE_FALSE(AnimObjGetBounds(animation.get(), -1, 1));
        REQUIRE_FALSE(AnimObjGetBounds(animation.get(), 3, 1));
        const auto wrapped_bounds = AnimObjGetBounds(animation.get(), 0, 0x101);
        REQUIRE(wrapped_bounds);
        REQUIRE(wrapped_bounds->minimum == bounds->minimum);
        REQUIRE(wrapped_bounds->maximum == bounds->maximum);
        animation->path = std::make_unique<W8PathAI>();
        PathAIAddPoint(*animation->path, glm::vec3(1, 2, 3));
        animation->paths[0].push_back(std::make_unique<W8PathAI>(*animation->path));
        animation->entries[0] = std::make_unique<W8AniMesh>();
        animation->entries[0]->filename = std::string(300, 'x');
        animation->entries[0]->bitmap_directory = "Textures/";
        animation->entries[0]->meshes.resize(2); // Deferred frames have no instances yet.
        animation->meshes[0].push_back(std::make_unique<W8AniMesh>(*animation->entries[0]));
        auto copy = std::make_unique<W8AnimObj>(*animation);
        REQUIRE((copy->entries[0] != animation->entries[0]));
        REQUIRE((copy->meshes[0][0] != animation->meshes[0][0]));
        REQUIRE((copy->entries[0]->meshes.size() == 2));
        REQUIRE((copy->frame_bounds.data() != animation->frame_bounds.data()));
        REQUIRE((copy->path != animation->path && copy->path->nodes[0].x == 1));
        REQUIRE((copy->paths[0][0] != animation->paths[0][0]));
        animation.reset();
        REQUIRE((copy->entries[0]->filename == std::string(300, 'x')));
        REQUIRE((copy->meshes[0][0]->bitmap_directory == "Textures/"));
        REQUIRE((copy->paths[0][0]->nodes[0].z == 3));
        const auto copy_bounds = AnimObjGetBounds(copy.get(), 0, 0);
        REQUIRE(copy_bounds);
        REQUIRE((copy_bounds->minimum.x == -500 && copy_bounds->maximum.x == 500));
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
        W8AniMesh mesh;
        mesh.flags.radius_loaded = true;
        mesh.bounds_minimum = {-1, -2, -3};
        mesh.bounds_maximum = {4, 5, 6};
        auto bounds = GetAniMeshBounds(&mesh);
        REQUIRE(bounds);
        REQUIRE(bounds->minimum == glm::vec3(-1, -2, -3));
        REQUIRE(bounds->maximum == glm::vec3(4, 5, 6));
        REQUIRE(mesh.last_used == old_stamp);
        REQUIRE(g_animesh_cache_stamp == old_stamp + 1);
        REQUIRE_FALSE(mesh.flags.loaded);
        bounds->minimum.x = -100;
        const auto snapshot = GetAniMeshBounds(&mesh);
        REQUIRE(snapshot);
        REQUIRE(snapshot->minimum.x == -1);
        REQUIRE(mesh.last_used == old_stamp + 1);
    }
    constexpr auto filename = "Data/empty-animation-mesh.dat";
    {
        auto file = wiz8::open_file(wiz8::asset_path(filename), wiz8::OpenMode::replace);
        const unsigned char frame_count = 0;
        file->write(&frame_count, 1);
    }
    {
        W8AniMesh mesh;
        mesh.filename = filename;
        REQUIRE_FALSE(GetAniMeshBounds(&mesh));
        REQUIRE_FALSE(mesh.flags.radius_loaded);
        REQUIRE(g_animesh_cache_stamp == old_stamp + 2);
    }
    REQUIRE(wiz8::remove_file(wiz8::asset_path(filename)));
    g_animesh_cache_stamp = old_stamp;
    {
        W8AniMesh original;
        REQUIRE((original.meshes.empty() && original.filename.empty()));
        REQUIRE((original.bounds_minimum == glm::vec3(0)));
        original.flags.loaded = true;
        original.flags.keep_loaded = true;
        original.flags.cache_protected = true;
        original.loaded_bytes = 32;
        g_animesh_cache_bytes += 32;
        g_animesh_cache_list.push_back(&original);
        {
            W8AniMesh copy(original);
            REQUIRE((g_animesh_cache_bytes == old_bytes + 64));
            REQUIRE((g_animesh_cache_list.size() == 2));
            g_animesh_cache_limit = old_bytes;
            EnforceAniMeshMemoryLimit(&original); // All candidates protected: no invalid erase.
            REQUIRE((copy.flags.loaded));
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
    W8GrObject original;
    original.m_pAI = std::make_unique<W8PathAI>();
    PathAIAddPoint(*static_cast<W8PathAI*>(original.m_pAI.get()), glm::vec3(1, 2, 3));
    auto event = std::make_unique<W8SoundEvent>(W8_SOUND_EVENT_FRAME, 1, 2, 3, "step.wav", true);
    auto* first = event.get();
    original.AddSoundEvent(std::move(event));
    for (int i = 0; i < 20; ++i) {
        original.AddSoundEvent(
            std::make_unique<W8SoundEvent>(W8_SOUND_EVENT_CYCLE, i, 0, 0, "other.wav", false));
    }
    REQUIRE((original.sound_events.front().get() == first));
    W8GrObject copy(original);
    REQUIRE((copy.m_pAI != original.m_pAI));
    REQUIRE((static_cast<W8PathAI*>(copy.m_pAI.get())->nodes[0].y == 2));
    REQUIRE((copy.sound_events.size() == original.sound_events.size()));
    REQUIRE((copy.sound_events.front().get() != first));
    REQUIRE((copy.sound_events.front()->wave_name == "step.wav"));
    REQUIRE((!copy.sound_events.front()->looping &&
             copy.sound_events.front()->sound_handle == nullptr));
    original.sound_events.clear();
    REQUIRE((copy.sound_events.front()->wave_name == "step.wav"));
    W8GrObject missile;
    auto* ai = new W8AIMissile{};
    REQUIRE((ai->kind == W8_AI_RECORD_MISSILE));
    ai->speed_per_step = 5;
    missile.m_pAI.reset(ai);
    W8GrObject missile_copy(missile);
    auto* cloned = static_cast<W8AIMissile*>(missile_copy.m_pAI.get());
    REQUIRE((cloned != ai && cloned->speed_per_step == 5 && !cloned->missile));
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
    stScript script;
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
    for (int index = 0; index < 2; ++index) {
        REQUIRE((chunks.OpenChunk(0, 0) && chunks.CurrentChunkId() == 0x52414843));
        REQUIRE(!chunks.CurrentChunkAtEnd());
        REQUIRE(chunks.CurrentChunkExtent() == 64 + 4 + W8_CHARACTER_SERIALIZED_SIZE);
        REQUIRE((chunks.SkipCurrentChunk() && chunks.ReleaseCurrentChunk()));
    }
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
        unfinished.OpenChunk(0x52414843, 0);
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
    REQUIRE(chunks.OpenChunk(0x52414843, 0));
    char name[64] = "vi.CHR";
    unsigned size = W8_CHARACTER_SERIALIZED_SIZE;
    chunks.Write(name, sizeof(name), nullptr);
    chunks.Write(&size, sizeof(size), nullptr);
    chunks.Write(&first, 1, nullptr);
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x444150, 0);
    wiz8::retail::write(chunks, second);
    chunks.ReleaseCurrentChunk();
    chunks.Close();
    loaded = second;
    REQUIRE(!LoadCharacterFromCurrentGame("vi.CHR", &loaded));
    REQUIRE(wiz8::retail::encode(loaded) == wiz8::retail::encode(second));
    // A malformed newer payload does not hide the last complete character.
    REQUIRE(chunks.OpenWrite(wiz8::user_path("saves\\CurrentGame.SAV")));
    chunks.OpenChunk(0x52414843, 0);
    chunks.Write(name, sizeof(name), nullptr);
    chunks.Write(&size, sizeof(size), nullptr);
    wiz8::retail::write(chunks, first);
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x52414843, 0);
    chunks.Write(name, sizeof(name), nullptr);
    chunks.Write(&size, sizeof(size), nullptr);
    chunks.Write(&replacement, 1, nullptr);
    chunks.ReleaseCurrentChunk();
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
    const auto utc = wiz8::file_time_to_utc(epoch);
    REQUIRE((utc.year == 1970 && utc.month == 1 && utc.day == 1));
    REQUIRE(wiz8::file_time_from_sdl(-1).ticks() == epoch.ticks() - 1);
    REQUIRE(wiz8::file_time_from_utc({1601, 1, 1, 0, 0, 0, 0, 1}).ticks() == 0);
    REQUIRE(wiz8::file_time_with_legacy_local_bias(epoch, 19800).ticks() ==
            epoch.ticks() + 19800ULL * 10000000);
    REQUIRE(wiz8::file_time_with_legacy_local_bias(epoch, -18000).ticks() ==
            epoch.ticks() - 18000ULL * 10000000);
    REQUIRE(wiz8::file_time_to_utc(wiz8::file_time_with_legacy_local_bias(epoch, -18000)).year ==
            1969);
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

    const auto now = std::filesystem::file_time_type::clock::now();
    for (int slot = 1; slot <= 3; ++slot) {
        const auto name = "Quick " + std::to_string(slot) + ".SAV";
        std::ofstream save(user / "saves" / name, std::ios::binary);
        write(save, slot);
        save.close();
        std::filesystem::last_write_time(user / "saves" / name, now - hours(slot == 1   ? 0
                                                                            : slot == 2 ? 4
                                                                                        : 2));
    }
    REQUIRE(SelectQuickSaveSlotForWrite() == "Quick 2");
    REQUIRE(FindStartupQuickSave() == "Quick 1");
    char oldest[] = "saves\\Quick 2.SAV", newest[] = "saves\\Quick 1.SAV";
    const auto oldest_status = wiz8::file_status(wiz8::user_path(oldest)),
               newest_status = wiz8::file_status(wiz8::user_path(newest));
    REQUIRE((oldest_status && newest_status));
    const auto oldest_time = wiz8::file_time_from_sdl(oldest_status->info.modify_time).ticks();
    const auto newest_time = wiz8::file_time_from_sdl(newest_status->info.modify_time).ticks();
    REQUIRE((oldest_time < newest_time && (newest_time - oldest_time) / 10000000 >= 3600));
    REQUIRE(!(newest_time < oldest_time));
    REQUIRE((newest_time - oldest_time) / 10000000 < 20000);
    wiz8::DiskFileTime high{0, 2}, low{UINT32_MAX, 1};
    REQUIRE(high.ticks() > low.ticks());
    const wiz8::DiskFileTime same_low{UINT32_MAX, 1};
    REQUIRE((low.ticks() < high.ticks() && low.ticks() == same_low.ticks()));
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
    std::uint32_t count = 0;

    const auto riff = wiz8::user_path("saves/riff.SAV");
    W8Chunk chunks;
    REQUIRE(chunks.OpenWrite(riff));
    REQUIRE(chunks.OpenChunk(0x454d4954u, 0)); // TIME
    REQUIRE((chunks.Write(words, sizeof(words), &count) && count == 8));
    REQUIRE(chunks.ReleaseCurrentChunk());
    chunks.Close();
    REQUIRE(chunks.OpenAppend(riff));
    REQUIRE(chunks.OpenChunk(0x5458454eu, 0)); // NEXT
    REQUIRE((chunks.Write("tail", 4, &count) && count == 4));
    REQUIRE(chunks.ReleaseCurrentChunk());
    chunks.Close();
    REQUIRE((chunks.OpenRead(riff) && chunks.ChunkCount() == 2));
    REQUIRE((chunks.OpenChunk(0, 0) && chunks.CurrentChunkId() == 0x454d4954u));
    REQUIRE((chunks.Read(stored, sizeof(stored), &count) && count == 8));
    REQUIRE((stored[0] == words[0] && stored[1] == words[1]));
    REQUIRE(chunks.ReleaseCurrentChunk());
    REQUIRE((chunks.OpenChunk(0, 0) && chunks.CurrentChunkId() == 0x5458454eu));
    char tail[4]{};
    REQUIRE((chunks.Read(tail, sizeof(tail), &count) && count == 4 && !memcmp(tail, "tail", 4)));
    REQUIRE(chunks.ReleaseCurrentChunk());
    chunks.Close();
}

static void global_status_record()
{
    W8Character characters[8] = {};
    W8PartySlotRow rows[8] = {};
    W8GlobalStatus status = {};
    status.buffers.Char = characters;
    status.buffers.XChar = rows;
    for (int slot = 0; slot < 8; ++slot) {
        rows[slot].fOccupied = true;
        rows[slot].pending_action = W8_ACTION_ATTACK;
        rows[slot].attack_mode[0] = 100 + slot;
        rows[slot].movement_fatigue = 1.25f + slot;
        rows[slot].party_order_index = slot;
        rows[slot].weapon_swap_pending = slot % 2;
    }
    status.party_gold = 0x12345678;
    status.uiTurnsElapsed = 0x87654321;
    status.pending_move_location.position = glm::vec3(1, 2, 3);
    const auto path = wiz8::user_path("gsta.bin");
    W8Chunk chunks;
    REQUIRE(chunks.OpenWrite(path));
    SaveGlobalStatus(&chunks, &status);
    chunks.Close();
    REQUIRE((chunks.OpenRead(path) && chunks.OpenChunk(0, 0)));
    REQUIRE(chunks.CurrentChunkId() == 0x41545347);
    unsigned size = 0, count = 0;
    REQUIRE((chunks.Read(&size, sizeof(size), &count) && count == sizeof(size) && size == 0x49c2));
    unsigned char bytes[0x49c2];
    REQUIRE((chunks.Read(bytes, sizeof(bytes), &count) && count == sizeof(bytes)));
    const auto status_image = wiz8::retail::encode(status);
    REQUIRE(!memcmp(bytes, status_image.data(), sizeof(bytes)));
    unsigned gold, turns;
    memcpy(&gold, bytes + 0x19, sizeof(gold));
    memcpy(&turns, bytes + 0x19d8, sizeof(turns));
    REQUIRE((gold == 0x12345678 && turns == 0x87654321));
    float position[3];
    memcpy(position, bytes + 0x22a7, sizeof(position));
    REQUIRE((position[0] == 1 && position[1] == 2 && position[2] == 3));
    // Verify each record's prefix and stride independently of native sizeof.
    for (int slot = 0; slot < 8; ++slot) {
        REQUIRE((chunks.Read(&size, 4, &count) && size == 0x1862));
        std::array<std::byte, 0x1862> character_bytes;
        REQUIRE(chunks.Read(character_bytes.data(), character_bytes.size(), &count));
    }
    for (int slot = 0; slot < 8; ++slot) {
        REQUIRE((chunks.Read(&size, 4, &count) && size == 0x106));
        std::array<std::byte, 0x106> party_bytes;
        REQUIRE(chunks.Read(party_bytes.data(), party_bytes.size(), &count));
        int attack_mode;
        float fatigue;
        std::memcpy(&attack_mode, party_bytes.data() + 5, 4);
        std::memcpy(&fatigue, party_bytes.data() + 0xf6, 4);
        REQUIRE((party_bytes[0] == std::byte{1} && attack_mode == 100 + slot));
        REQUIRE(fatigue == 1.25f + slot);
        REQUIRE(party_bytes[0x105] == std::byte(slot % 2));
    }
    chunks.ReleaseCurrentChunk();
    chunks.Close();
    W8Character restored_characters[8]{};
    W8PartySlotRow restored_rows[8]{};
    W8GlobalStatus restored{};
    restored.buffers.Char = restored_characters;
    restored.buffers.XChar = restored_rows;
    REQUIRE((chunks.OpenRead(path) && chunks.OpenChunk(0, 0)));
    LoadGameStatus(&chunks, &restored);
    REQUIRE(restored.party_gold == status.party_gold);
    for (int slot = 0; slot < 8; ++slot) {
        REQUIRE(restored_rows[slot].fOccupied);
        REQUIRE(restored_rows[slot].attack_mode[0] == 100 + slot);
        REQUIRE(restored_rows[slot].movement_fatigue == 1.25f + slot);
        REQUIRE(restored_rows[slot].weapon_swap_pending == bool(slot % 2));
    }
    chunks.ReleaseCurrentChunk();
    chunks.Close();
}

static void camera_angles()
{
    GDCamera camera;
    const struct RestoreCamera {
        GDCamera& camera;
        GDCamera* previous;
        ~RestoreCamera()
        {
            g_gd_camera = previous;
            delete camera.m_manual_input_timer;
        }
    } restore{camera, g_gd_camera};
    g_gd_camera = &camera;
    W8WorldCameraState state{};
    GetCameraOrientation(state.yaw, state.pitch);
    for (unsigned i = 0; i < 6; ++i) REQUIRE((state.yaw[i] == 0 && state.pitch[i] == 0));
    state.position = {1, 2, 3};
    state.yaw[0] = 0.5f;
    state.pitch[0] = 0.25f;
    state.yaw[5] = 123.0f;
    state.pitch[5] = 456.0f;
    SetCameraOrientation(state.yaw, state.pitch, nullptr);
    REQUIRE((camera.m_yaw == 0.5f && camera.m_pitch == 0.25f));
    REQUIRE((state.yaw[0] == 0.5f && state.pitch[0] == 0.25f && state.yaw[5] == 123.0f &&
             state.pitch[5] == 456.0f));
    const auto encoded = wiz8::retail::encode(state);
    const std::array<float, 15> expected{1, 2, 3, .25f, 0, 0, 0, 0, 456, .5f, 0, 0, 0, 0, 123};
    REQUIRE(std::ranges::equal(encoded, std::as_bytes(std::span(expected))));
    W8WorldCameraState decoded{};
    wiz8::retail::decode(encoded, decoded);
    REQUIRE(decoded.position == state.position);
    REQUIRE(std::ranges::equal(decoded.yaw, state.yaw));
    REQUIRE(std::ranges::equal(decoded.pitch, state.pitch));
}

#include <catch2/generators/catch_generators.hpp>

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct NpcGlobalGuard {
    decltype(g_npc_records) previous_records = g_npc_records;
    decltype(gXStatus.uiNpcsInDatabase) previous_count = gXStatus.uiNpcsInDatabase;

    ~NpcGlobalGuard()
    {
        ReleaseNpcStates();
        g_npc_records = previous_records;
        gXStatus.uiNpcsInDatabase = previous_count;
    }
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct CombatRecordGuard {
    decltype(g_spell_records) previous_spells = g_spell_records;
    decltype(g_status.buffers.Char) previous_characters = g_status.buffers.Char;

    ~CombatRecordGuard()
    {
        g_spell_records = previous_spells;
        g_status.buffers.Char = previous_characters;
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

TEST_CASE("Global status record", "[save][records]")
{
    global_status_record();
}

TEST_CASE_METHOD(SaveFixture, "NPC state versions and failure recovery", "[save][npc]")
{
    NpcGlobalGuard globals;
    const auto path = root / wiz8::path_from_utf8("npc-雪.bin");
    W8NpcDatabaseRecord database[3] = {};
    for (unsigned char index = 0; index < 3; ++index) database[index].kind = index;
    g_npc_records = database;
    // All three database nodes are supplied by the serialized states below.
    gXStatus.uiNpcsInDatabase = 3;
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
        LoadNpcStates(*file);
        REQUIRE(static_cast<int>(g_npc_states.size()) == 3);
        for (unsigned int i = 0; i < 3; ++i) {
            W8NpcState* npc = g_npc_states[i];
            REQUIRE(npc->record == &database[i]);
            REQUIRE((npc->items.empty() && npc->script_file == 0));
            if (i < 2) {
                REQUIRE(npc->character.has_value());
                REQUIRE(npc->character->name[0] == static_cast<char>('A' + i));
            } else {
                REQUIRE(!npc->character);
            }
        }
        unsigned int trailer;
        file->read_exact(&trailer, sizeof(trailer));
        REQUIRE(trailer == 0xdecafbadU);
        file.reset();
        // A filesystem failure at any NPC read preserves the previous object graph.
        file = wiz8::open_file(path);
        std::size_t operations;
        {
            FileFaults trace;
            LoadNpcStates(*file);
            operations = trace.operations;
        }
        const auto stable_states = g_npc_states;
        auto* stable_npc = stable_states[0];
        for (std::size_t operation = 1; operation <= operations; ++operation) {
            CAPTURE(version, operation);
            file = wiz8::open_file(path);
            bool rejected = false;
            {
                FileFaults fault(operation);
                try {
                    LoadNpcStates(*file);
                } catch (const std::exception&) {
                    rejected = true;
                }
                REQUIRE(fault.fired);
            }
            REQUIRE((rejected && g_npc_states == stable_states && g_npc_states[0] == stable_npc));
            REQUIRE(stable_npc->character->name[0] == L'A');
        }
        // Round-trip the supported NPCT version through its actual writer.
        file = wiz8::open_file(root / "roundtrip.npct", wiz8::OpenMode::replace);
        REQUIRE(SaveNpcStates(*file));
        file->close();
        file = wiz8::open_file(root / "roundtrip.npct");
        LoadNpcStates(*file);
        REQUIRE(g_npc_states.size() == 3);
        for (int index = 0; index < 3; ++index) {
            auto* npc = g_npc_states[index];
            REQUIRE((npc->record == &database[index] && npc->items.empty()));
            if (index < 2)
                REQUIRE((npc->character && npc->character->name[0] == L'A' + index));
            else
                REQUIRE(!npc->character);
        }
        file.reset();
        const auto previous = g_npc_states;
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
                LoadNpcStates(*file);
            } catch (const std::exception&) {
                rejected = true;
            }
            REQUIRE((rejected && g_npc_states == previous));
            REQUIRE(g_npc_states[0] == previous_npc);
            REQUIRE(previous_npc->character->name[0] == L'A');
            file.reset();
        }
        REQUIRE((!GetNpcState(-1) && !GetNpcState(3)));
        ReleaseNpcBinding(3); // one past the end is not a live binding
        auto* kept = GetNpcState(1);
        auto* retired = g_npc_states[0];
        retired->binding_unavailable = true;
        REQUIRE(!GetNpcState(0));
        auto* replacement = CreateNpcRuntimeNode(0);
        REQUIRE((g_npc_states.size() == 3 && GetNpcState(0) == replacement));
        REQUIRE((replacement->partner_index == 0 && GetNpcState(1) == kept));
        REQUIRE((retired->binding_unavailable && retired->character->name[0] == L'A'));
        ReleaseNpcStates();
        ReleaseNpcStates();
        REQUIRE(g_npc_states.empty());
    }

    g_npc_records = nullptr;
    gXStatus.uiNpcsInDatabase = 0;
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
        W8NpcScriptFile* file = LoadNpcScriptFile(filename);
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
        ReleaseNpcScriptFile(file);
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
    alignas(16) W8Character character = {};
    static W8SpellRuntimeRecord spells[W8_SPELL_COUNT] = {};
    g_spell_records = spells;
    spells[W8_SPELL_SLEEP].target_type = W8_TARGET_TYPE_ENEMY;
    g_status.buffers.Char = &character;
    W8CombatSlot target = {};
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = 0;
    REQUIRE(reinterpret_cast<uintptr_t>(character.uiCondition) % alignof(unsigned int) == 0);
    REQUIRE(MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    character.uiCondition[W8_CONDITION_ASLEEP] = 1;
    REQUIRE(!MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    g_status.buffers.Char = nullptr;
    g_spell_records = nullptr;
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

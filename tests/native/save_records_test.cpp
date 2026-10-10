#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/string_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/retail_text_records.h"
/* Exercise recovered NPCT loading with retail addresses and native handles as
   on-disk presence markers. No installed game data or display is required. */

#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <wiz8/file_time.h>
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/monster_generators.h"
#include "wiz8/spell_ids.h"
#include "wiz8/xstatus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include "temporary_directory.h"

extern std::vector<W8NpcState*> g_npc_states;
extern void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status);

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                               \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

template <class T> static void write(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    CHECK(out.good());
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
        CHECK(file.good() && extent == wiz8::retail::kCharacterBytes);
        std::array<std::byte, wiz8::retail::kCharacterBytes> bytes;
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        CHECK(file.good());
        W8Character character{};
        wiz8::retail::decode<W8Character>(bytes, character);
        CHECK(character.name == "vi" && character.name_part_2 == "probe");
        CHECK(wiz8::retail::encode(character) == bytes);
        character.name = "改名";
        const auto edited = wiz8::retail::encode(character);
        CHECK(std::equal(bytes.begin() + 0x25, bytes.begin() + 0x69, edited.begin() + 0x25));
    }
    g_status.game_started = false;
    CHECK(VerifyDataSubdirs());
    for (const char* name : {"A", "Vi", "Sir Bob", "Ninechars", "é中😀", "中文名字测试"})
    {
        W8Character character = {};
        character.name = name;
        character.uiExpLevel = 7;
        std::string path;
        path = BuildCharacterPath(name, -1);
        const std::string filename = std::string(name) + ".CHR";
        CHECK(path == std::string("Saves\\Characters\\") + filename);
        CHECK(SaveCharacter(&character, -1, false, nullptr));
        CHECK(character.record_version == 1);
        const auto saved = user / "Saves" / "Characters" / filename;
        CHECK(std::filesystem::file_size(saved) == 4 + wiz8::retail::kCharacterBytes);
        CHECK(!std::filesystem::exists(assets / "Saves" / "Characters" / filename));
        std::ifstream encoded(saved, std::ios::binary);
        unsigned int size = 0;
        encoded.read(reinterpret_cast<char*>(&size), sizeof(size));
        CHECK(encoded.good() && size == wiz8::retail::kCharacterBytes);
        encoded.close();
        W8Character loaded = {};
        CHECK(LoadCharacter(filename.c_str(), &loaded, -1, false));
        CHECK(wiz8::retail::encode(loaded) == wiz8::retail::encode(character));
        character.uiExpLevel = 8;
        CHECK(SaveCharacter(&character, -1, false, nullptr));
        CHECK(LoadCharacter(filename.c_str(), &loaded, -1, false));
        CHECK(wiz8::retail::encode(loaded) == wiz8::retail::encode(character));
    }

    // I/O errors must reach the same failure continuation as an unsuccessful
    // current-game save. A directory at the destination cannot be opened as a file.
    W8Character blocked{};
    blocked.name = "Blocked";
    std::filesystem::create_directory(user / "Saves" / "Characters" / "Blocked.CHR");
    static unsigned failed_saves = 0;
    CHECK(!SaveCharacter(&blocked, -1, false, [] { ++failed_saves; }));
    CHECK(failed_saves == 1);

    W8Character npc = {};
    npc.name = "Vi";
    std::string path;
    path = BuildCharacterPath(npc.name, 0);
    CHECK(path == "Saves\\NPCs\\Vi.CHR");
    CHECK(SaveCharacter(&npc, 0, false, nullptr));
    W8Character loaded = {};
    CHECK(LoadCharacter("Vi.CHR", &loaded, 0, false));
    CHECK(wiz8::retail::encode(loaded) == wiz8::retail::encode(npc));

    g_status.game_started = true;
    g_status.flags[0] = 0;
    path = BuildCharacterPath(npc.name, 0);
    CHECK(path == "Saves\\NPCs\\Vi.CHR");
    g_status.flags[0] = 1;
    path = BuildCharacterPath(npc.name, 0);
    CHECK(path == "Vi.CHR");
    path = BuildCharacterPath(npc.name, -1);
    CHECK(path == "Vi.CHR");
    g_status.flags[0] = 0;
    g_status.game_started = false;
}

static void unicode_database_contracts()
{
    wiz8::create_directory("Data/Databases");
    const unsigned count = 2;
    auto items = wiz8::open_file("Data/Databases/Items.DBS", wiz8::OpenMode::replace);
    items->write(&count, 4);
    for (unsigned i = 0; i < count; ++i) {
        W8ItemDatabaseRecord item{};
        item.display_name = i ? "药水😀" : "Épée";
        item.value = 100 + i;
        const auto image = wiz8::retail::encode(item);
        items->write(image.data(), image.size());
    }
    items->close();
    CHECK(InitializeItemDatabase());
    CHECK(gXStatus.uiItemsInDatabase == 2);
    CHECK(std::string(g_item_records[0].display_name) == "Épée");
    CHECK(std::string(g_item_records[1].display_name) == "药水😀" && g_item_records[1].value == 101);
    DestroyItemDatabase();
    g_item_records = nullptr;
    auto monsters = wiz8::open_file("Data/Databases/Monsters.DBS", wiz8::OpenMode::replace);
    monsters->write(&count, 4);
    for (unsigned i = 0; i < count; ++i) {
        W8MonsterRecord monster{};
        monster.name0 = i ? "怪物😀" : "Élan";
        monster.name1 = i ? "怪物们" : "Élans";
        const auto image = wiz8::retail::encode(monster);
        monsters->write(image.data(), image.size());
    }
    monsters->close();
    W8MonsterRecord* records = nullptr;
    CHECK(LoadMonsterDatabase(&records));
    CHECK(records && std::string(records[1].name0) == "怪物😀");
    delete[] records;
    W8MonsterRecord single{};
    CHECK(LoadMonsterDatabaseRecord(1, &single));
    CHECK(std::string(single.name0) == "怪物😀");
    W8MonsterRecord range[2]{};
    CHECK(LoadMonsterDatabaseRange(0, 1, range));
    CHECK(std::string(range[0].name1) == "Élans" && std::string(range[1].name1) == "怪物们");
}

static void unicode_file_contracts()
{
    wiz8::create_directory("Data/Strings");
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
        auto file = wiz8::open_file("Data/Strings/unicode.msg", wiz8::OpenMode::replace);
        const unsigned char header[5] = {0, 0, 0, 0, static_cast<unsigned char>(encrypted)};
        file->write(header, sizeof(header));
        const auto bytes = retail_bytes(name, encrypted);
        const int length = bytes.size() / 2;
        const unsigned first = 123, second = 456;
        file->write(&first, 4); file->write(&second, 4); file->write(&length, 4);
        file->write(bytes.data(), bytes.size());
        const int offset = 5, count = 1, tail = 0;
        file->write(&offset, 4); file->write(&count, 4); file->write(&tail, 4);
        file->close();
        std::string output;
        unsigned metadata0 = 0, metadata1 = 0;
        CHECK((!(output = GetStringFromStringDatabase("Data/Strings/unicode.msg", 0, &metadata0, &metadata1)).empty()));
        CHECK(std::string(output) == name && metadata0 == 123 && metadata1 == 456);
        CHECK(!(!(output = GetStringFromStringDatabase("Data/Strings/unicode.msg", -1, nullptr, nullptr)).empty()));
        CHECK(output.empty());
        std::string complete;
        CHECK((complete = GetStringFromStringDatabase("Data/Strings/unicode.msg", 0, nullptr, nullptr)) == name);
    }
    auto file = wiz8::open_file("Data/Strings/unicode.dat", wiz8::OpenMode::replace);
    const int count = 1;
    const auto bytes = retail_bytes("é中😀 %S %hs %s", true);
    const int length = bytes.size();
    file->write(&count, 4); file->write(&length, 4); file->write(bytes.data(), bytes.size());
    file->close();
    LoadLocalizedStrings("Data/Strings/unicode.dat");
    CHECK(giStringListLen == 1 && std::string(g_localized_strings[0]) == "é中😀 %s %hs %s");
    ReleaseLocalizedStrings();
    CHECK(g_localized_strings.empty() && !giStringListLen);

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
    file = wiz8::open_file("Saves/text.bin", wiz8::OpenMode::replace);
    CHECK(SaveMessageStorage(file.get()));
    file->close();
    ReleaseMessageStorage();
    g_status.buffers.save_version = 9.0f;
    file = wiz8::open_file("Saves/text.bin");
    // The fixed record starts after format and region count; its obsolete
    // pointer word must never contain a live allocation address.
    std::array<std::byte, 8 + 0x24> header{};
    file->read_exact(header.data(), header.size());
    for (std::size_t i = 8 + 0x18; i < 8 + 0x1c; ++i)
        CHECK(header[i] == std::byte{});
    file->seek(0, wiz8::SeekOrigin::begin);
    CHECK(LoadMessageStorage(file.get()));
    CHECK(std::string(g_message_storage[0][0].text) == message);
    CHECK(g_message_storage[0][0].entries.empty());
    CHECK(g_message_storage[0][0].length == 6);
    CHECK(g_message_storage[0][0].highlight_start == 3 && g_message_storage[0][0].highlight_stop == 6);
    ReleaseMessageStorage();
}

static void transcript_contracts()
{
    W8NpcInteractionState state{};
    auto* previous = g_npc_interaction_state;
    g_npc_interaction_state = &state;
    auto* entry = new W8DialogueTranscriptRecord{};
    entry->text = "é中😀";
    entry->category = 2;
    state.dialogue_transcript.push_back(entry);
    state.dialogue_category_filter = 3;
    state.transcript_sorted = true;
    auto file = wiz8::open_file("Saves/transcript.bin", wiz8::OpenMode::replace);
    CHECK(SaveNpcDialogueTranscript(file.get()));
    file->close();
    file = wiz8::open_file("Saves/transcript.bin");
    std::array<std::byte, 20> bytes{};
    file->read_exact(bytes.data(), bytes.size());
    CHECK(bytes[0] == std::byte{2} && bytes[1] == std::byte{1});
    CHECK(bytes[5] == std::byte{4}); // Four UTF-16 units, not nine UTF-8 bytes.
    CHECK(bytes[9] == std::byte{0xe9} && bytes[10] == std::byte{0});
    CHECK(bytes[13] == std::byte{0x3d} && bytes[14] == std::byte{0xd8});
    file->seek(0, wiz8::SeekOrigin::begin);
    CHECK(LoadNpcDialogueTranscript(file.get()));
    CHECK(state.dialogue_transcript.size() == 1);
    CHECK(state.dialogue_transcript[0]->text == "é中😀");
    CHECK(state.dialogue_transcript[0]->category == 2);
    CHECK(state.dialogue_category_filter == 3 && state.transcript_sorted);
    ClearNpcDialogueTranscript();
    file->close();
    g_npc_interaction_state = previous;
}

static void keyword_file_contracts()
{
    wiz8::create_directory("Data/Strings");
    auto write_keywords = [](const char* path, const std::string& text) {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        file->write(text.data(), text.size());
        file->close();
    };
    const char* english = "Data/Strings/English_Keywords.txt";
    const char* translated = "Data/Strings/translated_Keywords.txt";
    write_keywords(english, "header\r\n01234567890  hello / world /\r\n01234567890bye/last");
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.size() == 0);
    write_keywords(translated, "header\n01234567890bonjour/monde/\n01234567890au revoir/final");
    ReloadKeywordLists();
    CHECK(g_keyword_lists_loaded && g_keyword_lists.size() == 2);
    const auto& first = g_keyword_lists[0];
    CHECK(first.size() == 2);
    CHECK(first[0] == std::vector<std::string>({"hello", "world"}));
    CHECK(first[1] == std::vector<std::string>({"bye", "last"}));
    CHECK(TranslateDialogueKeyword("BONJOUR") == "hello");
    CHECK(TranslateDialogueKeyword("unknown") == "unknown");
    // Translated fields without an English counterpart remain unchanged.
    write_keywords(english, "header\n01234567890hello/");
    ReloadKeywordLists();
    CHECK(TranslateDialogueKeyword("monde") == "monde");
    CHECK(TranslateDialogueKeyword("final") == "final");
    ClearKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.size() == 0);
    write_keywords(english, "header\n01234567890partial/row/\n01234567890" + std::string("\0bad", 4));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.size() == 0);
    write_keywords(english, "header\n01234567890" + std::string(1000, 'x'));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.size() == 0);
    CHECK(wiz8::remove_file(english));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.size() == 0);
}

static void path_record_contracts()
{
    constexpr auto filename = "Data/path-records.bin";
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
            auto file = wiz8::open_file(filename, wiz8::OpenMode::replace);
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
        auto file = wiz8::open_file(filename);
        W8PathAI* loaded = nullptr;
        CHECK(LoadPathAI(&loaded, file.get()) && loaded);
        std::unique_ptr<W8PathAI> path(loaded);
        CHECK(path->nodes.size() == 2 && path->rotations.size() == 2);
        CHECK(path->nodes[1].x == 500 && path->total_length == 500);
        CHECK(path->unknown_08 == 17 && path->rotations[0] == glm::mat3(1));
        CHECK(path->scales.size() == (version == 2 ? 2 : 0));
        if (version == 2) CHECK(path->scales[1].y == 3);
        path->timed = true;
        auto copy = std::make_unique<W8PathAI>(*path);
        auto restarted = path->Clone();
        CHECK(!static_cast<W8PathAI*>(restarted.get())->timed);
        CHECK(copy->timed && copy->nodes.data() != path->nodes.data());
        CHECK(copy->rotations.data() != path->rotations.data());
        if (version == 2) CHECK(copy->scales.data() != path->scales.data());
        copy->nodes[0].x = 99;
        CHECK(path->nodes[0].x == 0);
        PathAIClearOwned(copy.get());
        PathAIClearOwned(copy.get());
        CHECK(copy->nodes.empty() && copy->rotations.empty() && copy->scales.empty());
        srVector3T<float> point;
        copy->looping = true;
        CHECK(!PathAINextPoint(copy.get(), &point));
        CHECK(PathAINextPoint(path.get(), &point) && point.x == 0);
        CHECK(PathAINextPoint(path.get(), &point) && point.x == 500);
        CHECK(!PathAINextPoint(path.get(), &point));
        path->animated = true;
        path->point_index = 0;
        path->interpolation_fraction = 0.25f;
        PathAIPosition(path.get(), &point);
        CHECK(point.x == 125);
    }
    for (int count : {-1, 0, 1}) {
        {
            auto file = wiz8::open_file(filename, wiz8::OpenMode::replace);
            write_header(*file, 2, count); // No point payload: count 1 is truncated.
        }
        auto file = wiz8::open_file(filename);
        W8PathAI existing{};
        auto* output = &existing;
        CHECK(LoadPathAI(&output, file.get()) == (count == 0));
        CHECK(output == (count == 0 ? nullptr : &existing));
    }
    CHECK(wiz8::remove_file(filename));
}

static void animation_record_contracts()
{
    constexpr auto filename = "Data/animation-records.dat";
    for (unsigned char flag : {0, 255}) {
        {
            auto file = wiz8::open_file(filename, wiz8::OpenMode::replace);
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
        auto file = wiz8::open_file(filename);
        W8ReadLevelInfo info{};
        info.hFile = file.get();
        auto animation = std::make_unique<W8AnimObj>();
        CHECK(AnimObjReadFromFile(&info, animation.get(), 0, nullptr, 0));
        CHECK(animation->animation_playing == (flag != 0));
        CHECK(animation->random_play == (flag != 0));
        CHECK(AnimationIsRunning(animation.get()) == (flag != 0));
        CHECK(animation->frame_bounds.size() == 2);
        srVector3T<float> minimum, maximum;
        CHECK(AnimObjGetBounds(animation.get(), 0, 1, &minimum, &maximum));
        CHECK(minimum.y == -1000 && maximum.z == 1500);
        animation->path = std::make_unique<W8PathAI>();
        PathAIAddPoint(*animation->path, srVector3T<float>(1, 2, 3));
        animation->paths[0].push_back(std::make_unique<W8PathAI>(*animation->path));
        animation->entries[0] = std::make_unique<W8AniMesh>();
        animation->entries[0]->filename = std::string(300, 'x');
        animation->entries[0]->bitmap_directory = "Textures/";
        animation->entries[0]->meshes.resize(2); // Deferred frames have no instances yet.
        animation->meshes[0].push_back(std::make_unique<W8AniMesh>(*animation->entries[0]));
        auto copy = std::make_unique<W8AnimObj>(*animation);
        CHECK(copy->entries[0] != animation->entries[0]);
        CHECK(copy->meshes[0][0] != animation->meshes[0][0]);
        CHECK(copy->entries[0]->meshes.size() == 2);
        CHECK(copy->frame_bounds.data() != animation->frame_bounds.data());
        CHECK(copy->path != animation->path && copy->path->nodes[0].x == 1);
        CHECK(copy->paths[0][0] != animation->paths[0][0]);
        animation.reset();
        CHECK(copy->entries[0]->filename == std::string(300, 'x'));
        CHECK(copy->meshes[0][0]->bitmap_directory == "Textures/");
        CHECK(copy->paths[0][0]->nodes[0].z == 3);
        CHECK(AnimObjGetBounds(copy.get(), 0, 0, &minimum, &maximum));
        CHECK(minimum.x == -500 && maximum.x == 500);
        copy->path_lists = true;
        CHECK(!AnimObjGetBounds(copy.get(), 0, 2, &minimum, &maximum));
    }
    CHECK(wiz8::remove_file(filename));
}

static void animated_mesh_cache_contracts()
{
    CHECK(g_animesh_cache_list.empty());
    const int old_limit = g_animesh_cache_limit;
    const int old_bytes = g_animesh_cache_bytes;
    {
        W8AniMesh original;
        CHECK(original.meshes.empty() && original.filename.empty());
        CHECK(original.bounds_minimum == srVector3T<float>(0, 0, 0));
        original.flags.loaded = true;
        original.flags.keep_loaded = true;
        original.flags.cache_protected = true;
        original.loaded_bytes = 32;
        g_animesh_cache_bytes += 32;
        g_animesh_cache_list.push_back(&original);
        {
            W8AniMesh copy(original);
            CHECK(g_animesh_cache_bytes == old_bytes + 64);
            CHECK(g_animesh_cache_list.size() == 2);
            g_animesh_cache_limit = old_bytes;
            EnforceAniMeshMemoryLimit(&original); // All candidates protected: no invalid erase.
            CHECK(copy.flags.loaded);
            copy.flags.cache_protected = false;
            EnforceAniMeshMemoryLimit(&original);
            CHECK(!copy.flags.loaded && original.flags.loaded);
            CHECK(g_animesh_cache_bytes == old_bytes + 32);
            CHECK(g_animesh_cache_list.size() == 1);
        }
    }
    CHECK(g_animesh_cache_bytes == old_bytes && g_animesh_cache_list.empty());
    g_animesh_cache_limit = old_limit;
}

static void graphical_object_ownership_contracts()
{
    W8GrObject original;
    original.m_pAI = std::make_unique<W8PathAI>();
    PathAIAddPoint(*static_cast<W8PathAI*>(original.m_pAI.get()), srVector3T<float>(1, 2, 3));
    auto event = std::make_unique<W8SoundEvent>(W8_SOUND_EVENT_FRAME, 1, 2, 3, "step.wav", true);
    auto* first = event.get();
    original.AddSoundEvent(std::move(event));
    for (int i = 0; i < 20; ++i) {
        original.AddSoundEvent(std::make_unique<W8SoundEvent>(W8_SOUND_EVENT_CYCLE, i, 0, 0, "other.wav", false));
    }
    CHECK(original.sound_events.front().get() == first);
    W8GrObject copy(original);
    CHECK(copy.m_pAI != original.m_pAI);
    CHECK(static_cast<W8PathAI*>(copy.m_pAI.get())->nodes[0].y == 2);
    CHECK(copy.sound_events.size() == original.sound_events.size());
    CHECK(copy.sound_events.front().get() != first);
    CHECK(copy.sound_events.front()->wave_name == "step.wav");
    CHECK(!copy.sound_events.front()->looping && copy.sound_events.front()->sound_handle == -1);
    original.sound_events.clear();
    CHECK(copy.sound_events.front()->wave_name == "step.wav");
    W8GrObject missile;
    auto* ai = new W8AIMissile{};
    CHECK(ai->kind == W8_AI_RECORD_MISSILE);
    ai->speed_per_step = 5;
    missile.m_pAI.reset(ai);
    W8GrObject missile_copy(missile);
    auto* cloned = static_cast<W8AIMissile*>(missile_copy.m_pAI.get());
    CHECK(cloned != ai && cloned->speed_per_step == 5 && !cloned->missile);
}

static void script_record_contracts()
{
    constexpr auto path = "Data/script-records.txt";
    const std::string prefix(31, 'x');
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        const auto text = "* comment\r\n\r\nStart:\r\n  MOVE 10\r\nstart:\nWAIT 5\n" +
                          prefix + "first:\nEND\n" + prefix + "second:\nFINAL";
        file->write(text.data(), text.size());
    }
    stScript script;
    CHECK(script.Load(path));
    CHECK(script.lines.size() == 4 && script.labels.size() == 2);
    CHECK(script.lines[0].text == "  MOVE 10" && script.lines[3].text == "FINAL");
    CHECK(script.FindLabelLine("START") == 0 && script.FindLabelLine(prefix.c_str()) == 2);
    CHECK(script.FindLabelLine("missing") == -1 && script.FindLabelLine(nullptr) == -1);
    CHECK(script.GetSourceLine(0) == 4 && script.GetSourceLine(3) == 10);
    CHECK(script.GetSourceLine(-1) == -1 && script.GetSourceLine(4) == -1);
    // Copies own their text and remain valid after the original is cleared.
    auto copy = script;
    script.Clear();
    script.Clear();
    CHECK(script.lines.empty() && script.labels.empty());
    CHECK(copy.lines[0].text == "  MOVE 10" && copy.FindLabelLine("start") == 0);
    CHECK(script.Load(path) && script.lines.size() == 4);
    CHECK(wiz8::remove_file(path));
    CHECK(!script.Load(path));
}

static void encounter_table_contracts()
{
    wiz8::create_directory("Data/Databases");
    constexpr auto path = "Data/Databases/EncounterTables.dbs";
    for (unsigned short version : {1, 2}) {
        {
            auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
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
        CHECK(InitializeEncounterTables());
        CHECK(g_encounter_names == std::vector<std::string>{"Wilderness"});
        CHECK(g_encounter_tables.size() == 1);
        const auto* table = GetEncounterTable(0);
        CHECK(table && !strcmp(table->name, "Forest") && table->category == 7);
        CHECK(table->version_two_flags == (version == 1 ? 0 : 0x42));
        CHECK(table->entries.size() == 2);
        const auto& first = table->entries[0];
        const auto& second = table->entries[1];
        CHECK(first.species == 12 && first.rarity_class == 3 &&
              first.time_condition == W8_ENCOUNTER_DAY && first.challenge_level == 5 &&
              !strcmp(first.script.value, "first.msf"));
        CHECK(second.species == 34 && second.rarity_class == 70 &&
              second.time_condition == W8_ENCOUNTER_ANY_TIME && second.challenge_level == 20 &&
              !strcmp(second.script.value, "second.msf"));
        CHECK(!GetEncounterTable(-1) && !GetEncounterTable(1));
    }
    // Failed loads must not publish partially populated parallel data.
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        const int count = 1;
        file->write(&count, sizeof(count));
    }
    CHECK(!InitializeEncounterTables());
    CHECK(g_encounter_tables.empty() && g_encounter_names.empty());
    CHECK(g_encounter_tables_level == -1);
    UnloadEncounterTables();
    CHECK(wiz8::remove_file(path));
}

static void save_file_contracts(const std::filesystem::path& user)
{
    using namespace std::chrono;
    const auto epoch = wiz8::file_time_from_sdl(0);
    CHECK(epoch.ticks() == 116444736000000000ULL);
    const auto utc = wiz8::file_time_to_utc(epoch);
    CHECK(utc.year == 1970 && utc.month == 1 && utc.day == 1);
    CHECK(wiz8::file_time_from_sdl(-1).ticks() == epoch.ticks() - 1);
    CHECK(wiz8::file_time_from_utc({1601, 1, 1, 0, 0, 0, 0, 1}).ticks() == 0);
    CHECK(wiz8::file_time_with_legacy_local_bias(epoch, 19800).ticks() ==
          epoch.ticks() + 19800ULL * 10000000);
    CHECK(wiz8::file_time_with_legacy_local_bias(epoch, -18000).ticks() ==
          epoch.ticks() - 18000ULL * 10000000);
    CHECK(wiz8::file_time_to_utc(wiz8::file_time_with_legacy_local_bias(epoch, -18000)).year == 1969);
    CHECK(wiz8::file_time_with_legacy_local_bias({}, -1).ticks() == UINT64_MAX - 9999999);

    char path[] = "Saves\\Timestamp.SAV";
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    CHECK(file);
    SDL_PathInfo info{};
    CHECK(SDL_GetPathInfo(wiz8::path_to_utf8(user / "Saves" / "Timestamp.SAV").c_str(), &info));
    const int offset = wiz8::current_utc_offset_seconds();
    auto created = file->times().created;
    CHECK(created.ticks() ==
          wiz8::file_time_with_legacy_local_bias(wiz8::file_time_from_sdl(info.create_time), offset).ticks());

    // SaveGame's unchanged masks apply independently to the packed dwords.
    constexpr unsigned masks[] = {0x6b24e9f0u, 0xe77c28c1u};
    static_assert(sizeof(g_status.save_filetime_xor) == 8);
    unsigned words[] = {created.low ^ masks[0], created.high ^ masks[1]};
    file->write(words, sizeof(words));
    CHECK(file->tell() == 8);
    std::filesystem::last_write_time(user / "Saves" / "Timestamp.SAV",
                                     std::filesystem::file_time_type::clock::now() - hours(48));
    auto later = file->times().created;
    CHECK(later.ticks() == created.ticks());
    file.reset();
    std::ifstream encoded(user / "Saves" / "Timestamp.SAV", std::ios::binary);
    unsigned stored[2]{};
    encoded.read(reinterpret_cast<char*>(stored), sizeof(stored));
    CHECK(encoded.gcount() == 8 && stored[0] == words[0] && stored[1] == words[1]);
    encoded.close();
    CHECK((stored[0] ^ masks[0]) == created.low &&
          (stored[1] ^ masks[1]) == created.high);
    file = wiz8::open_file(path);
    const auto times = file->times();
    later = times.created;
    CHECK(SDL_GetPathInfo(wiz8::path_to_utf8(user / "Saves" / "Timestamp.SAV").c_str(), &info));
    // Reader fallback is current SDL create_time (POSIX ctime, not birth).
    CHECK(later.ticks() ==
          wiz8::file_time_with_legacy_local_bias(wiz8::file_time_from_sdl(info.create_time), offset).ticks());
    CHECK(times.modified.ticks() ==
          wiz8::file_time_with_legacy_local_bias(wiz8::file_time_from_sdl(info.modify_time), offset).ticks());
    file.reset();
    // Fixed epoch pair encodes the same eight bytes; no record layout changed.
    const unsigned epoch_masked[] = {epoch.low ^ masks[0], epoch.high ^ masks[1]};
    CHECK(epoch_masked[0] == 0xbe1a69f0u && epoch_masked[1] == 0xe6e1991fu);

    const auto now = std::filesystem::file_time_type::clock::now();
    for (int slot = 1; slot <= 3; ++slot)
    {
        const auto name = "Quick " + std::to_string(slot) + ".SAV";
        std::ofstream save(user / "Saves" / name, std::ios::binary);
        write(save, slot);
        save.close();
        std::filesystem::last_write_time(user / "Saves" / name,
                                        now - hours(slot == 1 ? 0 : slot == 2 ? 4 : 2));
    }
    CHECK(SelectQuickSaveSlotForWrite() == "Quick 2");
    CHECK(FindStartupQuickSave() == "Quick 1");
    char oldest[] = "Saves\\Quick 2.SAV", newest[] = "Saves\\Quick 1.SAV";
    const auto oldest_status = wiz8::file_status(oldest), newest_status = wiz8::file_status(newest);
    CHECK(oldest_status && newest_status);
    const auto oldest_time = wiz8::file_time_from_sdl(oldest_status->info.modify_time).ticks();
    const auto newest_time = wiz8::file_time_from_sdl(newest_status->info.modify_time).ticks();
    CHECK(oldest_time < newest_time && (newest_time - oldest_time) / 10000000 >= 3600);
    CHECK(!(newest_time < oldest_time));
    CHECK((newest_time - oldest_time) / 10000000 < 20000);
    wiz8::DiskFileTime high{0, 2}, low{UINT32_MAX, 1};
    CHECK(high.ticks() > low.ticks());
    const wiz8::DiskFileTime same_low{UINT32_MAX, 1};
    CHECK(low.ticks() < high.ticks() && low.ticks() == same_low.ticks());
    CHECK(wiz8::remove_file(oldest));
    CHECK(SelectQuickSaveSlotForWrite() == "Quick 2");
    CHECK(wiz8::remove_file(newest));
    // Missing Quick 1 must not leave the candidate timestamp uninitialized.
    CHECK(FindStartupQuickSave() == "Quick 3");
    char third[] = "Saves\\Quick 3.SAV";
    CHECK(wiz8::remove_file(third));
    CHECK(!FindStartupQuickSave());
    std::ofstream fallback(user / "Saves" / "Quick.SAV", std::ios::binary);
    write(fallback, 7u);
    fallback.close();
    CHECK(FindStartupQuickSave() == "Quick");
    UINT32 count = 0;

    char riff[] = "Saves\\Riff.SAV";
    W8Chunk chunks;
    CHECK(chunks.OpenWrite(riff));
    CHECK(chunks.OpenChunk(0x454d4954u, 0)); // TIME
    CHECK(chunks.Write(words, sizeof(words), &count) && count == 8);
    CHECK(chunks.ReleaseCurrentChunk());
    chunks.Close();
    CHECK(chunks.OpenAppend(riff));
    CHECK(chunks.OpenChunk(0x5458454eu, 0)); // NEXT
    CHECK(chunks.Write("tail", 4, &count) && count == 4);
    CHECK(chunks.ReleaseCurrentChunk());
    chunks.Close();
    CHECK(chunks.OpenRead(riff) && chunks.ChunkCount() == 2);
    CHECK(chunks.OpenChunk(0, 0) && chunks.CurrentChunkId() == 0x454d4954u);
    CHECK(chunks.Read(stored, sizeof(stored), &count) && count == 8);
    CHECK(stored[0] == words[0] && stored[1] == words[1]);
    CHECK(chunks.ReleaseCurrentChunk());
    CHECK(chunks.OpenChunk(0, 0) && chunks.CurrentChunkId() == 0x5458454eu);
    char tail[4]{};
    CHECK(chunks.Read(tail, sizeof(tail), &count) && count == 4 && !memcmp(tail, "tail", 4));
    CHECK(chunks.ReleaseCurrentChunk());
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
    status.pending_move_location.position.Set(1, 2, 3);
    char path[] = "C:\\gsta.bin";
    W8Chunk chunks;
    CHECK(chunks.OpenWrite(path));
    SaveGlobalStatus(&chunks, &status);
    chunks.Close();
    CHECK(chunks.OpenRead(path) && chunks.OpenChunk(0, 0));
    CHECK(chunks.CurrentChunkId() == 0x41545347);
    unsigned size = 0, count = 0;
    CHECK(chunks.Read(&size, sizeof(size), &count) && count == sizeof(size) && size == 0x49c2);
    unsigned char bytes[0x49c2];
    CHECK(chunks.Read(bytes, sizeof(bytes), &count) && count == sizeof(bytes));
    const auto status_image = wiz8::retail::encode(status);
    CHECK(!memcmp(bytes, status_image.data(), sizeof(bytes)));
    unsigned gold, turns;
    memcpy(&gold, bytes + 0x19, sizeof(gold));
    memcpy(&turns, bytes + 0x19d8, sizeof(turns));
    CHECK(gold == 0x12345678 && turns == 0x87654321);
    float position[3];
    memcpy(position, bytes + 0x22a7, sizeof(position));
    CHECK(position[0] == 1 && position[1] == 2 && position[2] == 3);
    // Verify each record's prefix and stride independently of native sizeof.
    for (int slot = 0; slot < 8; ++slot) {
        CHECK(chunks.Read(&size, 4, &count) && size == 0x1862);
        std::array<std::byte, 0x1862> character_bytes;
        CHECK(chunks.Read(character_bytes.data(), character_bytes.size(), &count));
    }
    for (int slot = 0; slot < 8; ++slot) {
        CHECK(chunks.Read(&size, 4, &count) && size == 0x106);
        std::array<std::byte, 0x106> party_bytes;
        CHECK(chunks.Read(party_bytes.data(), party_bytes.size(), &count));
        int attack_mode;
        float fatigue;
        std::memcpy(&attack_mode, party_bytes.data() + 5, 4);
        std::memcpy(&fatigue, party_bytes.data() + 0xf6, 4);
        CHECK(party_bytes[0] == std::byte{1} && attack_mode == 100 + slot);
        CHECK(fatigue == 1.25f + slot);
        CHECK(party_bytes[0x105] == std::byte(slot % 2));
    }
    chunks.ReleaseCurrentChunk();
    chunks.Close();
    W8Character restored_characters[8]{};
    W8PartySlotRow restored_rows[8]{};
    W8GlobalStatus restored{};
    restored.buffers.Char = restored_characters;
    restored.buffers.XChar = restored_rows;
    CHECK(chunks.OpenRead(path) && chunks.OpenChunk(0, 0));
    LoadGameStatus(&chunks, &restored);
    CHECK(restored.party_gold == status.party_gold);
    for (int slot = 0; slot < 8; ++slot) {
        CHECK(restored_rows[slot].fOccupied);
        CHECK(restored_rows[slot].attack_mode[0] == 100 + slot);
        CHECK(restored_rows[slot].movement_fatigue == 1.25f + slot);
        CHECK(restored_rows[slot].weapon_swap_pending == bool(slot % 2));
    }
    chunks.ReleaseCurrentChunk();
    chunks.Close();
}

static void packed_camera_angles()
{
    GDCamera camera;
    auto* previous = g_gd_camera;
    g_gd_camera = &camera;
    alignas(float) struct PackedCamera {
        char padding;
        W8WorldCameraState state;
    } packed{};
    CHECK(reinterpret_cast<uintptr_t>(&packed.state) % alignof(float) != 0);
    GetCameraOrientation(packed.state.yaw, packed.state.pitch);
    W8CameraAngleRecord yaw, pitch;
    memcpy(yaw, packed.state.yaw, sizeof(yaw));
    memcpy(pitch, packed.state.pitch, sizeof(pitch));
    for (unsigned i = 0; i < 6; ++i) CHECK(yaw[i] == 0 && pitch[i] == 0);
    yaw[0] = 0.5f;
    pitch[0] = 0.25f;
    yaw[5] = 123.0f;
    pitch[5] = 456.0f;
    memcpy(packed.state.yaw, yaw, sizeof(yaw));
    memcpy(packed.state.pitch, pitch, sizeof(pitch));
    SetCameraOrientation(packed.state.yaw, packed.state.pitch, nullptr);
    CHECK(camera.m_yaw == 0.5f && camera.m_pitch == 0.25f);
    memcpy(yaw, packed.state.yaw, sizeof(yaw));
    memcpy(pitch, packed.state.pitch, sizeof(pitch));
    CHECK(yaw[0] == 0.5f && pitch[0] == 0.25f && yaw[5] == 123.0f && pitch[5] == 456.0f);
    g_gd_camera = previous;
    delete camera.m_manual_input_timer;
}

int main()
{
    packed_camera_angles();
    const auto temporary = make_temporary_directory("wiz8-save-records");
    const auto root = wiz8::path_from_utf8(temporary);
    const auto assets = root / "assets", user = root / "user";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(user / "Saves");
    w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user), {"", "", ""}});
    global_status_record();
    const auto path = root / wiz8::path_from_utf8("npc-雪.bin");
    W8NpcDatabaseRecord database[3] = {};
    g_npc_records = database;
    // Keep the missing-database-node backfill out of this isolated fixture.
    gXStatus.uiNpcsInDatabase = 0;
    W8Character unrelated = {};
    W8_PTR32(W8Character) live;
    live = &unrelated;
    unsigned int live_handle;
    memcpy(&live_handle, &live, sizeof(live_handle));
    for (unsigned char version : {2, 3})
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        write(out, version);
        write(out, 3u);
        const unsigned int markers[] = {0x12345678u, live_handle, 0};
        for (unsigned int i = 0; i < 3; ++i)
        {
            W8NpcState disk = {};
            disk.name_style = static_cast<unsigned char>(i);
            memcpy(&disk.character, &markers[i], sizeof(markers[i]));
            write(out, disk);
            if (markers[i])
            {
                W8Character character = {};
                character.name.assign(1, static_cast<char>('A' + i));
                character.record_version = 2;
                if (version == 3)
                {
                    write(out, static_cast<unsigned int>(wiz8::retail::kCharacterBytes));
                }
                const auto image = wiz8::retail::encode(character);
                out.write(reinterpret_cast<const char*>(image.data()),
                          version == 2 ? 0x185c : image.size());
                CHECK(out.good());
            }
        }
        // Version >= 2 follows the states with an item count for each NPC.
        for (unsigned int i = 0; i < 3; ++i)
        {
            write(out, 0u);
        }
        write(out, 0xdecafbadU);
        out.close();
        W8Chunk chunk;
        chunk.m_hFile = wiz8::open_host_file(path);
        CHECK(chunk.m_hFile != 0);
        LoadNpcStates(&chunk);
        CHECK(static_cast<int>(g_npc_states.size()) == 3);
        for (unsigned int i = 0; i < 3; ++i)
        {
            W8NpcState* npc = g_npc_states[i];
            CHECK(npc->record == &database[i]);
            CHECK(npc->items == 0 && npc->script_file == 0);
            if (i < 2)
            {
                CHECK(npc->character != 0 && npc->character != &unrelated);
                CHECK(npc->character->name[0] == static_cast<char>('A' + i));
            }
            else
            {
                CHECK(npc->character == 0);
            }
        }
        unsigned int trailer;
        CHECK(chunk.Read(&trailer, sizeof(trailer), 0) && trailer == 0xdecafbadU);
        chunk.m_hFile.reset();
        chunk.m_hFile = 0;
        CHECK(!GetNpcState(-1) && !GetNpcState(3));
        ReleaseNpcBinding(3); // one past the end is not a live binding
        auto* kept = GetNpcState(1);
        auto* retired = g_npc_states[0];
        retired->binding_unavailable = true;
        CHECK(!GetNpcState(0));
        auto* replacement = CreateNpcRuntimeNode(0);
        CHECK(g_npc_states.size() == 3 && GetNpcState(0) == replacement);
        CHECK(replacement->partner_index == 0 && GetNpcState(1) == kept);
        CHECK(retired->binding_unavailable && retired->character->name[0] == L'A');
        ReleaseNpcStates();
        ReleaseNpcStates();
        CHECK(g_npc_states.empty());
    }
    g_npc_records = nullptr;
    character_file_contracts(assets, user);
    // NSF pointer words are presence markers too. Include empty strings and
    // zero-count arrays carrying a word that collides with a runtime handle.
    const auto script_path = user / "Saves" / "dialogue.nsf";
    for (unsigned int marker : {0x12345678u, live_handle, 0u})
    {
        std::ofstream out(script_path, std::ios::binary | std::ios::trunc);
        wiz8::retail::nsf::Header disk = {};
        disk.quote_count = 3;
        memcpy(&disk.name, &marker, sizeof(marker));
        write(out, disk);
        if (marker)
        {
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
        CHECK(out.good());
        out.close();
        char filename[] = "Saves\\dialogue.nsf";
        W8NpcScriptFile* file = LoadNpcScriptFile(filename);
        CHECK(file && file->quote_count == 3);
        CHECK(marker ? file->name == "name" : file->name.empty());
        W8NpcScriptQuote* quotes = file->quotes.data();
        CHECK(quotes[0].subquote_count == 2 && quotes[0].subquotes[0] == "é中😀");
        CHECK(quotes[0].subquotes[1].empty());
        W8NpcQuoteSubEntry* subs = quotes[0].entries[0].sub_entries.data();
        CHECK(subs[0].operand == 11 && subs[0].text == "line");
        CHECK(subs[1].operand == 22 && subs[1].text.empty());
        CHECK(quotes[1].entries.empty() && quotes[1].subquotes.empty());
        CHECK(quotes[2].entries[0].operand0 == 0x1234 && quotes[2].entries[0].sub_entries.empty());
        ReleaseNpcScriptFile(file);
    }
    {
        auto file = wiz8::open_file("Saves/truncated.nsf", wiz8::OpenMode::replace);
        wiz8::retail::nsf::Header disk{};
        disk.quote_count = 1;
        disk.name = 1;
        file->write(&disk, sizeof(disk));
        const std::uint16_t length = 4;
        file->write(&length, sizeof(length));
        file->write("bad", 3);
        file->close();
        CHECK(LoadNpcScriptFile("Saves/truncated.nsf") == nullptr);
    }
    // The AI used to decay the odd-offset condition array into unsigned int*.
    alignas(16) W8Character character = {};
    static W8SpellRuntimeRecord spells[W8_SPELL_COUNT] = {};
    g_spell_records = spells;
    spells[W8_SPELL_SLEEP].target_type = W8_TARGET_TYPE_ENEMY;
    g_status.buffers.Char = &character;
    W8CombatSlot target = {};
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = 0;
    CHECK(reinterpret_cast<uintptr_t>(character.uiCondition) % alignof(unsigned int) == 0);
    CHECK(MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    character.uiCondition[W8_CONDITION_ASLEEP] = 1;
    CHECK(!MonsterSpellTargetOK(nullptr, W8_SPELL_SLEEP, &target));
    g_status.buffers.Char = nullptr;
    g_spell_records = nullptr;
    struct alignas(16) PackedTriangle
    {
        char padding;
        srVector3T<float> vertices[3];
        srVector3T<float> point;
    } triangle;
    triangle.vertices[0].Set(0, 0, 0);
    triangle.vertices[1].Set(0, 2, 0);
    triangle.vertices[2].Set(0, 0, 2);
    triangle.point.Set(0, 0.25f, 0.25f);
    CHECK(PointInsideTriangle(triangle.vertices, 0, &triangle.point));
    triangle.point.Set(0, 3, 3);
    CHECK(!PointInsideTriangle(triangle.vertices, 0, &triangle.point));
    CHECK(BuildLevelStatusPath(56) == "Levels\\Test\\DefaultLevel.STS");
    for (int index = 0; index < 10; ++index) {
        const auto name = index == 0 ? std::string("Ending") : "Ending" + std::to_string(index);
        auto file = wiz8::open_file("Saves/" + name + ".SAV", wiz8::OpenMode::replace);
        file->close();
    }
    CHECK(FindFreeEndingSaveName() == std::optional<std::string>{"Ending10"});
    for (int index = 10; index < 20; ++index) {
        auto file = wiz8::open_file("Saves/Ending" + std::to_string(index) + ".SAV", wiz8::OpenMode::replace);
        file->close();
    }
    CHECK(FindFreeEndingSaveName() == std::optional<std::string>{"Ending20"});
    auto ending = wiz8::open_file("Saves/Ending20.SAV", wiz8::OpenMode::replace);
    ending->close();
    CHECK(!FindFreeEndingSaveName());
    save_file_contracts(user);
    transcript_contracts();
    keyword_file_contracts();
    unicode_file_contracts();
    unicode_database_contracts();
    encounter_table_contracts();
    script_record_contracts();
    path_record_contracts();
    animation_record_contracts();
    graphical_object_ownership_contracts();
    animated_mesh_cache_contracts();
    wiz8::clear_asset_archives();

    std::filesystem::remove_all(root);
    puts("NPCT/NSF consume retail and handle-valued markers; packed AI/vectors pass");
}

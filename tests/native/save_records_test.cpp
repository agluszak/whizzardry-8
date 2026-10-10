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
#include "wiz8/spell_ids.h"
#include "wiz8/xstatus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include "temporary_directory.h"

extern W8GrowableVector<W8NpcState*>* g_npc_states;
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
    g_status.game_started = false;
    CHECK(VerifyDataSubdirs());
    for (const wchar_t* name : {L"A", L"Vi", L"Sir Bob", L"Ninechars"})
    {
        W8Character character = {};
        wcscpy(character.name, name);
        character.uiExpLevel = 7;
        char path[260];
        BuildCharacterPath(path, name, -1);
        char stem[10] = {};
        CHECK(wcstombs(stem, name, sizeof(stem)) == wcslen(name));
        const std::string filename = std::string(stem) + ".CHR";
        CHECK(path == std::string("Saves\\Characters\\") + filename);
        CHECK(SaveCharacter(&character, -1, false, nullptr));
        CHECK(character.record_version == 1);
        const auto saved = user / "Saves" / "Characters" / filename;
        CHECK(std::filesystem::file_size(saved) == 4 + sizeof(character));
        CHECK(!std::filesystem::exists(assets / "Saves" / "Characters" / filename));
        std::ifstream encoded(saved, std::ios::binary);
        unsigned int size = 0;
        encoded.read(reinterpret_cast<char*>(&size), sizeof(size));
        CHECK(encoded.good() && size == sizeof(character));
        encoded.close();
        W8Character loaded = {};
        CHECK(LoadCharacter(filename.c_str(), &loaded, -1, false));
        CHECK(!memcmp(&loaded, &character, sizeof(character)));
        character.uiExpLevel = 8;
        CHECK(SaveCharacter(&character, -1, false, nullptr));
        CHECK(LoadCharacter(filename.c_str(), &loaded, -1, false));
        CHECK(!memcmp(&loaded, &character, sizeof(character)));
    }

    W8Character npc = {};
    wcscpy(npc.name, L"Vi");
    char path[260];
    BuildCharacterPath(path, npc.name, 0);
    CHECK(!strcmp(path, "Saves\\NPCs\\Vi.CHR"));
    CHECK(SaveCharacter(&npc, 0, false, nullptr));
    W8Character loaded = {};
    CHECK(LoadCharacter("Vi.CHR", &loaded, 0, false));
    CHECK(!memcmp(&loaded, &npc, sizeof(npc)));

    g_status.game_started = true;
    g_status.flags[0] = 0;
    BuildCharacterPath(path, npc.name, 0);
    CHECK(!strcmp(path, "Saves\\NPCs\\Vi.CHR"));
    g_status.flags[0] = 1;
    BuildCharacterPath(path, npc.name, 0);
    CHECK(!strcmp(path, "Vi.CHR"));
    BuildCharacterPath(path, npc.name, -1);
    CHECK(!strcmp(path, "Vi.CHR"));
    g_status.flags[0] = 0;
    g_status.game_started = false;
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
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.GetCount() == 0);
    write_keywords(translated, "header\n01234567890bonjour/monde/\n01234567890au revoir/final");
    ReloadKeywordLists();
    CHECK(g_keyword_lists_loaded && g_keyword_lists.GetCount() == 2);
    auto first = *g_keyword_lists.GetAt(0);
    CHECK(first->GetCount() == 2);
    auto row = *first->GetAt(0);
    CHECK(row->GetCount() == 2 && !wcscmp(*row->GetAt(0), L"hello") &&
          !wcscmp(*row->GetAt(1), L"world"));
    row = *first->GetAt(1);
    CHECK(row->GetCount() == 2 && !wcscmp(*row->GetAt(0), L"bye") &&
          !wcscmp(*row->GetAt(1), L"last"));
    ClearKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.GetCount() == 0);
    write_keywords(english, "header\n01234567890partial/row/\n01234567890" + std::string("\0bad", 4));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.GetCount() == 0);
    write_keywords(english, "header\n01234567890" + std::string(1000, 'x'));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.GetCount() == 0);
    CHECK(wiz8::remove_file(english));
    ReloadKeywordLists();
    CHECK(!g_keyword_lists_loaded && g_keyword_lists.GetCount() == 0);
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
    char selected[260];
    CHECK(SelectQuickSaveSlotForWrite(selected) && !strcmp(selected, "Quick 2"));
    CHECK(FindStartupQuickSave(selected) && !strcmp(selected, "Quick 1"));
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
    CHECK(SelectQuickSaveSlotForWrite(selected) && !strcmp(selected, "Quick 2"));
    CHECK(wiz8::remove_file(newest));
    // Missing Quick 1 must not leave the candidate timestamp uninitialized.
    CHECK(FindStartupQuickSave(selected) && !strcmp(selected, "Quick 3"));
    char third[] = "Saves\\Quick 3.SAV";
    CHECK(wiz8::remove_file(third));
    CHECK(!FindStartupQuickSave(selected));
    std::ofstream fallback(user / "Saves" / "Quick.SAV", std::ios::binary);
    write(fallback, 7u);
    fallback.close();
    CHECK(FindStartupQuickSave(selected) && !strcmp(selected, "Quick"));
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
    CHECK(!memcmp(bytes, &status, sizeof(bytes)));
    unsigned gold, turns;
    memcpy(&gold, bytes + 0x19, sizeof(gold));
    memcpy(&turns, bytes + 0x19d8, sizeof(turns));
    CHECK(gold == 0x12345678 && turns == 0x87654321);
    float position[3];
    memcpy(position, bytes + 0x22a7, sizeof(position));
    CHECK(position[0] == 1 && position[1] == 2 && position[2] == 3);
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
                character.name[0] = static_cast<wchar_t>('A' + i);
                character.record_version = 2;
                if (version == 3)
                {
                    write(out, static_cast<unsigned int>(sizeof(character)));
                }
                out.write(reinterpret_cast<const char*>(&character),
                          version == 2 ? 0x185c : sizeof(character));
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
        CHECK(g_npc_states->GetCount() == 3);
        for (unsigned int i = 0; i < 3; ++i)
        {
            W8NpcState* npc = *g_npc_states->GetAt(i);
            CHECK(npc->record == &database[i]);
            CHECK(npc->items == 0 && npc->script_file == 0);
            if (i < 2)
            {
                CHECK(npc->character != 0 && npc->character != &unrelated);
                CHECK(npc->character->name[0] == static_cast<wchar_t>('A' + i));
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
        ReleaseNpcStates();
    }
    g_npc_records = nullptr;
    character_file_contracts(assets, user);
    // NSF pointer words are presence markers too. Include empty strings and
    // zero-count arrays carrying a word that collides with a runtime handle.
    const auto script_path = user / "Saves" / "dialogue.nsf";
    for (unsigned int marker : {0x12345678u, live_handle, 0u})
    {
        std::ofstream out(script_path, std::ios::binary | std::ios::trunc);
        W8NpcScriptFile disk = {};
        disk.quote_count = 3;
        memcpy(&disk.name, &marker, sizeof(marker));
        write(out, disk);
        if (marker)
        {
            write(out, static_cast<unsigned short>(4));
            out.write("name", 4);
        }
        W8NpcScriptQuote quote = {};
        quote.entry_count = 1;
        const unsigned int present = marker ? marker : 0x87654321u;
        memcpy(&quote.subquotes, &present, sizeof(present));
        write(out, quote);
        write(out, static_cast<unsigned char>(2));
        write(out, static_cast<unsigned short>(3));
        const wchar_t text[] = {L'o', L'n', L'e'};
        out.write(reinterpret_cast<const char*>(text), sizeof(text));
        write(out, static_cast<unsigned short>(0));
        W8NpcQuoteEntry entry = {};
        entry.sub_entry_count = 2;
        write(out, entry);
        W8NpcQuoteSubEntry sub = {};
        sub.operand = 11;
        memcpy(&sub.text, &present, sizeof(present));
        write(out, sub);
        write(out, static_cast<unsigned short>(4));
        out.write("line", 4);
        sub.text = nullptr;
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
        CHECK(marker ? file->name != 0 && strcmp(file->name, "name") == 0 : file->name == 0);
        W8NpcScriptQuote* quotes = file->quotes;
        CHECK(quotes[0].subquote_count == 2 && strcmp(quotes[0].subquotes[0], "one") == 0);
        CHECK(quotes[0].subquotes[1] == nullptr);
        W8NpcQuoteSubEntry* subs = quotes[0].entries[0].sub_entries;
        CHECK(subs[0].operand == 11 && strcmp(subs[0].text, "line") == 0);
        CHECK(subs[1].operand == 22 && subs[1].text == 0);
        CHECK(quotes[1].entries == 0 && quotes[1].subquotes == 0);
        CHECK(quotes[2].entries[0].operand0 == 0x1234 && quotes[2].entries[0].sub_entries == 0);
        // The recovered release routine leaves outer arrays/text to callers.
        free(subs[0].text);
        free(subs);
        free(quotes[0].entries);
        free(quotes[0].subquotes[0]);
        free(quotes[0].subquotes);
        free(quotes[2].entries);
        free(quotes);
        free(file->name);
        free(file);
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
    CHECK(reinterpret_cast<uintptr_t>(character.uiCondition) % alignof(unsigned int) != 0);
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
    save_file_contracts(user);
    keyword_file_contracts();
    wiz8::clear_asset_archives();

    std::filesystem::remove_all(root);
    puts("NPCT/NSF consume retail and handle-valued markers; packed AI/vectors pass");
}

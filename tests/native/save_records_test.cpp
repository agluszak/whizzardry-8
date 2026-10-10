/* Exercise recovered NPCT loading with retail addresses and native handles as
   on-disk presence markers. No installed game data or display is required. */
#include "FileMan.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/spell_ids.h"
#include "wiz8/xstatus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "temporary_directory.h"

extern W8GrowableVector<W8NpcState*>* g_npc_states;

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

int main()
{
    const auto temporary = make_temporary_directory("wiz8-save-records");
    const auto path = std::filesystem::path(temporary) / "npc.bin";
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
        std::string filename = path.string();
        chunk.m_hFile = FileOpen(filename.data(), FILE_ACCESS_READ | FILE_OPEN_EXISTING, 0);
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
        FileClose(chunk.m_hFile);
        chunk.m_hFile = 0;
        ReleaseNpcStates();
    }
    g_npc_records = nullptr;
    // NSF pointer words are presence markers too. Include empty strings and
    // zero-count arrays carrying a word that collides with a runtime handle.
    const auto script_path = std::filesystem::path(temporary) / "dialogue.nsf";
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
        std::string filename = script_path.string();
        W8NpcScriptFile* file = LoadNpcScriptFile(filename.data());
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
    std::filesystem::remove_all(temporary);
    puts("NPCT/NSF consume retail and handle-valued markers; packed AI/vectors pass");
}

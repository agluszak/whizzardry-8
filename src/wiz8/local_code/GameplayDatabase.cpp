#include "wiz8/retail_text_records.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/3d_code/PList.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/xstatus.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/item_tables.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/utility.h"
#include "wiz8/sr_api.h"
#include "wiz8/vector.h"
#include "wiz8/virtual_file.h"
#include "wiz8/filesystem.h"
#include "random.h"
#include "timer.h"
#include "wiz8/local_code/character_events.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 0x0054B300 resets one of eight slots. */
/* The gStatus object owned by GameplayDatabase.cpp. */
// GLOBAL: WIZ8 0x00685170
W8GlobalStatus g_status;
/* Packed gXStatus named by the database and manager assertions. Record
   arrays remain separate roots at their own addresses. */
// GLOBAL: WIZ8 0x006836B8
W8XStatus gXStatus;
/* Persistent database roots owned by this translation unit.  Leaving these as
   unresolved externals made the runnable image relocate every load/store to
   the PE image base; the first four-byte count read consequently targeted a
   read-only header instead of game state. */
// GLOBAL: WIZ8 0x006836AC
W8FactDatabaseRecord* g_fact_records;
// GLOBAL: WIZ8 0x0068516C
W8ItemDatabaseRecord* g_item_records;
/* The six item stacks granted by the new-game status reset. */
// GLOBAL: WIZ8 0x006164DC
unsigned int g_starting_item_ids[6] = {0x14f, 0x14f, 0x15a, 0x15a, 0x15b, 0x155};
// GLOBAL: WIZ8 0x006836A4
W8LevelDatabaseRecord* g_level_records;
// GLOBAL: WIZ8 0x006836A0
W8NpcDatabaseRecord* g_npc_records;
// GLOBAL: WIZ8 0x006836B0
W8ItemTableRecord** g_item_tables;
// GLOBAL: WIZ8 0x006836B4
char** g_item_table_category_names;
// GLOBAL: WIZ8 0x0065BE1C
W8SpellRuntimeRecord* g_spell_records;
// GLOBAL: WIZ8 0x0065BE18
unsigned int g_spell_database_version;
#define GAMEPLAY_DATABASE_CPP "C:\\Projects\\Wizardry 8\\Local Code\\GameplayDatabase.cpp"

// FUNCTION: WIZ8 0x0054a400
bool InitializeItemDatabase(void)
try
{
    char path[60];
    unsigned int index;
    unsigned int transferred;
    std::unique_ptr<wiz8::File> handle;

    sprintf(path, "%s\\%s.%s", "Data\\Databases", "Items", "DBS");
    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return false;
    }
    if (!((transferred = handle->read(&gXStatus.uiItemsInDatabase, 4).bytes) == static_cast<std::size_t>(4))) {
        if (handle) handle->close();
        handle.reset();
        return false;
    }
    g_item_records = static_cast<W8ItemDatabaseRecord*>(
        malloc(gXStatus.uiItemsInDatabase * sizeof(*g_item_records)));
    if (!g_item_records) {
        return false;
    }
    for (index = 0; index < gXStatus.uiItemsInDatabase; ++index) {
        if (!((transferred = wiz8::retail::read(*handle, g_item_records[index]).bytes) == wiz8::retail::size<W8ItemDatabaseRecord>)) {
            if (handle) handle->close();
            handle.reset();
            return false;
        }
    }
    if (handle) handle->close();
    handle.reset();
    return true;
}
catch (const std::exception&) { return false; }

/* ItemTables.DBS carries two arrays: category names, each a fixed 0x100-byte
   buffer, then the tables themselves. Both are arrays of pointers, cleared
   before use. The category reads are unchecked in the original while the table
   reads are not, and the per-table allocation is cleared before its own null
   check rather than after; both are reproduced. */
// FUNCTION: WIZ8 0x0054a510
bool InitializeItemTables(void)
try
{
    char path[60];
    unsigned int index;
    unsigned int transferred;
    std::unique_ptr<wiz8::File> handle;

    sprintf(path, "%s\\%s.%s", "Data\\Databases", "ItemTables", "DBS");
    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return false;
    }
    if (!((transferred = handle->read(&gXStatus.uiItemTableCategories, 4).bytes) == static_cast<std::size_t>(4))) {
        if (handle) handle->close();
        handle.reset();
        return false;
    }
    if (gXStatus.uiItemTableCategories) {
        g_item_table_category_names =
            static_cast<char**>(malloc(gXStatus.uiItemTableCategories * sizeof(*g_item_table_category_names)));
        if (!g_item_table_category_names) {
            return false;
        }
        memset(g_item_table_category_names, 0, gXStatus.uiItemTableCategories * sizeof(*g_item_table_category_names));
        for (index = 0; index < gXStatus.uiItemTableCategories; ++index) {
            g_item_table_category_names[index] = static_cast<char*>(malloc(0x100));
            ((transferred = handle->read(g_item_table_category_names[index], 0x100).bytes) == static_cast<std::size_t>(0x100));
        }
    }
    if (!((transferred = handle->read(&gXStatus.uiItemTablesInDatabase, 4).bytes) == static_cast<std::size_t>(4))) {
        if (handle) handle->close();
        handle.reset();
        return false;
    }
    if (gXStatus.uiItemTablesInDatabase) {
        g_item_tables = static_cast<W8ItemTableRecord**>(
            malloc(gXStatus.uiItemTablesInDatabase * sizeof(*g_item_tables)));
        if (!g_item_tables) {
            return false;
        }
        memset(g_item_tables, 0, gXStatus.uiItemTablesInDatabase * sizeof(*g_item_tables));
        for (index = 0; index < gXStatus.uiItemTablesInDatabase; ++index) {
            g_item_tables[index] =
                static_cast<W8ItemTableRecord*>(malloc(sizeof(W8ItemTableRecord)));
            memset(g_item_tables[index], 0, sizeof(*g_item_tables[index]));
            if (!g_item_tables[index]) {
                return false;
            }
            if (!((transferred = handle->read(g_item_tables[index]->name, sizeof(*g_item_tables[index])).bytes) == static_cast<std::size_t>(sizeof(*g_item_tables[index])))) {
                if (handle) handle->close();
                handle.reset();
                return false;
            }
        }
    }
    if (handle) handle->close();
    handle.reset();
    return true;
}
catch (const std::exception&) { return false; }

/* Seeks straight to one record rather than holding the file open, and strips the
   four name fields afterwards. The failed seek leaves the handle open where
   every other failure closes it, as elsewhere in this unit. */
// FUNCTION: WIZ8 0x0054a8a0
bool LoadMonsterDatabaseRecord(unsigned int uiMonsterIndex, W8MonsterRecord* record)
try
{
    char path[60];
    unsigned int bytes_read;
    std::unique_ptr<wiz8::File> handle;

    if (!(uiMonsterIndex < gXStatus.uiMonstersInDatabase)) {
        srAssertFail("uiMonsterIndex < gXStatus.uiMonstersInDatabase", GAMEPLAY_DATABASE_CPP, 0x140,
                     0);
    }
    sprintf(path, "%s\\%s.%s", "Data\\Databases", "Monsters", "DBS");
    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return false;
    }
    if (!(handle->seek(uiMonsterIndex * wiz8::retail::size<W8MonsterRecord> + 4, wiz8::SeekOrigin::begin), true)) {
        return false;
    }
    if (!((bytes_read = wiz8::retail::read(*handle, *record).bytes) == wiz8::retail::size<W8MonsterRecord>)) {
        if (handle) handle->close();
        handle.reset();
        return false;
    }
    if (handle) handle->close();
    handle.reset();
    StripMonsterNameSuffix(record->name0);
    StripMonsterNameSuffix(record->name1);
    StripMonsterNameSuffix(record->name2);
    StripMonsterNameSuffix(record->name3);
    return true;
}
catch (const std::exception&) { return false; }

/* Unlike its fact and level siblings this one guards the free and then leaves
   the pointer dangling rather than clearing it. Both halves of that asymmetry
   are the original's. */
// FUNCTION: WIZ8 0x0054a4f0
void DestroyItemDatabase(void)
{
    if (g_item_records) {
        free(g_item_records);
    }
}

/* A generic guarded free, called from three unrelated subsystems, so it is named
   for what it does rather than for any one database. */
// FUNCTION: WIZ8 0x0054a880
void FreeIfNotNull(void* block)
{
    if (block) {
        free(block);
    }
}

/* The counterpart to InitializeItemTables: the category names first, then the
   tables, each entry freed before its array. Both arrays are re-read after
   every free because nothing tells VC6 that free leaves them alone. */
// FUNCTION: WIZ8 0x0054a6e0
void DestroyItemTables(void)
{
    unsigned int index;

    if (g_item_table_category_names) {
        for (index = 0; index < gXStatus.uiItemTableCategories; ++index) {
            if (g_item_table_category_names[index]) {
                free(g_item_table_category_names[index]);
            }
        }
        free(g_item_table_category_names);
    }
    if (g_item_tables) {
        for (index = 0; index < gXStatus.uiItemTablesInDatabase; ++index) {
            if (g_item_tables[index]) {
                free(g_item_tables[index]);
            }
        }
        free(g_item_tables);
    }
}

/* Reads MONSTERS.DBS whole: the count into gXStatus, then - only when the
   caller wants them - every record into one allocation handed back through the
   out-parameter. InitializeGame calls it with null just to publish the count. */
// FUNCTION: WIZ8 0x0054a760
bool LoadMonsterDatabase(W8MonsterRecord** records)
try
{
    char path[60];
    unsigned int transferred;
    unsigned int index;
    W8MonsterRecord* block;
    std::unique_ptr<wiz8::File> handle;

    sprintf(path, "%s\\%s.%s", "Data\\Databases", "Monsters", "DBS");
    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return false;
    }
    if (!((transferred = handle->read(&gXStatus.uiMonstersInDatabase, 4).bytes) == static_cast<std::size_t>(4))) {
        if (handle) handle->close();
        handle.reset();
        return false;
    }
    if (records) {
        block = static_cast<W8MonsterRecord*>(
            malloc(gXStatus.uiMonstersInDatabase * sizeof(W8MonsterRecord)));
        if (!block) {
            return false;
        }
        for (index = 0; index < gXStatus.uiMonstersInDatabase; ++index) {
            if (!((transferred = wiz8::retail::read(*handle, block[index]).bytes) == wiz8::retail::size<W8MonsterRecord>)) {
                if (handle) handle->close();
                handle.reset();
                free(block);
                return false;
            }
        }
        *records = block;
    }
    if (handle) handle->close();
    handle.reset();
    return true;
}
catch (const std::exception&) { return false; }

/* The range sibling of LoadMonsterDatabaseRecord, named by its own assertion at
   GameplayDatabase.cpp line 378. It seeks to the first record and reads the
   whole inclusive span in one call, computing the length as two separate record
   offsets subtracted rather than from a record count. A failed seek leaves the
   handle open where every other failure closes it. */
// FUNCTION: WIZ8 0x0054a9a0
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records)
try
{
    char path[60];
    std::unique_ptr<wiz8::File> handle;

    if (!(uiEndIndex < gXStatus.uiMonstersInDatabase)) {
        srAssertFail("uiEndIndex < gXStatus.uiMonstersInDatabase", GAMEPLAY_DATABASE_CPP, 0x17a, 0);
    }
    if (uiStartIndex > uiEndIndex) {
        srAssertFail("uiStartIndex <= uiEndIndex", GAMEPLAY_DATABASE_CPP, 0x17b, 0);
    }
    sprintf(path, "%s\\%s.%s", "Data\\Databases", "Monsters", "DBS");
    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (!handle) {
        return false;
    }
    if (!(handle->seek(uiStartIndex * wiz8::retail::size<W8MonsterRecord> + 4, wiz8::SeekOrigin::begin), true)) {
        return false; /* retail: failed seek leaves the handle open */
    }
    for (unsigned int index = uiStartIndex; index <= uiEndIndex; ++index) {
        if (wiz8::retail::read(*handle, records[index - uiStartIndex]).bytes != wiz8::retail::size<W8MonsterRecord>) return false;
    }
    if (handle) handle->close();
    handle.reset();
    return true;
}
catch (const std::exception&) { return false; }

/* Retail emits this owning-list teardown out of line here and expands the same
   operation at the NPC-item sites.  The exact source boundary remains
   unresolved; it is neither an authored specialization nor a W8PList member
   destructor under the VC6 ABI. */

// FUNCTION: WIZ8 0x0054ac90
void DestroyNpcDatabase(void)
{
    unsigned int index;

    if (g_npc_records) {
        for (index = 0; index < gXStatus.uiNpcsInDatabase; ++index) {
            if (g_npc_records[index].item_stock_rules) {
                W8PList* rules = g_npc_records[index].item_stock_rules;
                while (PLLength(rules) != 0) {
                    delete static_cast<W8NpcItemStockRule*>(PLRemoveAt(rules, 0));
                }
                PListFreeData(rules);
                PLDestroy(rules);
                g_npc_records[index].item_stock_rules = 0;
            }
        }
        free(g_npc_records);
        g_npc_records = 0;
    }
}

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
#include "wiz8/filesystem.h"
#include "random.h"
#include "timer.h"
#include "wiz8/local_code/character_events.h"
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <vector>

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

namespace {
std::unique_ptr<W8ItemDatabaseRecord[]> item_records;
std::vector<std::array<char, 0x100>> item_table_categories;
std::vector<W8ItemTableRecord> item_tables;
std::vector<char*> item_table_category_names;
std::vector<W8ItemTableRecord*> item_table_pointers;
}

// FUNCTION: WIZ8 0x0054a400
bool InitializeItemDatabase(void)
try
{
    auto handle = wiz8::open_file("Data\\Databases\\Items.DBS", wiz8::OpenMode::read);
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count > (handle->size() - handle->tell()) / std::int64_t(sizeof(W8ItemDatabaseRecord))) {
        return false;
    }
    auto records = count ? std::make_unique<W8ItemDatabaseRecord[]>(count) : nullptr;
    handle->read_exact(records.get(), std::size_t(count) * sizeof(W8ItemDatabaseRecord));
    item_records = std::move(records);
    g_item_records = item_records.get();
    gXStatus.uiItemsInDatabase = count;
    return true;
}
catch (const std::exception&) { return false; }

/* ItemTables.DBS carries fixed 0x100-byte category names, then table records.
   The exported pointer arrays borrow contiguous storage owned here. */
// FUNCTION: WIZ8 0x0054a510
bool InitializeItemTables(void)
try
{
    auto handle = wiz8::open_file("Data\\Databases\\ItemTables.DBS", wiz8::OpenMode::read);
    unsigned int category_count;
    handle->read_exact(&category_count, sizeof(category_count));
    const auto remaining = handle->size() - handle->tell();
    if (remaining < std::int64_t(sizeof(unsigned int)) ||
        category_count > (remaining - std::int64_t(sizeof(unsigned int))) / 0x100) {
        return false;
    }
    std::vector<std::array<char, 0x100>> categories(category_count);
    handle->read_exact(categories.data(), categories.size() * sizeof(categories[0]));
    std::vector<char*> names;
    names.reserve(category_count);
    for (auto& category : categories) {
        if (std::find(category.begin(), category.end(), '\0') == category.end()) {
            return false;
        }
        names.push_back(category.data());
    }

    unsigned int table_count;
    handle->read_exact(&table_count, sizeof(table_count));
    if (table_count > (handle->size() - handle->tell()) / std::int64_t(sizeof(W8ItemTableRecord))) {
        return false;
    }
    std::vector<W8ItemTableRecord> tables(table_count);
    handle->read_exact(tables.data(), tables.size() * sizeof(W8ItemTableRecord));
    std::vector<W8ItemTableRecord*> pointers;
    pointers.reserve(table_count);
    for (auto& table : tables) {
        if (std::find(std::begin(table.name), std::end(table.name), '\0') == std::end(table.name)) {
            return false;
        }
        pointers.push_back(&table);
    }

    item_table_categories.swap(categories);
    item_table_category_names.swap(names);
    item_tables.swap(tables);
    item_table_pointers.swap(pointers);
    g_item_table_category_names = category_count ? item_table_category_names.data() : nullptr;
    g_item_tables = table_count ? item_table_pointers.data() : nullptr;
    gXStatus.uiItemTableCategories = category_count;
    gXStatus.uiItemTablesInDatabase = table_count;
    return true;
}
catch (const std::exception&) { return false; }

/* Seeks straight to one record and strips the four name suffixes. */
// FUNCTION: WIZ8 0x0054a8a0
bool LoadMonsterDatabaseRecord(unsigned int uiMonsterIndex, W8MonsterRecord* record)
try
{
    if (!(uiMonsterIndex < gXStatus.uiMonstersInDatabase)) {
        srAssertFail("uiMonsterIndex < gXStatus.uiMonstersInDatabase", GAMEPLAY_DATABASE_CPP, 0x140,
                     0);
    }
    auto handle = wiz8::open_file("Data\\Databases\\Monsters.DBS", wiz8::OpenMode::read);
    handle->seek(std::int64_t(uiMonsterIndex) * std::int64_t(sizeof(*record)) + 4, wiz8::SeekOrigin::begin);
    W8MonsterRecord loaded;
    handle->read_exact(&loaded, sizeof(loaded));
    for (auto* name : {loaded.name0, loaded.name1, loaded.name2, loaded.name3}) {
        if (std::find(name, name + std::size(loaded.name0), L'\0') == name + std::size(loaded.name0)) {
            return false;
        }
        StripMonsterNameSuffix(name);
    }
    *record = loaded;
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x0054a4f0
void DestroyItemDatabase(void)
{
    item_records.reset();
    g_item_records = nullptr;
    gXStatus.uiItemsInDatabase = 0;
}

/* A generic free, called from three unrelated subsystems, so it is named
   for what it does rather than for any one database. */
// FUNCTION: WIZ8 0x0054a880
void FreeIfNotNull(void* block)
{
    free(block);
}

// FUNCTION: WIZ8 0x0054a6e0
void DestroyItemTables(void)
{
    decltype(item_table_categories){}.swap(item_table_categories);
    decltype(item_table_category_names){}.swap(item_table_category_names);
    decltype(item_tables){}.swap(item_tables);
    decltype(item_table_pointers){}.swap(item_table_pointers);
    g_item_table_category_names = nullptr;
    g_item_tables = nullptr;
    gXStatus.uiItemTableCategories = 0;
    gXStatus.uiItemTablesInDatabase = 0;
}

/* Reads MONSTERS.DBS whole: the count into gXStatus, then - only when the
   caller wants them - every record into one allocation handed back through the
   out-parameter. InitializeGame calls it with null just to publish the count. */
// FUNCTION: WIZ8 0x0054a760
bool LoadMonsterDatabase(W8MonsterRecord** records)
try
{
    auto handle = wiz8::open_file("Data\\Databases\\Monsters.DBS", wiz8::OpenMode::read);
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count > std::size(gXStatus.monster_record_cache) ||
        count > (handle->size() - handle->tell()) / std::int64_t(sizeof(W8MonsterRecord))) {
        return false;
    }
    if (records) {
        const auto bytes = std::size_t(count) * sizeof(W8MonsterRecord);
        std::unique_ptr<W8MonsterRecord, decltype(&free)> block(
            count ? static_cast<W8MonsterRecord*>(malloc(bytes)) : nullptr, &free);
        if (count && !block) {
            return false;
        }
        handle->read_exact(block.get(), bytes);
        *records = block.release();
    }
    gXStatus.uiMonstersInDatabase = count;
    return true;
}
catch (const std::exception&) { return false; }

/* The range sibling of LoadMonsterDatabaseRecord reads an inclusive span. */
// FUNCTION: WIZ8 0x0054a9a0
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records)
try
{
    if (!(uiEndIndex < gXStatus.uiMonstersInDatabase)) {
        srAssertFail("uiEndIndex < gXStatus.uiMonstersInDatabase", GAMEPLAY_DATABASE_CPP, 0x17a, 0);
    }
    if (uiStartIndex > uiEndIndex) {
        srAssertFail("uiStartIndex <= uiEndIndex", GAMEPLAY_DATABASE_CPP, 0x17b, 0);
    }
    auto handle = wiz8::open_file("Data\\Databases\\Monsters.DBS", wiz8::OpenMode::read);
    handle->seek(std::int64_t(uiStartIndex) * std::int64_t(sizeof(*records)) + 4, wiz8::SeekOrigin::begin);
    const auto count = std::size_t(uiEndIndex) - uiStartIndex + 1;
    const auto remaining = handle->size() - handle->tell();
    if (remaining < 0 || count > std::uint64_t(remaining) / sizeof(*records)) {
        return false;
    }
    std::vector<W8MonsterRecord> loaded(count);
    handle->read_exact(loaded.data(), loaded.size() * sizeof(*records));
    std::copy(loaded.begin(), loaded.end(), records);
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
    if (g_npc_records) {
        for (unsigned int index = 0; index < gXStatus.uiNpcsInDatabase; ++index) {
            if (g_npc_records[index].item_stock_rules) {
                W8PList* rules = g_npc_records[index].item_stock_rules;
                for (int entry = 0; entry < rules->iNumUsed; ++entry) {
                    delete static_cast<W8NpcItemStockRule*>(rules->data[entry]);
                }
                PLDestroy(rules);
            }
        }
        free(g_npc_records);
        g_npc_records = 0;
    }
    gXStatus.uiNpcsInDatabase = 0;
}

#include "wiz8/local_code/GameplayDatabase.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "layouts/item_tables.h"
#include "retail_records.h"
#include "surrender/srDebug.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/xstatus.h"

/* 0x0054B300 resets one of eight slots. */
/* The gStatus object owned by GameplayDatabase.cpp. */
W8GlobalStatus g_status;
/* Packed gXStatus named by the database and manager assertions. Record
   arrays remain separate roots at their own addresses. */
W8XStatus gXStatus;
/* Persistent database roots owned by this translation unit.  Leaving these as
   unresolved externals made the runnable image relocate every load/store to
   the PE image base; the first four-byte count read consequently targeted a
   read-only header instead of game state. */
std::vector<W8FactDatabaseRecord> g_fact_records;
std::vector<W8ItemDatabaseRecord> g_item_records;
/* The six item stacks granted by the new-game status reset. */
unsigned int g_starting_item_ids[6] = {0x14f, 0x14f, 0x15a, 0x15a, 0x15b, 0x155};
std::vector<W8LevelDatabaseRecord> g_level_records;
std::vector<W8NpcDatabaseRecord> g_npc_records;
W8ItemTableRecord* g_item_tables;
std::string* g_item_table_category_names;
std::vector<W8SpellRuntimeRecord> g_spell_records;

namespace {
std::vector<std::string> item_table_categories;
std::vector<W8ItemTableRecord> item_tables;
}

bool InitializeItemDatabase(void)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\Items.DBS"), wiz8::OpenMode::read);
    g_item_records = wiz8::retail::read_database<W8ItemDatabaseRecord>(*handle);
    return true;
} catch (const std::exception&) {
    return false;
}

/* ItemTables.DBS carries fixed 0x100-byte category names, then table records.
   The exported views borrow contiguous storage owned here. */
bool InitializeItemTables(void)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\ItemTables.DBS"), wiz8::OpenMode::read);
    unsigned int category_count;
    handle->read_exact(&category_count, sizeof(category_count));
    const auto remaining = handle->size() - handle->tell();
    if (remaining < std::int64_t(sizeof(unsigned int)) ||
        category_count > (remaining - std::int64_t(sizeof(unsigned int))) / 0x100) {
        return false;
    }
    std::vector<std::string> categories(category_count);
    for (auto& category : categories) {
        std::array<char, 256> bytes;
        handle->read_exact(bytes.data(), bytes.size());
        auto end = std::find(bytes.begin(), bytes.end(), '\0');
        if (end == bytes.end()) return false;
        category.assign(bytes.begin(), end);
    }

    unsigned int table_count;
    handle->read_exact(&table_count, sizeof(table_count));
    if (table_count > (handle->size() - handle->tell()) / std::int64_t(sizeof(W8ItemTableRecord))) {
        return false;
    }
    std::vector<W8ItemTableRecord> tables(table_count);
    handle->read_exact(tables.data(), tables.size() * sizeof(W8ItemTableRecord));
    for (auto& table : tables) {
        if (std::find(std::begin(table.name), std::end(table.name), '\0') == std::end(table.name)) {
            return false;
        }
    }

    item_table_categories.swap(categories);
    item_tables.swap(tables);
    g_item_table_category_names = category_count ? item_table_categories.data() : nullptr;
    g_item_tables = table_count ? item_tables.data() : nullptr;
    gXStatus.uiItemTableCategories = category_count;
    gXStatus.uiItemTablesInDatabase = table_count;
    return true;
} catch (const std::exception&) {
    return false;
}

/* Seeks straight to one record and strips the four name suffixes. */
bool LoadMonsterDatabaseRecord(unsigned int uiMonsterIndex, W8MonsterRecord* record)
try {
    if (!(uiMonsterIndex < gXStatus.uiMonstersInDatabase)) {
        Fail("uiMonsterIndex < gXStatus.uiMonstersInDatabase");
    }
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\Monsters.DBS"), wiz8::OpenMode::read);
    handle->seek(std::int64_t(uiMonsterIndex) * std::int64_t(wiz8::retail::kMonsterRecordBytes) + 4,
                 wiz8::SeekOrigin::begin);
    W8MonsterRecord loaded;
    wiz8::retail::read_exact(*handle, loaded);
    for (auto* name : {&loaded.name0, &loaded.name1, &loaded.name2, &loaded.name3})
        StripMonsterNameSuffix(*name);
    *record = std::move(loaded);
    return true;
} catch (const std::exception&) {
    return false;
}

void DestroyItemDatabase(void)
{
    g_item_records = {};
}

void DestroyItemTables(void)
{
    decltype(item_table_categories){}.swap(item_table_categories);
    decltype(item_tables){}.swap(item_tables);
    g_item_table_category_names = nullptr;
    g_item_tables = nullptr;
    gXStatus.uiItemTableCategories = 0;
    gXStatus.uiItemTablesInDatabase = 0;
}

/* Reads MONSTERS.DBS whole: the count into gXStatus, then - only when the
   caller wants them - every record into one allocation handed back through the
   out-parameter. InitializeGame calls it with null just to publish the count. */
bool LoadMonsterDatabase(std::unique_ptr<W8MonsterRecord[]>* records)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\Monsters.DBS"), wiz8::OpenMode::read);
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count > std::size(gXStatus.monster_record_cache) ||
        count >
            (handle->size() - handle->tell()) / std::int64_t(wiz8::retail::kMonsterRecordBytes)) {
        return false;
    }
    if (records) {
        auto block = std::make_unique<W8MonsterRecord[]>(count);
        for (unsigned i = 0; i < count; ++i) wiz8::retail::read_exact(*handle, block[i]);
        *records = std::move(block);
    }
    gXStatus.uiMonstersInDatabase = count;
    return true;
} catch (const std::exception&) {
    return false;
}

/* The range sibling of LoadMonsterDatabaseRecord reads an inclusive span. */
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records)
try {
    if (!(uiEndIndex < gXStatus.uiMonstersInDatabase)) {
        Fail("uiEndIndex < gXStatus.uiMonstersInDatabase");
    }
    if (uiStartIndex > uiEndIndex) {
        Fail("uiStartIndex <= uiEndIndex");
    }
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\Monsters.DBS"), wiz8::OpenMode::read);
    handle->seek(std::int64_t(uiStartIndex) * std::int64_t(wiz8::retail::kMonsterRecordBytes) + 4,
                 wiz8::SeekOrigin::begin);
    const auto count = std::size_t(uiEndIndex) - uiStartIndex + 1;
    const auto remaining = handle->size() - handle->tell();
    if (remaining < 0 || count > std::uint64_t(remaining) / wiz8::retail::kMonsterRecordBytes) {
        return false;
    }
    std::vector<W8MonsterRecord> loaded(count);
    for (auto& record : loaded) wiz8::retail::read_exact(*handle, record);
    std::copy(loaded.begin(), loaded.end(), records);
    return true;
} catch (const std::exception&) {
    return false;
}

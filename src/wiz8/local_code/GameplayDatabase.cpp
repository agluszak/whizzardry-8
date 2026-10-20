#include "wiz8/local_code/GameplayDatabase.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "retail_records.h"
#include "wiz8/data/records.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/retail_text_records.h"

namespace {
unsigned int monster_database_count;
} // namespace

unsigned int MonsterDatabaseCount()
{
    return monster_database_count;
}

/* Persistent database roots owned by this translation unit.  Leaving these as
   unresolved externals made the runnable image relocate every load/store to
   the PE image base; the first four-byte count read consequently targeted a
   read-only header instead of game state. */
/* The six item stacks granted by the new-game status reset. */
unsigned int g_starting_item_ids[6] = {0x14f, 0x14f, 0x15a, 0x15a, 0x15b, 0x155};

/* Seeks straight to one record and strips the four name suffixes. */
bool LoadMonsterDatabaseRecord(unsigned int uiMonsterIndex, W8MonsterRecord* record)
try {
    if (!(uiMonsterIndex < monster_database_count)) {
        Fail("uiMonsterIndex < monster_database_count");
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

/* Reads MONSTERS.DBS whole: the count, then - only when the
   caller wants them - every record into one allocation handed back through the
   out-parameter. InitializeGame calls it with null just to publish the count. */
bool LoadMonsterDatabase(std::unique_ptr<W8MonsterRecord[]>* records)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\Monsters.DBS"), wiz8::OpenMode::read);
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count > kMaxMonsterSpecies || count > (handle->size() - handle->tell()) /
                                                  std::int64_t(wiz8::retail::kMonsterRecordBytes)) {
        return false;
    }
    if (records) {
        auto block = std::make_unique<W8MonsterRecord[]>(count);
        for (unsigned i = 0; i < count; ++i) wiz8::retail::read_exact(*handle, block[i]);
        *records = std::move(block);
    }
    monster_database_count = count;
    return true;
} catch (const std::exception&) {
    return false;
}

/* The range sibling of LoadMonsterDatabaseRecord reads an inclusive span. */
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records)
try {
    if (!(uiEndIndex < monster_database_count)) {
        Fail("uiEndIndex < monster_database_count");
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

#pragma once

#include <memory>

struct W8MonsterRecord;

/* Monsters.DBS holds at most this many species; saves and the record cache
   are bounded by it. */
constexpr unsigned int kMaxMonsterSpecies = 1000;

/* Species in the loaded Monsters.DBS. */
unsigned int MonsterDatabaseCount(void);
bool LoadMonsterDatabase(std::unique_ptr<W8MonsterRecord[]>* records);
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records);
bool LoadMonsterDatabaseRecord(unsigned int monster_species, W8MonsterRecord* record);

extern unsigned int g_starting_item_ids[];

bool InitializeItemDatabase(void);
bool InitializeItemTables(void);
unsigned int ItemTableCount(void);
unsigned int ItemTableCategoryCount(void);
void DestroyItemDatabase(void);
void DestroyItemTables(void);

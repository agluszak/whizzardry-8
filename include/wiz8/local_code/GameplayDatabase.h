#pragma once

#include <memory>
#include <vector>

#include "wiz8/data/records.h"

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

// The loaded retail databases; each vector owns its records. Replacing or releasing a
// database invalidates pointers into it.
extern std::vector<W8FactDatabaseRecord> g_fact_records;
extern std::vector<W8LevelDatabaseRecord> g_level_records;
extern std::vector<W8NpcDatabaseRecord> g_npc_records;
extern std::vector<W8SpellRuntimeRecord> g_spell_records;

bool InitializeItemDatabase(void);
bool InitializeItemTables(void);
unsigned int ItemTableCount(void);
unsigned int ItemTableCategoryCount(void);
void DestroyItemDatabase(void);
void DestroyItemTables(void);

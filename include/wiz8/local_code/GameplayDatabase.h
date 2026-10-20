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

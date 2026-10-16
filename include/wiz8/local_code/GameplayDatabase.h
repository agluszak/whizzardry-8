#pragma once

#include <memory>

struct W8MonsterRecord;

bool LoadMonsterDatabase(std::unique_ptr<W8MonsterRecord[]>* records);
bool LoadMonsterDatabaseRange(unsigned int uiStartIndex, unsigned int uiEndIndex,
                              W8MonsterRecord* records);
bool LoadMonsterDatabaseRecord(unsigned int monster_species, W8MonsterRecord* record);

extern unsigned int g_starting_item_ids[];

bool InitializeItemDatabase(void);
bool InitializeItemTables(void);
void DestroyItemDatabase(void);
void DestroyItemTables(void);

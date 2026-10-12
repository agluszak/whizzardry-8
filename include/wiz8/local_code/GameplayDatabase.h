#pragma once


#include <cstdint>
struct W8MonsterRecord;

bool LoadMonsterDatabase(W8MonsterRecord** records);
bool LoadMonsterDatabaseRange(std::uint32_t uiStartIndex, std::uint32_t uiEndIndex,
                              W8MonsterRecord* records);
bool LoadMonsterDatabaseRecord(std::uint32_t monster_species, W8MonsterRecord* record);

extern std::uint32_t g_starting_item_ids[];

bool InitializeItemDatabase(void);
bool InitializeItemTables(void);
void DestroyItemDatabase(void);
void DestroyItemTables(void);
void FreeIfNotNull(void* block);

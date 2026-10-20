#ifndef WIZ8_FACT_STATE_H
#define WIZ8_FACT_STATE_H

#include <cstdint>

#include "wiz8/data/fact_ids.h"

struct W8Chunk;

namespace wiz8 {
class File;
}

/* 1000 entries, indices 0..999, stored at 0x00689B78 and ending exactly at
   g_npc_name_buffer. Retail GetFact/SetFact checked fact_id > 1000, so index 1000
   historically aliases g_npc_name_buffer[0]. Native storage has no such alias. */
extern unsigned char g_fact_values[1000];
extern bool g_fact_notifications_suppressed;

unsigned char GetFact(W8FactId fact_id);
unsigned char EvaluateFact(W8FactId fact_id); /* 0x005080F0 */
void HandleFactChange(W8FactId fact_id, unsigned char value);
/* Scripted consequences of an NPC-bound monster's death; lives in NPC
   Scripting Facts.cpp, driven by MonsterManager's death switch. */
void HandleScriptedNpcDeath(unsigned int monster_list_index); /* 0x00508D70 */
void SetFact(W8FactId fact_id, unsigned char value, bool suppress_side_effects);
void SaveFactState(wiz8::File& save_handle);
void InitializeFactState(void);
void LoadFactState(W8Chunk& save_handle);

void PostNewGameLoad(void);

#endif

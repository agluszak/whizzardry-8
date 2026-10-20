#ifndef WIZ8_FACT_STATE_H
#define WIZ8_FACT_STATE_H

#include <cstdint>

#include "wiz8/data/fact_ids.h"

struct ScriptContext;
struct Story;
struct W8Chunk;
struct GameStatus;

namespace wiz8 {
class File;
}

/* Raised while facts are seeded, so that the journal records the changes without
   posting their notices. */
extern bool g_fact_notifications_suppressed;

unsigned char GetFact(W8FactId fact_id);
unsigned char EvaluateFact(W8FactId fact_id); /* 0x005080F0 */
void HandleFactChange(const ScriptContext& context, W8FactId fact_id, unsigned char value);
/* Scripted consequences of an NPC-bound monster's death; lives in NPC
   Scripting Facts.cpp, driven by MonsterManager's death switch. */
void HandleScriptedNpcDeath(const ScriptContext& context,
                            unsigned int monster_list_index); /* 0x00508D70 */
void SetFact(W8FactId fact_id, unsigned char value, bool suppress_side_effects);
void SaveFactState(const Story& story, wiz8::File& save_handle);
void SeedNewGameFacts(const GameStatus& status);
void LoadFactState(W8Chunk& save_handle);

void PostNewGameLoad(void);

#endif

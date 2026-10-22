#ifndef WIZ8_FACT_STATE_H
#define WIZ8_FACT_STATE_H

#include <array>
#include <cstdint>

#include "wiz8/data/fact_ids.h"

struct ScriptContext;
struct GameStatus;

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
void SeedNewGameFacts(const GameStatus& status);
/* Install a save's facts, then re-apply the consequences that do not survive a save. */
void InstallFacts(const std::array<unsigned char, W8_FACT_SLOT_COUNT>& facts);

void PostNewGameLoad(void);

#endif

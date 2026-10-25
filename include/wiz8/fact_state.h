#ifndef WIZ8_FACT_STATE_H
#define WIZ8_FACT_STATE_H

#include "wiz8/data/fact_ids.h"

struct ScriptContext;

void HandleFactChange(const ScriptContext& context, W8FactId fact_id, unsigned char value);
/* Scripted consequences of an NPC-bound monster's death; lives in NPC
   Scripting Facts.cpp, driven by MonsterManager's death switch. */
void HandleScriptedNpcDeath(const ScriptContext& context,
                            unsigned int monster_list_index); /* 0x00508D70 */

void PostNewGameLoad(void);

#endif

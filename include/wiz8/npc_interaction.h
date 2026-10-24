#pragma once

#include <string>

#include "wiz8/local_code/NPCScripting.h"

/* The NPC-scripting gates NPC Scripting.cpp sets and npc_interaction.cpp
   reads. */

/* Raise / drop the world-cursor gate around a scripted world action. */
void BeginScriptedWorldAction(void);
void ClearMainGameTargetState(void);
/* 0x005294C0: format the bound NPC's quote for one event type into the wide
   output. */
std::string GetNpcQuoteText(W8NpcState* npc, int type);

bool IsPartySlotEligible(int slot);

std::string FormatNpcVoiceSoundPath(W8NpcState* npc, bool script_event = false);

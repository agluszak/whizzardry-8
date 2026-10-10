#include <string>
#include <string_view>
#pragma once

#include "wiz8/local_code/NPCScripting.h"

/* The NPC-scripting gates NPC Scripting.cpp sets and npc_interaction.cpp
   reads. */

bool IsNpcScriptSessionActive(void);
bool ShouldDeferCharacterEventForNpcScript(bool require_group_entry);
/* 0x005294C0: format the bound NPC's quote for one event type into the wide
   output. */
std::string GetNpcQuoteText(W8NpcState* npc, int type);

bool IsPartySlotEligible(int slot);
void ClearNpcMessageQueue(void); /* 0x00524C50 */

std::string FormatNpcVoiceSoundPath(W8NpcState* npc);
void BeginNpcScriptDialogue(W8NpcState* npc, unsigned char preserve_state);
void FinishNpcVoicePlayback(bool resume_script);
int ComputePortraitMessageDuration(std::string_view text);
void UpdateNpcDialogueVoiceIdle(void);

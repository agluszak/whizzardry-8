#pragma once

#include "wiz8/message_box.h"

struct ScriptContext;

/* Quest-specific world actions queued by NPC scripts: endgame and book
   sequences, monster removals and moves, henchmen, and the Al-Sedexus scene. */
void RunNpcStoryAction(const ScriptContext& context, W8NpcStoryAction action);

void BeginNpcScriptedScene(const ScriptContext& context);
void EndScriptedPortraitPick(const ScriptContext& context, int party_slot);
void BeginSedexusCapture(const ScriptContext& context);
bool IsSedexusCaptureActive(void);
/* Advance a pending Al-Sedexus capture or release; runs each scripting frame. */
void UpdateSedexusScene(const ScriptContext& context);
void ResetSedexusScene(void);

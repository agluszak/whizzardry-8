#pragma once

#include "wiz8/message_box.h"

/* Quest-specific world actions queued by NPC scripts: endgame and book
   sequences, monster removals and moves, henchmen, and the Al-Sedexus scene. */
void RunNpcStoryAction(W8NpcStoryAction action);

void BeginNpcScriptedScene(void);
void EndScriptedPortraitPick(int party_slot);
void BeginSedexusCapture(void);

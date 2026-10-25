#pragma once

#include "wiz8/audio_resources.h"
#include "wiz8/message_box.h"

struct ScriptContext;

/* Quest-specific world actions queued by NPC scripts: endgame and book
   sequences, monster removals and moves, henchmen, and the Al-Sedexus scene. */
void RunNpcStoryAction(const ScriptContext& context, W8NpcStoryAction action);

void BeginNpcScriptedScene(const ScriptContext& context);
void EndScriptedPortraitPick(const ScriptContext& context, int party_slot);
void BeginSedexusCapture(const ScriptContext& context);
/* Advance a pending Al-Sedexus capture or release; runs each scripting frame. */
void UpdateSedexusScene(const ScriptContext& context);

/* The Al-Sedexus capture scene. Capture and release are each picked up by
   the next scripting frame that runs in a suitable environment; the capture
   stays active, with the moaning ambience streaming, until the release. */
struct W8SedexusScene {
    bool capture_pending = false;
    bool release_pending = false;
    bool capture_active = false;
    wiz8::sound::Playback moaning;
};

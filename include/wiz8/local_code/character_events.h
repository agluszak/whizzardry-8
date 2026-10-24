#pragma once

struct SDL_Surface;

#include <cstdint>
#include <string>
#include <string_view>

#include "wiz8/audio_resources.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/rules/conditions.h"

bool IsVoiceMuted(void);
/* Audio panels pass 0 or 2 for the enabled setting. */
void SetVoiceMuted(unsigned char muted);

struct W8Character;
struct W8CharacterEvent;
struct W8MonsterManagerEntry;

extern int g_special_event13;
extern int g_special_event14;
extern int g_special_event17;
/* The party's voices, one step: each occupied slot's line finishes when its voice or
   unvoiced hold ends, a member who can no longer speak is cut off, an idle party lets a
   member at zero hit points cry out, and a speaking portrait's mouth follows its voice. */
void AdvancePartyVoices(void);
/* The party portraits' idle animation: each shown portrait moves toward its pose and
   blinks, and a portrait whose pose or mouth changed is redrawn. */
void AnimatePartyPortraits(void);
W8CharacterEvent* QueueCharacterEvent(W8Character* character, int event_type, unsigned int flags,
                                      unsigned int volume);

/* PC quotes and voice samples use character identity, without a party slot. */
std::string GetCharacterQuoteText(const W8Character& character, unsigned int event_type,
                                  unsigned int* metadata = nullptr);
std::string GetCharacterVoicePath(const W8Character& character, unsigned int event_type);

/* First entry of a -1-terminated event-id table. */
extern const int g_fact_check_event;

int PickRandomPartySpeaker(unsigned int event_type, unsigned char excluded_slot);
W8CharacterEvent* ApplyItemEffectToRandomCharacter(unsigned int event_type, int excluded_slot,
                                                   unsigned int flags);
void MaybeStartIncapacitationEvent(unsigned int party_slot);
/* Only one occupied member still standing at turn begin. */
void QueueLastSurvivorEvent(void);
/* Requeue the selected character's stored portrait event. */
void RequeueSelectedPortraitEvent(void);
/* Left-click on a party portrait completes that slot's active
   event, or finishes the NPC voice playback for the player portraits. */
/* Re-blit each active portrait quote bubble when the screen comes
   back from a modal view. */
void RedrawPortraitQuoteBubbles(void);
/* Queue the character's breath/idle event unless a spell or item
   is being aimed; `force` queues it regardless. */
void StartBreathCycle(int party_slot, bool force);
void RenderPartyPortrait(SDL_Surface& target, int portrait, int left, int top, SpriteDraw options,
                         unsigned char value, int party_slot);
/* Blit one animated portrait frame and its transition, returning
   whether a frame was drawn. */
bool BlitPartyPortraitAnimation(SDL_Surface& target, int portrait, int left, int top,
                                SpriteDraw options, int party_slot, bool animate);

extern int g_special_event2;
extern unsigned int g_event_range_max;
extern int g_special_event18; /* One of the three melee
                                         swing event ids StartCharacterAttack
                                         rolls between */
extern int g_special_event15; /* One of the three blocked-hit
                                        reaction ids ContinueMonsterAttack rolls
                                        between */
extern int g_special_event19;
extern int g_special_event20;
extern unsigned int g_event_range_min;
extern int g_special_event5;
extern int g_special_event6;
extern int g_event_target_out_of_range; /* Emitted when a slot's action
                                        cannot reach a monster group */
extern int g_event_sight_blocked;       /* Emitted when the selected
                                        sight line is blocked */
extern int g_special_event8;

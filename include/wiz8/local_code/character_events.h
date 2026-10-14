#pragma once

struct SDL_Surface;

#include <string_view>
#include "wiz8/conditions.h"

#include "input.h"
#include "sprite.h"

bool IsVoiceMuted(void);
/* Audio panels pass 0 or 2 for the enabled setting. */
void SetVoiceMuted(unsigned char muted);

struct W8Character;
struct W8CharacterEvent;
struct W8MonsterManagerEntry;

extern int g_special_event13;
extern int g_special_event14;
extern int g_special_event17;
int UpdateCharacterEventState(void);
W8CharacterEvent* QueueCharacterEvent(W8Character* character, int event_type, unsigned int flags,
                                      int queue_mode, unsigned int volume);

/* Format one character quote for the given event type into the
   shared wide text buffer. Returns zero and empties it when the type has no
   quote. */
bool FormatCharacterQuoteText(W8Character* character, unsigned int event_type,
                              unsigned int* metadata);
/* First entry of a -1-terminated event-id table. */
extern const int g_fact_check_event;

/* True when no occupied party slot has an active portrait/voice record. */
unsigned char PartyPortraitEventsIdle(void);
int PickRandomPartySpeaker(unsigned int event_type, unsigned char excluded_slot);
W8CharacterEvent* ApplyItemEffectToRandomCharacter(unsigned int event_type, int excluded_slot,
                                                   unsigned int flags, int queue_mode);
void MaybeStartIncapacitationEvent(unsigned int party_slot);
void QueueDamageReactionEvents(W8Character* character);
/* After a character dies, pick one other party member and queue
   their reaction event with a three-second clock. */
void QueuePartyDeathReaction(unsigned int party_slot);
/* Several members still active at turn begin; queue the shared
   reaction event on one random eligible character. */
void QueueTurnReactionEvent(void);
/* Only one occupied member still standing at turn begin. */
void QueueLastSurvivorEvent(void);
/* React to a freshly recomputed highest_condition. */
void QueueConditionChangeReaction(W8Character* character);
/* React to a condition being lifted. */
void QueueConditionClearedReaction(W8Character* character, W8Condition condition);
/* Requeue the selected character's stored portrait event. */
void RequeueSelectedPortraitEvent(void);
/* Set the pose a party-slot portrait animates toward; clears any
   pose animation in progress and forces the incapacitated pose when the
   character is too far gone or the party is surprised. */
void SetPortraitTargetPose(struct W8MonsterManagerEntry* slot, int pose);
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

void SetPartyPortraitEventState(unsigned int party_slot, bool active, unsigned int event_type,
                                std::string_view quote_text, int show_quote);
void CreatePartyPortraitEventRegions();
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

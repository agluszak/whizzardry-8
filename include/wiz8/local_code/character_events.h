#pragma once

#include <cstdint>
#include <string_view>
#include "wiz8/audio_resources.h"
#include "wiz8/conditions.h"

#include "input.h"
#include "sprite.h"

bool IsVoiceMuted(void);
/* The audio panels pass the raw W8TextControl mask bit (0 or 2) through. */
void SetVoiceMuted(std::uint8_t muted);

struct W8Character;
struct W8CharacterEvent;
struct W8MonsterManagerEntry;
struct W8Region;

extern std::int32_t g_special_event13;
extern std::int32_t g_special_event14;
extern std::int32_t g_special_event17;
std::int32_t UpdateCharacterEventState(void);
W8CharacterEvent* QueueCharacterEvent(W8Character* character, std::int32_t event_type, std::uint32_t flags,
                                      std::int32_t queue_mode, std::uint32_t volume);

/* Format one character quote for the given event type into the
   shared wide text buffer. Returns zero and empties it when the type has no
   quote. */
bool FormatCharacterQuoteText(W8Character* character, std::uint32_t event_type,
                              std::uint32_t* metadata);
/* First entry of a -1-terminated event-id table. */
extern const std::int32_t g_fact_check_event;

/* True when no occupied party slot has an active portrait/voice record. */
std::uint8_t PartyPortraitEventsIdle(void);
std::int32_t PickRandomPartySpeaker(std::uint32_t event_type, std::uint8_t excluded_slot);
W8CharacterEvent* ApplyItemEffectToRandomCharacter(std::uint32_t event_type, std::int32_t excluded_slot,
                                                   std::uint32_t flags, std::int32_t queue_mode);
void MaybeStartIncapacitationEvent(std::uint32_t party_slot);
void QueueDamageReactionEvents(W8Character* character);
/* After a character dies, pick one other party member and queue
   their reaction event with a three-second clock. */
void QueuePartyDeathReaction(std::uint32_t party_slot);
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
void SetPortraitTargetPose(struct W8MonsterManagerEntry* slot, std::int32_t pose);
/* Left-click on a party portrait completes that slot's active
   event, or finishes the NPC voice playback for the player portraits. */
std::uint8_t PartyPortraitEventRegionEvent(const InputEvent* event, struct W8Region* region);
/* Re-blit each active portrait quote bubble when the screen comes
   back from a modal view. */
void RedrawPortraitQuoteBubbles(void);
/* Queue the character's breath/idle event unless a spell or item
   is being aimed; `force` queues it regardless. */
void StartBreathCycle(std::int32_t party_slot, bool force);
void RenderPartyPortrait(std::int32_t portrait, std::int32_t left, std::int32_t top, SpriteDraw options, std::uint8_t value,
                         std::int32_t party_slot);
/* Blit one animated portrait frame and its transition, returning
   whether a frame was drawn. */
bool BlitPartyPortraitAnimation(std::int32_t portrait, std::int32_t left, std::int32_t top, SpriteDraw options, std::int32_t party_slot,
                                bool animate);

/* Drive `party_slot`'s portrait from `voice`: its remaining time and mouth
   gaps follow the playback, or the portrait holds for `unvoiced_ms` when no
   voice started. Character events and NPC party members both speak this way. */
void BeginPortraitVoice(std::uint32_t party_slot, const wiz8::sound::Playback& voice,
                        std::string_view voice_path, std::uint32_t unvoiced_ms);
/* The portrait's line is over: complete the character event speaking through
   it, or just close the portrait when the speaker was an NPC line. */
void FinishPortraitSpeech(std::uint32_t party_slot);
void SetPartyPortraitEventState(std::uint32_t party_slot, bool active, std::uint32_t event_type,
                                std::string_view quote_text, std::int32_t show_quote);
void CreatePartyPortraitEventRegions();
extern std::int32_t g_special_event2;
extern std::uint32_t g_event_range_max;
extern std::int32_t g_special_event18; /* One of the three melee
                                         swing event ids StartCharacterAttack
                                         rolls between */
extern std::int32_t g_special_event15; /* One of the three blocked-hit
                                        reaction ids ContinueMonsterAttack rolls
                                        between */
extern std::int32_t g_special_event19;
extern std::int32_t g_special_event20;
extern std::uint32_t g_event_range_min;
extern std::int32_t g_special_event5;
extern std::int32_t g_special_event6;
extern std::int32_t g_event_target_out_of_range; /* Emitted when a slot's action
                                        cannot reach a monster group */
extern std::int32_t g_event_sight_blocked;       /* Emitted when the selected
                                        sight line is blocked */
extern std::int32_t g_special_event8;

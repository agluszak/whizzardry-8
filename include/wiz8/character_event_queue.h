#ifndef WIZ8_CHARACTER_EVENT_QUEUE_H
#define WIZ8_CHARACTER_EVENT_QUEUE_H

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/audio_resources.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/item_instance.h"

struct W8Character;

/* W8CharacterEvent::flags bits. Bit 0x01 is written by several producers but
   never tested anywhere in retail. */
enum W8CharacterEventFlag {
    /* Dispatch skips the eligibility and condition checks entirely. */
    W8_EVENT_BYPASS_CHECKS = 0x04,
    /* QueueEntry never diverts the event to npc_deferred_events while NPC
       scripting is active. */
    W8_EVENT_NO_NPC_DEFER = 0x08,
    /* The portrait quote/subtitle is suppressed; the dispatch path tests it
       through the kCharacterEventFlagsMask global. */
    W8_EVENT_SUPPRESS_QUOTE = 0x10,
    /* Events above the ordinary range normally preempt the slot's active
       event and dispatch immediately; this bit queues them normally. */
    W8_EVENT_NO_PREEMPT = 0x20,
    /* The event is an NPC-script line: Dispatch reloads the NPC's script
       resources around W8NpcDialogue::Speak and Complete skips the item notice. */
    W8_EVENT_NPC_SCRIPT = 0x40
};

/* One queued character-event entry. The constructor, quote formatter and
   dispatch path live with QueueCharacterEvent in QuoteManager.cpp; the
   original type name is unknown. */
struct W8CharacterEvent {
    W8CharacterEvent(W8Character* character, unsigned int event_type, unsigned int flags,
                     int volume);
    ~W8CharacterEvent();
    W8CharacterEvent(const W8CharacterEvent&) = delete;
    W8CharacterEvent& operator=(const W8CharacterEvent&) = delete;

    W8Character* character;
    unsigned int event_type;
    /* 0x10: event flags passed as the third argument. */
    unsigned int flags;
    /* 0x14: the 0-127 multiplier applied to the voice volume. */
    int volume;
    /* 0x18/0x1c: thresholds sampled at queue time (hp_current or
       highest_condition, by event type); the consumer fires once the live
       value crosses them. */
    int trigger_value;
    int trigger_value_2;
    /* 0x20: the original event type TryAdjustQueuedEvent stores before
       rewriting event_type to the shared follow-up id 10. */
    unsigned int original_event_type = 0;
    /* 0x24: an embedded item instance: the event consumer passes it to
       GetItemDisplayName, which reads both the id and the identified flag.
       Only the id is ever established here; the remaining bytes keep whatever
       the allocation gave them, exactly as before. */
    W8ItemInstance item;
    /* 0x30/0x34: ProcessDeferredCharacterEvents delays dispatch until this many
       milliseconds have elapsed since the queued w8_get_ticks stamp. */
    int dispatch_delay_ms;
    uint64_t dispatch_delay_start;

    /* The event owns its voice; the portrait's shared handle can change
       independently of the event lifetime. */
    wiz8::sound::Playback voice_playback;

    void DelayDispatch(int milliseconds)
    {
        dispatch_delay_ms = milliseconds;
        dispatch_delay_start = w8_get_ticks();
    }

    bool IsAmbientExchange() const
    {
        const auto type = event_type == 10 ? original_event_type : event_type;
        return type == 14 || type == 15;
    }

    void Complete(); /* 0x0052CED0 */

    unsigned char Dispatch(); /* 0x0052CA60 */

    unsigned char IsConditionMet(unsigned int event_type); /* 0x0052C910 */
    unsigned char PlayEventSound(std::string_view quote);                        /* 0x0052D260 */
};

/* The character-event queue. Construction is at 0x0052D460; QueueEntry and the
   later methods occupy the following 0x52Dxxx block. The whole 0x52C810-0x52FEE0
   span sits inside Local Code\Health Stamina Mana.cpp's emission range: 0x52C500
   asserts that file's name before it and 0x52FE80 is placed after it, so the
   bodies live in Health Stamina Mana.cpp. */
struct W8CharacterEventQueue {
    /* The queue owns every event it accepts: pending and NPC-deferred entries
       until they dispatch or are dropped, dispatched ones until they
       complete. */
    std::vector<std::unique_ptr<W8CharacterEvent>> pending_events;
    std::vector<std::unique_ptr<W8CharacterEvent>> npc_deferred_events;
    std::vector<std::unique_ptr<W8CharacterEvent>> active_events;
    int active_event_type;
    int active_party_slot;
    /* 0x58: a five-second countdown armed by every successful Dispatch; while
       it ticks, active_event_type/active_party_slot still describe the last
       dispatched event for duplicate coalescing. */
    uint64_t recent_event_clock;
    // No schedule means idle dialogue is disabled. Queue membership supplies
    // the wait for playback; this stores only the next speaker and deadline.
    struct FollowUp {
        int excluded_slot = -1;
        uint64_t ready_at = 0;
    };
    std::optional<FollowUp> follow_up;
    /* One party-slot bit mask per (remapped) event type. */
    std::array<unsigned char, 0xb1> event_character_masks{};

    W8CharacterEventQueue();
    ~W8CharacterEventQueue();
    W8CharacterEventQueue(const W8CharacterEventQueue&) = delete;
    W8CharacterEventQueue& operator=(const W8CharacterEventQueue&) = delete;
    void DestroyAllEvents();
    /* Takes ownership of `entry`. Returns the event while the queue still
       holds it, so the caller may adjust it, or null when it was dropped. */
    W8CharacterEvent* QueueEntry(std::unique_ptr<W8CharacterEvent> entry);
    /* Descriptive name for the successful-dispatch bookkeeping operation. */
    void RecordDispatchedEvent(unsigned int event_type, unsigned int party_slot);
    void SetEventCharacterMask(unsigned int event_type, unsigned int party_slot, bool enabled);
    bool HasEventCharacter(unsigned int event_type, unsigned int party_slot); /* 0x0052DD90 */
    void ProcessDeferredCharacterEvents();                                    /* 0x0052DDD0 */
    unsigned char TryAdjustQueuedEvent(W8CharacterEvent* entry);              /* 0x0052DC80 */
    bool IsMainQueueEmpty() const;                                            /* 0x0052E470 */
    /* Unlink an active event, restart the follow-up clock for it, complete it
       and release it. Retail expands this body in the dispatch helpers as well
       as retaining the emission. */
    void CompleteActiveEvent(W8CharacterEvent* entry); /* 0x0052D8D0 */
    void CompleteFirstActiveEvent();
    void RestartFollowUpClock(W8CharacterEvent* entry);
    /* Removes every queued event belonging to a character; active ones are
       completed first. Runs when a character dies. */
    void RemoveCharacterEvents(W8Character* character); /* 0x0052D970 */
    void CompleteAllActiveEvents();                     /* 0x0052DB30 */
    bool HasActiveEvents();                             /* 0x0052E460 */
    /* Advances the ambient follow-up exchange: arms it, then on each clock
       expiry queues another middle-band event on a random member, excluding
       the previous speaker. */
    void ProcessFollowUpEvents(); /* 0x0052E1C0 */
};

#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "asset_paths.h"
#include "audio_resources.h"
#include "layouts/main_game_screen.h"
#include "message_box.h"
#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/records.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_speech.h"
#include "wiz8/platform/timer.h"
#include "wiz8/portrait_animation.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/string_database.h"
#include "wiz8/ui_layer.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"

constexpr unsigned int kPortraitLowHealthPercent = 20;
constexpr unsigned int kRemappedEventCount = 31;

/* TU attribution follows the demo __FILE__ and quote-personality strings. */

/* Re-blit each active portrait quote bubble onto the game surface. Runs when
   the screen comes back from a modal view; slots whose character is dead or
   too far gone to be speaking keep their bubble down. */
void RedrawPortraitQuoteBubbles(void)
{
    unsigned int party_slot;
    W8MonsterManagerEntry* slot;

    IsModalOpen();
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        slot = &CurrentGame().monster_manager_entries[party_slot];
        if (!CurrentGame().party.members[party_slot].has_value() ||
            CurrentGame().party.characters[party_slot].hp_current <= 0 ||
            CurrentGame().party.characters[party_slot].highest_condition >= W8_CONDITION_ASLEEP ||
            !slot->portrait_event_active) {
            continue;
        }
        if (slot->quote.bubble)
            slot->quote.bubble->draw(GetFrameBufferSurface(), slot->quote.x, slot->quote.y);
    }
}

void StartBreathCycle(int party_slot, bool force)
{
    if ((!gpSCSV && !ItemUseDraft()) || force) {
        QueueCharacterEvent(&CurrentGame().party.characters[party_slot], g_special_event2, 0,
                            kCharacterEventFullVolume);
    }
}

/* Collect every occupied living slot other than `excluded_slot` whose
   character can actually speak `event_type`, then pick one at random. */
int PickRandomPartySpeaker(unsigned int event_type, unsigned char excluded_slot)
{
    int eligible[8];
    unsigned int count = 0;

    for (int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() && slot != excluded_slot &&
            character->hp_current != 0 && character->highest_condition < W8_CONDITION_ASLEEP &&
            !GetPartyQuoteText(*character, event_type).empty()) {
            eligible[count++] = slot;
        }
    }
    if (count != 0) {
        return eligible[CurrentGame().rng.below(count)];
    }
    return -1;
}

/* Character-event subsystem: the event descriptor table, event object, and the
   five-vector dispatch queue. */

static int g_special_event21 = kFirstRemappedEvent;
unsigned int g_event_range_min = kFirstRemappedEvent + 21;
/* The quote file-name stem per personality, a twenty-byte fixed
   buffer each. The nine personas end exactly at the next global; the quote
   lookup composes Data\Quotes\PCs\<m|f>_<stem><1|2>0.MSG from them. */
const char g_quote_personality_names[9][0x14] = {
    "aggr", "intell", "burly", "chaos", "cun", "ecc", "kind", "laid", "loner",
};
unsigned int g_event_range_max = kFirstRemappedEvent + 23;
int g_trap_notice_event = kFirstRemappedEvent + 12;
int g_lock_notice_event = kFirstRemappedEvent + 11;
/* Per-slot quote coordinates, followed by the five-by-five pose transition
   table. Pose IDs are one-based. The table ends at 0x0061CBC0, where the
   separately owned byte-per-portrait animation flags begin. */
static unsigned short g_portrait_quote_x[8] = {
    0x0080, 0x0138, 0x0080, 0x0138, 0x0080, 0x0138, 0x0080, 0x0138,
};
static unsigned short g_portrait_quote_y[8] = {
    0x0013, 0x0013, 0x0067, 0x0067, 0x00bc, 0x00bc, 0x0111, 0x0111,
};
static int g_portrait_pose_transition[5][5] = {
    {1, 3, 3, 4, 5}, {3, 2, 3, 3, 3}, {1, 2, 3, 4, 1}, {1, 3, 3, 4, 1}, {1, 1, 1, 1, 5},
};
const int g_fact_check_event = 129;
const unsigned int g_normal_event_count = 146; /* ordinary-event count */

/* Eight-byte dispatch records indexed by remapped event type.
   The table ends at 0x005EE588 where 0 begins. TryAdjustQueuedEvent
   reads coalesce_duplicates at +4; ProcessDeferredCharacterEvents reads
   defer_outside_main_game at +5. */
struct W8CharacterEventDescriptor {
    int portrait_pose_category;
    /* A queued copy may be rewritten to the shared follow-up event (10)
       when another character already has the same event active. */
    unsigned char coalesce_duplicates;
    unsigned char defer_outside_main_game;
    /* When the portrait quote closes, its text is posted to the notice
       log instead of being dropped silently. */
    unsigned char log_quote_on_finish;
    unsigned char unknown_07;
};
const W8CharacterEventDescriptor g_character_event_descriptors[0xb1] = {
    {0x00000001, 0x00, 0x00, 0x00, 0x00}, {0x00000005, 0x00, 0x01, 0x00, 0x00},
    {0x00000004, 0x00, 0x01, 0x00, 0x00}, {0x00000002, 0x00, 0x01, 0x00, 0x00},
    {0x00000003, 0x01, 0x01, 0x00, 0x00}, {0x00000003, 0x01, 0x01, 0x00, 0x00},
    {0x00000003, 0x01, 0x01, 0x00, 0x00}, {0x00000003, 0x01, 0x01, 0x00, 0x00},
    {0x00000001, 0x01, 0x00, 0x00, 0x00}, {0x00000001, 0x01, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000005, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000004, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000004, 0x00, 0x01, 0x00, 0x00}, {0x00000004, 0x00, 0x01, 0x00, 0x00},
    {0x00000004, 0x00, 0x01, 0x00, 0x00}, {0x00000004, 0x00, 0x01, 0x00, 0x00},
    {0x00000004, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000004, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000005, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000005, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000005, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000005, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x00, 0x00, 0x00},
    {0x00000004, 0x00, 0x01, 0x00, 0x00}, {0x00000005, 0x00, 0x00, 0x00, 0x00},
    {0x00000005, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000005, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x00, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x01, 0x00}, {0x00000001, 0x00, 0x01, 0x01, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000004, 0x00, 0x01, 0x00, 0x00},
    {0x00000005, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00}, {0x00000001, 0x00, 0x01, 0x00, 0x00},
    {0x00000001, 0x00, 0x01, 0x00, 0x00},
};

static int g_special_event0 = kFirstRemappedEvent + 9;
static int g_special_event1 = kFirstRemappedEvent + 10;
int g_special_event2 = kFirstRemappedEvent + 5;
static int g_special_event3 = kFirstRemappedEvent + 1;
static int g_special_event4 = kFirstRemappedEvent + 18;
int g_special_event5 = kFirstRemappedEvent + 27;
int g_special_event6 = kFirstRemappedEvent + 28;
static int g_special_event7 = kFirstRemappedEvent + 2;
int g_event_target_out_of_range = kFirstRemappedEvent + 29;
int g_special_event8 = kFirstRemappedEvent + 4;
static int g_special_event9 = kFirstRemappedEvent + 8;
static int g_special_event11 = kFirstRemappedEvent + 7;
static int g_special_event10 = kFirstRemappedEvent + 19;
static int g_special_event12 = kFirstRemappedEvent + 17;
int g_special_event13 = kFirstRemappedEvent + 15;
int g_special_event14 = kFirstRemappedEvent + 3;
int g_special_event15 = kFirstRemappedEvent + 25;
static int g_special_event16 = kFirstRemappedEvent + 20;
int g_special_event17 = kFirstRemappedEvent + 6;
int g_special_event18 = kFirstRemappedEvent + 22;
int g_special_event19 = kFirstRemappedEvent + 24;
int g_special_event20 = kFirstRemappedEvent + 26;

static bool MapEventTypeToDescriptorIndex(unsigned int event_type, unsigned int* descriptor_index)
{
    if (event_type < g_normal_event_count) {
        *descriptor_index = event_type;
        return true;
    }
    if (event_type >= kFirstRemappedEvent &&
        event_type < kFirstRemappedEvent + kRemappedEventCount) {
        *descriptor_index = g_normal_event_count + event_type - kFirstRemappedEvent;
        return true;
    }
    return false;
}

/* Character-event queue and portrait/voice updates. The original
   translation-unit name is unknown; the existing unit is retained intact. */

bool IsVoiceMuted(void)
{
    return g_settings.muted_voice_volume != 0xff;
}

void SetVoiceMuted(unsigned char muted)
{
    if (muted != 0) {
        if (g_settings.muted_voice_volume == 0xff) {
            g_settings.muted_voice_volume = g_settings.voice_volume;
            g_settings.voice_volume = 0;
        }
    } else if (g_settings.muted_voice_volume != 0xff) {
        g_settings.voice_volume = g_settings.muted_voice_volume;
        g_settings.muted_voice_volume = 0xff;
    }
    wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::voice, g_settings.voice_volume);
}

static void CharacterEventSoundEndCallback(W8CharacterEvent* entry)
{
    // A voice can finish after its game has ended.
    if (GameInProgress()) CurrentGame().speech.CompleteActiveEvent(entry);
}

W8CharacterEvent::W8CharacterEvent(W8Character* character, unsigned int event_type,
                                   unsigned int flags, int volume)
    : character(character), event_type(event_type), flags(flags), volume(volume)
{
    item.iItemNo = -1;
    switch (event_type) {
    case 2:
    case 3:
        trigger_value = character->hp_current;
        break;
    case 5:
    case 6:
    case 9:
    case 0x54:
        trigger_value = character->highest_condition;
        break;
    case 7:
        trigger_value = 12;
        break;
    case 0x38:
    case 0x55:
        trigger_value = character->hp_current;
        trigger_value_2 = character->highest_condition;
        break;
    }
}

W8CharacterEvent::~W8CharacterEvent()
{
    wiz8::sound::stop(voice_playback);
}

std::string GetCharacterQuoteText(const W8Character& character, unsigned int event_type,
                                  unsigned int* metadata)
{
    if (metadata) *metadata = 0xffffffff;
    if (event_type >= 0x92) return {};
    std::string text;
    char gender_code = static_cast<char>(((character.gender != W8_GENDER_MALE) - 1U & 7) + 0x66);
    std::string path =
        fmt::format("Data\\Quotes\\PCs\\{:c}_{}{:d}0.MSG", gender_code,
                    g_quote_personality_names[character.personality], (character.voice != 0) + 1);
    if (![&]() {
            const auto status = wiz8::file_status(wiz8::asset_path(path));
            return status && status->info.type == SDL_PATHTYPE_FILE;
        }()) {
        return {};
    }
    text = GetStringFromStringDatabase(path, event_type, 0, metadata);
    // The quote records include a trailing terminator character.
    if (!text.empty()) text.resize(wiz8::text::previous(text, text.size()));
    return text.empty() ? std::string{} : fmt::format("\"{}\"", text);
}

// Party RPCs use their NPC quote database; character creation uses PC identity.
std::string GetPartyQuoteText(const W8Character& character, unsigned int event_type)
{
    if (event_type >= 0x92) return {};
    if (character.fInParty && CurrentScreenState().id != W8_SCREEN_CHARACTER) {
        const int slot = CharacterPointerToPartySlot(&character);
        const int npc_index = CurrentGame().party.npcIndex(slot);
        if (npc_index != -1) {
            const auto text = GetNpcQuoteText(GetNpcState(npc_index), event_type);
            return text.empty() ? std::string{} : fmt::format("\"{}\"", text);
        }
    }
    return GetCharacterQuoteText(character, event_type);
}

W8CharacterEventQueue::W8CharacterEventQueue() : active_event_type(-1), active_party_slot(-1) {}

void W8CharacterEvent::DelayDispatch(std::chrono::milliseconds delay)
{
    dispatch_delay = delay;
    dispatch_delay_start = CurrentGame().time;
}

static void ReleasePartyQuoteInput();
W8CharacterEventQueue::~W8CharacterEventQueue()
{
    DestroyAllEvents();
    ReleasePartyQuoteInput();
}

/* Completes every active event, then drops the undispatched ones. */
void W8CharacterEventQueue::DestroyAllEvents()
{
    CompleteAllActiveEvents();
    npc_deferred_events.clear();
    pending_events.clear();
}

void W8CharacterEventQueue::CompleteActiveEvent(W8CharacterEvent* entry)
{
    const auto owner =
        std::ranges::find(active_events, entry, &std::unique_ptr<W8CharacterEvent>::get);
    if (owner == active_events.end()) {
        return;
    }
    const std::unique_ptr<W8CharacterEvent> completed = std::move(*owner);
    active_events.erase(owner);
    RestartFollowUpClock(completed.get());
    completed->Complete();
}

void W8CharacterEventQueue::RemoveCharacterEvents(W8Character* character)
{
    const auto belongs_to_character = [character](const auto& entry) {
        return entry->character == character;
    };
    /* Unlink the character's active events before completing them, so
       completion never observes a half-updated list. */
    std::vector<std::unique_ptr<W8CharacterEvent>> removed;
    for (auto& entry : active_events) {
        if (belongs_to_character(entry)) {
            removed.push_back(std::move(entry));
        }
    }
    std::erase(active_events, nullptr);
    for (const auto& entry : removed) {
        entry->Complete();
    }
    std::erase_if(pending_events, belongs_to_character);
    std::erase_if(npc_deferred_events, belongs_to_character);
}

void W8CharacterEventQueue::CompleteAllActiveEvents()
{
    while (!active_events.empty()) {
        const std::unique_ptr<W8CharacterEvent> entry = std::move(active_events.front());
        active_events.erase(active_events.begin());
        entry->Complete();
    }
}

void W8CharacterEventQueue::CompleteFirstActiveEvent()
{
    if (!active_events.empty()) {
        CompleteActiveEvent(active_events.front().get());
    }
}

void W8CharacterEvent::Complete()
{
    W8MonsterManagerEntry* slot;
    int party_slot;
    bool sound_was_active;

    party_slot = CharacterPointerToPartySlot(character);
    slot = &CurrentGame().monster_manager_entries[party_slot];
    sound_was_active = slot->portrait_event_active;
    wiz8::sound::stop(voice_playback);
    slot->active_character_event = 0;
    if (sound_was_active) {
        SetPartyPortraitEventState(party_slot, false, -1, {}, 1);
    }
    slot->mouth_gap = {};
    if (event_type == 23 || event_type == 24) {
        if ((flags & W8_EVENT_NPC_SCRIPT) == 0) {
            if (item.iItemNo == -1) {
                PostCharacterNotice(party_slot, Data().strings[0x771]);
            } else {
                PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x772],
                                                             GetItemDisplayName(Data(), &item)));
            }
        }
    } else if (event_type == 51) {
        CurrentGame().npc_dialogue.Queue(W8NpcMarkMissing{party_slot});
    }
}

bool W8CharacterEvent::IsConditionMet(unsigned int event_type)
{
    do {
        switch (event_type) {
        case 2:
        case 3:
            return static_cast<unsigned int>(trigger_value) > character->hp_current;
        case 5:
        case 6:
        case 7:
        case 9:
            return character->highest_condition == static_cast<unsigned int>(trigger_value);
        case 10:
            event_type = original_event_type;
            break;
        case 14:
        case 15:
        case 35:
            return !CurrentGame().combat.has_value();
        case 43:
        case 44:
        case 45:
            return character->gender != W8_GENDER_FEMALE;
        case 56:
            return character->hp_current >= static_cast<unsigned int>(trigger_value) &&
                   character->highest_condition <= static_cast<unsigned int>(trigger_value_2);
        case 84:
            return static_cast<unsigned int>(trigger_value) > character->highest_condition;
        case 85:
            if (character->hp_current < static_cast<unsigned int>(trigger_value) ||
                character->highest_condition != W8_CONDITION_NONE) {
                QueueCharacterEvent(character, 84, 0, 0x7f);
                return false;
            }
            return true;
        default:
            return true;
        }
    } while (1);
}

static bool CanDispatchCharacterEvent(unsigned int party_slot, unsigned int event_type)
{
    W8Character* character;
    unsigned int mapped_event_type;
    unsigned char slot_mask;

    if (party_slot >= 8) {
        return false;
    }
    if (event_type >= g_normal_event_count) {
        if (event_type < kFirstRemappedEvent ||
            event_type >= kRemappedEventCount + kFirstRemappedEvent) {
            return false;
        }
    }
    if (!CurrentGame().party.members[party_slot].has_value()) {
        return false;
    }
    character = &CurrentGame().party.characters[party_slot];
    if (character->highest_condition > W8_CONDITION_WEBBED) {
        if (event_type == static_cast<unsigned int>(g_special_event9) ||
            event_type == static_cast<unsigned int>(g_special_event10) || g_special_event16 != 0) {
            if (character->uiCondition[W8_CONDITION_UNCONSCIOUS] != 0 ||
                character->uiCondition[W8_CONDITION_MISSING] != 0) {
                return false;
            }
        } else {
            if (character->highest_condition != W8_CONDITION_UNCONSCIOUS &&
                character->highest_condition != W8_CONDITION_ASLEEP) {
                return false;
            }
            if (event_type != static_cast<unsigned int>(g_special_event11) &&
                event_type != static_cast<unsigned int>(g_special_event12) &&
                event_type != static_cast<unsigned int>(g_special_event4)) {
                return false;
            }
        }
    }
    if (character->hp_current == 0 && event_type != static_cast<unsigned int>(g_special_event9)) {
        return false;
    }
    mapped_event_type = event_type;
    if (mapped_event_type >= kFirstRemappedEvent) {
        mapped_event_type += g_normal_event_count - kFirstRemappedEvent;
    }
    slot_mask = static_cast<unsigned char>(1 << (party_slot & 31));
    return (CurrentGame().speech.event_character_masks[mapped_event_type] & slot_mask) == 0;
}

std::string GetCharacterVoicePath(const W8Character& character, unsigned int event_type)
{
    char gender_code = static_cast<char>(((character.gender != W8_GENDER_MALE) - 1U & 7) + 0x66);
    std::string voice_stem =
        fmt::format("{:c}_{}{:d}0", gender_code, g_quote_personality_names[character.personality],
                    character.voice + 1);
    return fmt::format("Data\\Sound\\PCs\\{}\\{}_{:03d}.wav", voice_stem, voice_stem, event_type);
}

/* What a character says in place of a missing voice sample. */
#define FALLBACK_VOICE_TEXT "Ouch play this sound."

bool W8CharacterEvent::PlayEventSound(std::string_view quote)
{
    unsigned int party_slot = CharacterPointerToPartySlot(character);
    unsigned int sound_event = event_type;
    int npc_index = CurrentGame().party.npcIndex(party_slot);
    std::string sound_path;
    std::string npc_sound_name;
    W8NpcState* npc;

    if (character->uiCondition[W8_CONDITION_SILENCED] != 0) {
        sound_event = g_special_event1;
    }
    if (npc_index == -1 || !GameStarted() || CurrentScreenState().id == W8_SCREEN_CHARACTER) {
        sound_path = GetCharacterVoicePath(*character, sound_event);
    } else {
        npc = GetNpcState(npc_index);
        if (npc != 0) {
            npc_sound_name = FormatNpcVoiceSoundPath(npc);
            sound_path = fmt::format("Data\\Sound\\PCs\\{}\\{}_{:03d}.wav", npc_sound_name,
                                     npc_sound_name, sound_event);
        }
    }
    wiz8::sound::stop(voice_playback);
    voice_playback = wiz8::sound::play(
        sound_path, {.volume = std::uint32_t(volume),
                     .channel = wiz8::sound::VolumeChannel::voice,
                     .on_finished = [this] { CharacterEventSoundEndCallback(this); }});
    BeginPortraitVoice(party_slot, voice_playback, sound_path,
                       ComputePortraitMessageDuration(
                           event_type > 0x91 ? std::string_view(FALLBACK_VOICE_TEXT) : quote));
    return true;
}

void W8CharacterEventQueue::RecordDispatchedEvent(unsigned int event_type, unsigned int party_slot)
{
    if (event_type > 1 && (event_type < 4 || event_type == 0x1c)) {
        SetEventCharacterMask(event_type, party_slot, true);
    }
    if (event_type != 10) {
        active_event_type = event_type;
        active_party_slot = party_slot;
    }
    recent_event_until = CurrentGame().time + std::chrono::seconds{5};
}

bool W8CharacterEvent::Dispatch()
{
    unsigned int party_slot;
    unsigned int descriptor_index;
    W8MonsterManagerEntry* slot;
    W8PartySlotRow* row;
    int npc_index;
    W8NpcState* npc;
    std::string quote;

    if (character == 0) {
        return false;
    }
    party_slot = CharacterPointerToPartySlot(character);
    if (!MapEventTypeToDescriptorIndex(event_type, &descriptor_index)) {
        return false;
    }
    if ((flags & W8_EVENT_BYPASS_CHECKS) == 0) {
        if (!CanDispatchCharacterEvent(party_slot, event_type) || IsConditionMet(event_type) == 0) {
            goto finish_without_dispatch;
        }
        if (event_type != static_cast<unsigned int>(g_special_event21) &&
            event_type != static_cast<unsigned int>(g_special_event1)) {
            if (character->uiCondition[W8_CONDITION_SILENCED] != 0) {
                QueueCharacterEvent(character, g_special_event1, 0, 0x7f);
                return false;
            }
            if (character->uiCondition[W8_CONDITION_INSANE] != 0) {
                QueueCharacterEvent(character, g_special_event21, 0, 0x7f);
                return false;
            }
        }
    }
    slot = &CurrentGame().monster_manager_entries[party_slot];
    if (!slot->portrait_event_active) {
        if (event_type == 0x33) {
            SetNpcDialoguePanelVisible(0);
        }
        row = &CurrentGame().party.rows[party_slot];
        npc_index = CurrentGame().party.npcIndex(party_slot);
        if (npc_index != -1 && event_type < 0x93) {
            npc = GetNpcState(npc_index);
            row->pending_event_type = event_type;
            if (npc == 0) {
                return true;
            }
            if (npc->name_style == W8_NPC_VI_DOMINA && event_type > 0x8b && event_type < 0x92 &&
                GetLoadedLevelID() != 0) {
                return false;
            }
            CurrentGame().npc_dialogue.Begin(npc, true, (flags & W8_EVENT_NPC_SCRIPT) != 0);
            CurrentGame().npc_dialogue.Speak(MakeScriptContext(CurrentGame()), event_type,
                                             (flags & W8_EVENT_NPC_SCRIPT) != 0);
            slot->active_character_event = this;
            CurrentGame().speech.RecordDispatchedEvent(event_type, party_slot);
            return true;
        }
        quote = GetPartyQuoteText(*character, event_type);
        if (PlayEventSound(quote) != 0) {
            CurrentGame().speech.RecordDispatchedEvent(event_type, party_slot);
            if (event_type < 0x92) {
                SetPartyPortraitEventState(party_slot, true, event_type, quote,
                                           1 - ((flags & kCharacterEventFlagsMask) != 0));
                slot->active_character_event = this;
                row->pending_event_type = event_type;
                slot->pending_event_type = event_type;
                return true;
            }
            SetPartyPortraitEventState(party_slot, true, event_type, {}, 1);
            slot->pending_event_type = event_type;
            return true;
        }
        Complete();
        if (quote.empty()) {
            return false;
        }
        CurrentGame().speech.RecordDispatchedEvent(event_type, party_slot);
        CurrentGame().speech.RestartFollowUpClock(this);
        return false;
    }
finish_without_dispatch:
    Complete();
    return false;
}

namespace {
struct PartyQuoteInput {
    UiLayer input{UiLayerOrder::PartyQuotes, UiLayerScope::Shared};
    std::array<HitTarget*, 8> targets;
    PartyQuoteInput()
    {
        for (int slot = 0; slot < 8; ++slot) {
            targets[slot] = &input.add(SDL_Rect{0, 0, 1, 1}, [slot](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                if (event.action == UiPointerAction::Up && event.inside &&
                    event.button == UiPointerButton::Left &&
                    (event.captured_buttons & SDL_BUTTON_LMASK)) {
                    AfterUiDispatch([slot] {
                        auto* active =
                            CurrentGame().monster_manager_entries[slot].active_character_event;
                        if (slot > 1 && active)
                            CurrentGame().speech.CompleteActiveEvent(active);
                        else
                            TryFinishNpcVoicePlayback(true);
                    });
                }
                return UiResult::Handled;
            });
            targets[slot]->setEnabled(false);
        }
    }
};
// Quotes persist while switching between camp and the live game.
std::unique_ptr<PartyQuoteInput> party_quote_input;
}

static void ReleasePartyQuoteInput()
{
    party_quote_input.reset();
}

void CreatePartyPortraitEventRegions()
{
    if (!party_quote_input) party_quote_input = std::make_unique<PartyQuoteInput>();
}

void SetPartyPortraitEventBounds(unsigned int slot, unsigned short x, unsigned short y,
                                 unsigned short right, unsigned short bottom)
{
    CreatePartyPortraitEventRegions();
    if (slot < 8)
        party_quote_input->targets[slot]->setShape(SDL_Rect{x, y, right - x + 1, bottom - y + 1});
}

void EnablePartyPortraitEvent(unsigned int slot)
{
    CreatePartyPortraitEventRegions();
    party_quote_input->input.setEnabled(true);
    if (slot < 8) party_quote_input->targets[slot]->setEnabled(true);
}

void DisablePartyPortraitEvent(unsigned int slot)
{
    if (party_quote_input && slot < 8) party_quote_input->targets[slot]->setEnabled(false);
}

void BeginPortraitVoice(unsigned int party_slot, const wiz8::sound::Playback& voice,
                        std::string_view voice_path, std::uint64_t unvoiced_ms)
{
    W8MonsterManagerEntry& record = CurrentGame().monster_manager_entries[party_slot];
    record.voice_sound_handle = voice;
    if (voice == nullptr) {
        record.portrait.voice_remaining_ms = unvoiced_ms;
        return;
    }
    record.portrait.voice_remaining_ms =
        wiz8::sound::playback_time(voice).value_or(wiz8::sound::PlaybackTime{}).total_ms;
    record.mouth_gap = LoadMouthGapTrack(voice_path);
}

void FinishPortraitSpeech(unsigned int party_slot)
{
    W8MonsterManagerEntry& record = CurrentGame().monster_manager_entries[party_slot];
    if (record.active_character_event == 0) {
        SetPartyPortraitEventState(party_slot, false, -1, {}, 1);
    } else {
        CurrentGame().speech.CompleteActiveEvent(record.active_character_event);
    }
}

/* Turn a party-slot portrait/voice event on or off, optionally
   laying out the quote bubble and posting subtitle notices when one ends. */
void SetPartyPortraitEventState(unsigned int party_slot, bool active, unsigned int event_type,
                                std::string_view quote_text, int show_quote)
{
    W8MonsterManagerEntry* record = &CurrentGame().monster_manager_entries[party_slot];
    W8PortraitQuoteState* quote = &record->quote;

    if (record->portrait_event_active == active) {
        return;
    }
    if (!active) {
        record->portrait_event_active = false;
        record->portrait.frame = 6;
        record->portrait.frame_dirty = true;
        int pc_slot = RPCPtrToPCSlot(record);
        record->portrait.blink_closing = false;
        W8Condition highest_condition = CurrentGame().party.characters[pc_slot].highest_condition;
        if (highest_condition < W8_CONDITION_ASLEEP && !CurrentGame().surprise_possible) {
            if (record->portrait.target_pose != 1) {
                record->portrait.target_pose = 1;
            }
        } else {
            record->portrait.target_pose = 2;
        }
        if (!quote->bubble) {
            record->portrait_event_active = active;
            return;
        }
        unsigned int stored_event = record->pending_event_type;
        unsigned int mapped_event = stored_event;
        if (static_cast<int>(g_normal_event_count) < static_cast<int>(mapped_event)) {
        show_deactivate_quote:
            std::string formatted;
            std::string character_name = CurrentGame().party.characters[party_slot].name;
            formatted = character_name;
            ShowNotice(W8_FONT_PALETTE_GREEN, formatted, 3, W8_NOTICE_WRAP_TEXT_BOX);
            /* Retail 0x0052fc82..0x0052fc9b: the quote line follows the
               speaker's name into the dialogue box (3) with the same scroll
               width, not the mode-selected box. */
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, quote->bubble->text(), 3, W8_NOTICE_WRAP_TEXT_BOX);
        } else {
            if (kFirstRemappedEvent <= mapped_event) {
                mapped_event = g_normal_event_count - kFirstRemappedEvent + mapped_event;
            }
            if (g_character_event_descriptors[mapped_event].log_quote_on_finish != 0) {
                goto show_deactivate_quote;
            }
        }
        quote->bubble.reset();
        if (CurrentScreenState().id == W8_SCREEN_CAMP ||
            (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
             !MainGameHud()->review_transition_active)) {
            int left = quote->x;
            int top = quote->y;
            EraseRegion(left, top, quote->width + left, quote->height + top);
        }
        DisablePartyPortraitEvent(party_slot);
        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
            CurrentGame().events.push(PartyPanelChanged{});
            CurrentGame().events.push(MonstersChanged{});
            record->portrait_event_active = false;
            return;
        }
        if (auto* camp = FindScreen<CampScreen>()) camp->InvalidateAll();
        record->portrait_event_active = active;
        return;
    }

    record->portrait_event_active = true;
    record->portrait.frame = 7;
    record->portrait.frame_dirty = true;
    record->portrait.frame_due = CurrentGame().time + std::chrono::milliseconds{120};
    int pose_category;
    if (static_cast<int>(g_normal_event_count) < static_cast<int>(event_type)) {
        pose_category = 1;
    } else {
        unsigned int mapped_event = event_type;
        if (kFirstRemappedEvent <= event_type) {
            mapped_event = g_normal_event_count - kFirstRemappedEvent + event_type;
        }
        pose_category = g_character_event_descriptors[mapped_event].portrait_pose_category;
    }
    int pc_slot = RPCPtrToPCSlot(record);
    record->portrait.blink_closing = false;
    W8Condition highest_condition = CurrentGame().party.characters[pc_slot].highest_condition;
    if (highest_condition < W8_CONDITION_ASLEEP && !CurrentGame().surprise_possible) {
        if (record->portrait.target_pose != pose_category) {
            record->portrait.target_pose = pose_category;
        }
    } else {
        record->portrait.target_pose = 2;
    }
    if (quote_text.empty() || show_quote == 0) {
        quote->bubble.reset();
    } else {
        bool layout_quote = false;
        bool use_modal_gate = false;
        if (static_cast<int>(g_normal_event_count) < static_cast<int>(event_type)) {
            use_modal_gate = true;
        } else {
            unsigned int mapped_event = event_type;
            if (kFirstRemappedEvent <= event_type) {
                mapped_event = g_normal_event_count - kFirstRemappedEvent + event_type;
            }
            if (g_character_event_descriptors[mapped_event].defer_outside_main_game != 0 ||
                CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
                use_modal_gate = true;
            } else {
                layout_quote = true;
            }
        }
        if (use_modal_gate && CurrentScreenState().id == W8_SCREEN_MAIN_GAME && !IsModalOpen()) {
            layout_quote = true;
        }
        if (!layout_quote || g_settings.pc_subtitles == 0) {
            quote->bubble.reset();
        } else {
            quote->bubble = QuoteBubble::layout(quote_text, 200, std::nullopt);
            quote->width = static_cast<unsigned short>(quote->bubble ? quote->bubble->width() : 0);
            quote->height =
                static_cast<unsigned short>(quote->bubble ? quote->bubble->height() : 0);
            if (auto* camp = FindScreen<CampScreen>()) {
                if (static_cast<int>(party_slot) == camp->ReviewedPartySlot()) {
                    quote->x = 10;
                    quote->y = 8;
                } else {
                    quote->x = static_cast<unsigned short>(((party_slot & 1) * 0x30) + 0x36);
                    quote->y = static_cast<unsigned short>((party_slot >> 1) * 0x27 + 5);
                }
                camp->InvalidateAll();
            } else {
                unsigned short base_x = g_portrait_quote_x[party_slot];
                quote->x = base_x;
                quote->y = g_portrait_quote_y[party_slot];
                if ((party_slot & 1) == 1) {
                    quote->x = static_cast<unsigned short>(base_x - quote->width + 200);
                }
                if (quote->y + quote->height > 0x165) {
                    quote->y = static_cast<unsigned short>(0x165 - quote->height);
                }
            }
            unsigned short x = quote->x;
            unsigned short y = quote->y;
            SetPartyPortraitEventBounds(party_slot, x, y, x + quote->width, quote->height + y);
            EnablePartyPortraitEvent(party_slot);
        }
    }
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
        g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS &&
        MainGameHud()->portrait_refresh_pending[party_slot] == 0 &&
        event_type != static_cast<unsigned int>(g_special_event17)) {
        RefreshSelectedPartyPortrait(party_slot);
        record->auto_portrait_refresh = true;
        record->portrait_event_active = active;
        return;
    }
    record->portrait_event_active = active;
}

// The first line schedules a quick response; the response schedules a new
// exchange after five to six minutes. Only completion arms the next deadline.
void W8CharacterEventQueue::RestartFollowUpClock(W8CharacterEvent* entry)
{
    if (follow_up && entry->IsAmbientExchange()) {
        follow_up->ready_at =
            MonotonicMilliseconds() + (follow_up->excluded_slot != -1
                                           ? CurrentGame().rng.below(6000) + 2000
                                           : CurrentGame().rng.below(60000) + 300000);
    }
}

void W8CharacterEventQueue::ProcessFollowUpEvents()
{
    if (!CurrentGame().advancement.time_passes) return;
    if (!follow_up) follow_up = FollowUp{};
    const auto ambient = [](const auto& event) { return event->IsAmbientExchange(); };
    for (const auto* events : {&pending_events, &npc_deferred_events, &active_events})
        if (std::ranges::any_of(*events, ambient)) return;
    if (TimeUntilDeadline(follow_up->ready_at) != 0) return;

    const int speaker = GetRandomCharacter(0, 0, follow_up->excluded_slot, -1);
    if (speaker == -1) return;
    if (!QueueCharacterEvent(&CurrentGame().party.characters[speaker],
                             CurrentGame().rng.below(2) + 0xe, 1, 0x7f))
        return;
    follow_up->excluded_slot = follow_up->excluded_slot == -1 ? speaker : -1;
}

W8CharacterEvent* W8CharacterEventQueue::QueueEntry(std::unique_ptr<W8CharacterEvent> entry)
{
    const unsigned int party_slot = CharacterPointerToPartySlot(entry->character);
    if (HasEventCharacter(entry->event_type, party_slot)) {
        return nullptr;
    }
    if (CurrentGame().status.greeting_pending) {
        return nullptr;
    }
    if (entry->event_type > 0x91 && (entry->flags & W8_EVENT_NO_PREEMPT) == 0) {
        W8MonsterManagerEntry* slot = &CurrentGame().monster_manager_entries[party_slot];
        if (slot->active_character_event != 0) {
            CompleteActiveEvent(slot->active_character_event);
        }
        if (entry->Dispatch() == 0) {
            return nullptr;
        }
        return active_events.emplace_back(std::move(entry)).get();
    }
    if (entry->event_type == 0x21) {
        const auto is_same_type = [](const auto& queued) { return queued->event_type == 0x21; };
        if ((!active_events.empty() && is_same_type(active_events.front())) ||
            std::ranges::any_of(pending_events, is_same_type)) {
            return nullptr;
        }
    }
    if ((ShouldDeferCharacterEventForNpcScript(false) || IsNpcScriptSessionActive()) &&
        (entry->flags & W8_EVENT_NO_NPC_DEFER) == 0) {
        return npc_deferred_events.emplace_back(std::move(entry)).get();
    }
    return pending_events.emplace_back(std::move(entry)).get();
}

void W8CharacterEventQueue::SetEventCharacterMask(unsigned int event_type, unsigned int party_slot,
                                                  bool enabled)
{
    unsigned char mask = static_cast<unsigned char>(1 << (party_slot & 31));

    unsigned int mask_index;
    if (!MapEventTypeToDescriptorIndex(event_type, &mask_index)) {
        return;
    }
    if (!enabled) {
        event_character_masks[mask_index] &= static_cast<unsigned char>(~mask);
    } else {
        event_character_masks[mask_index] |= mask;
    }
}

bool W8CharacterEventQueue::HasEventCharacter(unsigned int event_type, unsigned int party_slot)
{
    if (event_type >= kFirstRemappedEvent) {
        event_type += g_normal_event_count - kFirstRemappedEvent;
    }
    return (event_character_masks[event_type] &
            static_cast<unsigned char>(1 << (party_slot & 31))) != 0;
}

bool W8CharacterEventQueue::TryAdjustQueuedEvent(W8CharacterEvent* entry)
{
    if (entry == 0 || active_event_type == -1 ||
        entry->event_type != static_cast<unsigned int>(active_event_type)) {
        return true;
    }

    unsigned int party_slot = CharacterPointerToPartySlot(entry->character);
    if (party_slot == static_cast<unsigned int>(active_party_slot)) {
        return true;
    }

    if (CurrentGame().time >= recent_event_until) {
        active_event_type = -1;
        active_party_slot = -1;
        return true;
    }

    unsigned int event_type = entry->event_type;
    unsigned int descriptor_index;
    if (!MapEventTypeToDescriptorIndex(event_type, &descriptor_index)) {
        return true;
    }
    if (g_character_event_descriptors[descriptor_index].coalesce_duplicates == 0) {
        return true;
    }

    entry->original_event_type = event_type;
    if (event_type == 4) {
        return false;
    }
    entry->event_type = 10;
    return true;
}

bool W8CharacterEventQueue::HasActiveEvents()
{
    return !active_events.empty();
}

bool W8CharacterEventQueue::IsMainQueueEmpty() const
{
    return pending_events.empty();
}

void W8CharacterEventQueue::ProcessDeferredCharacterEvents()
{
    if (CurrentGame().surprise_possible) {
        return;
    }

    if (!npc_deferred_events.empty() && !ShouldDeferCharacterEventForNpcScript(false) &&
        !IsNpcScriptSessionActive()) {
        /* Requeueing can dispatch, so detach the batch first; anything
           deferred again lands in the emptied list. */
        auto deferred = std::move(npc_deferred_events);
        npc_deferred_events.clear();
        for (auto& entry : deferred) {
            QueueEntry(std::move(entry));
        }
    }

    if (!pending_events.empty()) {
        /* At most three party members voice the same event at once; extra
           duplicates of the head event are dropped at random. */
        const W8CharacterEvent& baseline = *pending_events.front();
        std::vector<int> conflict_indices{0};
        for (int index = 1; index < static_cast<int>(pending_events.size()); ++index) {
            const W8CharacterEvent& entry = *pending_events[index];
            if (entry.event_type == baseline.event_type && entry.character != baseline.character &&
                entry.event_type != 0x2a && entry.event_type != 0x24) {
                conflict_indices.push_back(index);
            }
        }

        const int conflict_count = static_cast<int>(conflict_indices.size());
        if (conflict_count > 3) {
            int remaining_conflicts = conflict_count;
            while (remaining_conflicts > 3) {
                int pick = CurrentGame().rng.below(conflict_count);
                if (conflict_indices[pick] != -1) {
                    conflict_indices[pick] = -1;
                    --remaining_conflicts;
                }
            }
            for (int index = conflict_count - 1; index >= 0; --index) {
                if (conflict_indices[index] != -1) {
                    pending_events.erase(pending_events.begin() + conflict_indices[index]);
                }
            }
        }
    }

    if (pending_events.empty()) {
        const ScriptContext context = MakeScriptContext(CurrentGame());
        UpdateNpcDialogueVoiceAndCursor(context);
        if (PartyPortraitEventsIdle() != 0) {
            UpdateSedexusScene(context);
            CurrentGame().npc_dialogue.Advance(context);
        }
        return;
    }

    for (auto position = pending_events.begin(); position != pending_events.end(); ++position) {
        W8CharacterEvent& entry = **position;
        const unsigned int event_type = entry.event_type;
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
            unsigned int descriptor_index;
            if (MapEventTypeToDescriptorIndex(event_type, &descriptor_index) &&
                g_character_event_descriptors[descriptor_index].defer_outside_main_game != 0) {
                continue;
            }
        }

        if (entry.character->fInParty) {
            const unsigned int party_slot = CharacterPointerToPartySlot(entry.character);
            if (!HasEventCharacter(event_type, party_slot)) {
                if (PartyPortraitEventsIdle() == 0) {
                    return;
                }
                if (entry.dispatch_delay != std::chrono::milliseconds::zero() &&
                    CurrentGame().time - entry.dispatch_delay_start <= entry.dispatch_delay) {
                    return;
                }
                std::unique_ptr<W8CharacterEvent> dispatched = std::move(*position);
                pending_events.erase(position);
                if (TryAdjustQueuedEvent(dispatched.get()) == 0 || dispatched->Dispatch() == 0) {
                    return;
                }
                active_events.push_back(std::move(dispatched));
                return;
            }
        }

        /* The character left the party or already voiced this event. */
        pending_events.erase(position);
        return;
    }
}

/* Queue `effect` on a random eligible party member. When the first pick cannot
   receive the event the draw retries up to fifty times; exhaustion returns
   zero without queueing anything. */
W8CharacterEvent* ApplyItemEffectToRandomCharacter(unsigned int event_type, int excluded_slot,
                                                   unsigned int flags)
{
    int character_slot;
    int attempts;

    if (event_type >= g_normal_event_count &&
        (event_type < kFirstRemappedEvent ||
         event_type >= kRemappedEventCount + kFirstRemappedEvent)) {
        return 0;
    }
    character_slot = GetRandomCharacter(0, 0, excluded_slot, -1);
    if (character_slot == -1) {
        return 0;
    }
    attempts = 0;
    bool can_dispatch = CanDispatchCharacterEvent(character_slot, event_type);
    while (!can_dispatch && attempts < 0x32) {
        character_slot = GetRandomCharacter(0, 0, excluded_slot, -1);
        ++attempts;
        can_dispatch = CanDispatchCharacterEvent(character_slot, event_type);
    }
    if (attempts == 0x32) {
        return 0;
    }
    return QueueCharacterEvent(&CurrentGame().party.characters[character_slot], event_type, flags,
                               kCharacterEventFullVolume);
}

W8CharacterEvent* QueueCharacterEvent(W8Character* character, int event_type, unsigned int flags,
                                      unsigned int volume)
{
    if (g_settings.pc_confirmations == 0 &&
        (event_type == g_special_event2 || event_type == g_special_event17)) {
        return 0;
    }
    if (event_type != g_special_event0 && event_type != g_special_event12 &&
        event_type != g_special_event4 && event_type != g_special_event9 &&
        event_type != g_special_event10 && event_type != g_special_event16) {
        volume = volume * 70 / 100;
    }
    return CurrentGame().speech.QueueEntry(
        std::make_unique<W8CharacterEvent>(character, event_type, flags, volume));
}

/* Set the pose a party-slot portrait is animating toward. The request drops
   any pose animation already in progress; a slot whose character is dead or
   in a severe condition - or a party caught surprised - is forced to the
   incapacitated pose instead. */
void SetPortraitTargetPose(W8MonsterManagerEntry* slot, int pose)
{
    int party_slot = RPCPtrToPCSlot(slot);

    slot->portrait.blink_closing = false;
    if (CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_ASLEEP &&
        !CurrentGame().surprise_possible) {
        if (slot->portrait.target_pose != pose) {
            slot->portrait.target_pose = pose;
        }
        return;
    }
    slot->portrait.target_pose = 2;
}

/* A character at zero percent hit points may start one of the three recovered
   incapacitation events. Which pair is available is selected by the two data
   flags; successfully queueing the event clears the matching held effect. */
void MaybeStartIncapacitationEvent(unsigned int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    int effect;

    if ((character->hp_current * 100) / static_cast<unsigned int>(character->uiHPMax) != 0) {
        return;
    }
    effect = 3;
    if (kPortraitLowHealthPercent == 0) {
        if (kFleeHpFraction == 0) {
            return;
        }
        effect = CurrentGame().rng.below(2) == 0 ? 2 : 28;
    }
    if (effect != -1 && QueueCharacterEvent(character, effect, 0, kCharacterEventFullVolume) != 0) {
        CurrentGame().speech.SetEventCharacterMask(effect, party_slot, true);
    }
}

/* After a death in `party_slot`, pick one other eligible party member and queue
   their reaction event - the slot-two pair share one effect, later slots pick
   by gender - with a three-second clock on the queued entry. */
void QueuePartyDeathReaction(unsigned int party_slot)
{
    W8CharacterEvent* entry;
    unsigned int selected[1];
    unsigned int remaining;
    unsigned int index;
    bool skip_first_two = false;
    int effect;

    if (party_slot < 2) {
        skip_first_two = true;
        effect = 20;
    } else {
        effect = 18;
        if (CurrentGame().party.characters[party_slot].gender != W8_GENDER_MALE) {
            effect = 19;
        }
    }
    remaining = GetRandomPartySlots(0, 0, party_slot, selected, 1, skip_first_two);
    for (index = 0; index < remaining; ++index) {
        entry = QueueCharacterEvent(&CurrentGame().party.characters[selected[index]], effect, 0,
                                    kCharacterEventFullVolume);
        if (entry != 0) {
            entry->DelayDispatch(std::chrono::milliseconds{3000});
        }
    }
}

/* Queue a low-HP flee or incapacitation event when the character is still
   standing, then always roll one of three ambient follow-up events. */
void QueueDamageReactionEvents(W8Character* character)
{
    unsigned int hp_percent;
    unsigned int party_slot;
    bool has_incapacitation_event;
    bool has_flee_event;
    W8CharacterEvent* entry;
    int effect;
    int follow_up_events[3];

    hp_percent = (character->hp_current * 100) / static_cast<unsigned int>(character->uiHPMax);
    party_slot = CharacterPointerToPartySlot(character);
    has_incapacitation_event = CurrentGame().speech.HasEventCharacter(3, party_slot);
    has_flee_event = CurrentGame().speech.HasEventCharacter(2, party_slot);
    CurrentGame().speech.HasEventCharacter(28, party_slot);
    if (character->highest_condition != W8_CONDITION_ASLEEP &&
        character->highest_condition != W8_CONDITION_UNCONSCIOUS) {
        if (kPortraitLowHealthPercent <= hp_percent || has_incapacitation_event) {
            if (kFleeHpFraction <= hp_percent) {
                goto queue_follow_up_event;
            }
            if (CurrentGame().rng.below(2) != 0 || has_flee_event) {
                effect = 28;
            } else {
                effect = 2;
            }
            entry = QueueCharacterEvent(character, effect, 1, kCharacterEventFullVolume);
        } else {
            entry = QueueCharacterEvent(character, 3, 1, kCharacterEventFullVolume);
        }
        if (entry != 0) {
            entry->DelayDispatch(std::chrono::milliseconds{1500});
        }
    }
queue_follow_up_event:
    follow_up_events[0] = g_special_event11;
    follow_up_events[1] = g_special_event12;
    follow_up_events[2] = g_special_event4;
    QueueCharacterEvent(character, follow_up_events[CurrentGame().rng.below(3)], 1,
                        kCharacterEventFullVolume);
}

/* Turn-begin path for several surviving party members: queue event 0x15 on
   one random eligible character. */
void QueueTurnReactionEvent(void)
{
    unsigned int selected[1];
    unsigned int index;
    unsigned int remaining;

    remaining = GetRandomPartySlots(0, 0, -1, selected, 1, false);
    for (index = 0; index < remaining; ++index) {
        QueueCharacterEvent(&CurrentGame().party.characters[selected[index]], 0x15, 0,
                            kCharacterEventFullVolume);
    }
}

/* Turn-begin path when only one occupied party member is still standing:
   that character says event 0x16. */
void QueueLastSurvivorEvent(void)
{
    int alive_count = 0;
    int last_alive = 0;

    for (int slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.members[slot].has_value() &&
            CurrentGame().party.characters[slot].uiCondition[W8_CONDITION_DEAD] == 0) {
            ++alive_count;
            last_alive = slot;
        }
    }
    if (alive_count != 0) {
        QueueCharacterEvent(&CurrentGame().party.characters[last_alive], 0x16, 0,
                            kCharacterEventFullVolume);
    }
}

/* Reacts to a freshly recomputed highest_condition: the armed one-shot flag
   swallows one change, otherwise the character queues the reaction event for
   the new condition. A dying character first loses every queued event, and a
   charmed character makes a different party member say the line instead. */
void QueueConditionChangeReaction(W8Character* character)
{
    int events[3];
    int excluded_slot;
    int slot;
    int attempts;
    unsigned int reaction;

    if (IsSedexusCaptureActive()) {
        return;
    }
    if (CurrentGame().status.skip_next_condition_reaction) {
        CurrentGame().status.skip_next_condition_reaction = false;
        return;
    }
    switch (character->highest_condition) {
    case 0x12:
        CurrentGame().speech.RemoveCharacterEvents(character);
        events[0] = g_special_event9;
        events[1] = g_special_event10;
        events[2] = g_special_event16;
        QueueCharacterEvent(character, events[CurrentGame().rng.below(3)], 1,
                            kCharacterEventFullVolume);
        return;
    case 0x11:
        events[0] = g_special_event9;
        events[1] = g_special_event10;
        events[2] = g_special_event16;
        QueueCharacterEvent(character, events[CurrentGame().rng.below(3)], 0,
                            kCharacterEventFullVolume);
        return;
    case 5:
        QueueCharacterEvent(character, g_special_event13, 0, kCharacterEventFullVolume);
        return;
    case 8:
        QueueCharacterEvent(character, g_special_event1, 0, kCharacterEventFullVolume);
        return;
    case 3:
    case 4:
        QueueCharacterEvent(character, g_special_event7, 0, kCharacterEventFullVolume);
        return;
    case 2:
    case 7:
    case 9:
    case 10:
        reaction = kConditionReaction;
        if (CurrentGame().rng.below(2) == 0) {
            reaction = kConditionReactionAlt;
        }
        QueueCharacterEvent(character, reaction, 0, kCharacterEventFullVolume);
        return;
    case 0xc:
        QueueCharacterEvent(character, 7, 0, kCharacterEventFullVolume);
        return;
    case 0xe:
    case 0x10:
        QueueCharacterEvent(character, 9, 0, kCharacterEventFullVolume);
        return;
    case 0x13:
        excluded_slot = CharacterPointerToPartySlot(character);
        reaction = 40;
        if ((reaction < g_normal_event_count ||
             (kFirstRemappedEvent <= reaction &&
              reaction < kRemappedEventCount + kFirstRemappedEvent)) &&
            (slot = GetRandomCharacter(0, 0, excluded_slot, -1)) != -1) {
            attempts = 0;
            while (!CanDispatchCharacterEvent(slot, reaction)) {
                if (attempts >= 0x32) {
                    return;
                }
                slot = GetRandomCharacter(0, 0, excluded_slot, -1);
                ++attempts;
            }
            QueueCharacterEvent(&CurrentGame().party.characters[slot], reaction, 0,
                                kCharacterEventFullVolume);
            return;
        }
        break;
    case 6:
        QueueCharacterEvent(character, g_special_event3, 0, kCharacterEventFullVolume);
        return;
    case 0xb:
        QueueCharacterEvent(character, g_special_event21, 0, kCharacterEventFullVolume);
        break;
    default:
        break;
    }
}

/* Reacts to a condition being lifted: when nothing remains as the highest
   condition the character announces full recovery (0x55), otherwise the
   surviving-condition line (0x54); selected conditions map to fixed events. */
void QueueConditionClearedReaction(W8Character* character, W8Condition condition)
{
    if (IsSedexusCaptureActive()) {
        return;
    }
    if (CurrentGame().status.skip_next_condition_reaction) {
        CurrentGame().status.skip_next_condition_reaction = false;
        return;
    }
    switch (condition) {
    case W8_CONDITION_DEAD:
        QueueCharacterEvent(character, 12, 0, kCharacterEventFullVolume);
        return;
    case W8_CONDITION_INFATUATED:
    case W8_CONDITION_MISSING:
        QueueCharacterEvent(character, 11, 0, kCharacterEventFullVolume);
        return;
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_IRRITATED:
    case W8_CONDITION_NAUSEATED:
    case W8_CONDITION_SLOWED:
    case W8_CONDITION_AFRAID:
    case W8_CONDITION_POISONED:
    case W8_CONDITION_SILENCED:
    case W8_CONDITION_HEXED:
    case W8_CONDITION_INSANE:
    case W8_CONDITION_BLIND:
    case W8_CONDITION_WEBBED:
    case W8_CONDITION_PARALYZED:
    case W8_CONDITION_UNCONSCIOUS:
        if (character->highest_condition == W8_CONDITION_NONE) {
            QueueCharacterEvent(character, 0x55, 0, kCharacterEventFullVolume);
            return;
        }
        QueueCharacterEvent(character, 0x54, 0, kCharacterEventFullVolume);
        break;
    default:
        break;
    }
}

/* Requeue the stored portrait event of the selected character with a forced
   argument of 4; the selection and the stored type are cleared elsewhere. */
void RequeueSelectedPortraitEvent(void)
{
    int slot = CurrentGame().status.selected_character;

    if (slot != -1) {
        unsigned int event_type = CurrentGame().party.rows[slot].pending_event_type;
        if (event_type != 0) {
            QueueCharacterEvent(&CurrentGame().party.characters[slot], event_type, 4, 0x7f);
        }
    }
}

/* Occupied slots with portrait_event_active set still have a portrait/voice record in
   flight; trap-trigger follow-up waits until none of those are active. */
bool PartyPortraitEventsIdle(void)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (CurrentGame().party.members[slot] &&
            CurrentGame().monster_manager_entries[slot].portrait_event_active) {
            return false;
        }
    }
    return true;
}

static void SetCurrentPortraitPose(W8PortraitAnimation& record, int pose)
{
    if (record.pose != pose) {
        record.pose = pose;
        record.pose_dirty = true;
    }
}

void AdvancePortraitSpeech(W8PortraitAnimation& state, std::chrono::milliseconds now,
                           bool mouth_open, bool fixed_frame)
{
    if (!mouth_open) {
        if (now >= state.frame_due) {
            if (state.voice_remaining_ms < 120) {
                state.frame = 6;
                state.frame_dirty = true;
                state.voice_remaining_ms = 0;
            } else {
                int direction =
                    ChooseDifferentMonsterDirection(static_cast<short>(state.frame) - 6) + 6;
                if (fixed_frame) {
                    direction = 8;
                }
                state.frame = direction;
                state.frame_dirty = true;
                state.frame_due = now + std::chrono::milliseconds{120};
                state.voice_remaining_ms -= 120;
            }
        }
    } else {
        state.frame = 6;
        state.frame_dirty = true;
    }
}

void AdvancePortraitPose(W8PortraitAnimation& state)
{
    if (!state.blink_closing) {
        if (state.pose == state.target_pose) {
            if (state.pose == 1 && TimeUntilDeadline(state.idle_clock) == 0) {
                state.blink_closing = true;
                state.idle_clock = MonotonicMilliseconds() + (CurrentGame().rng.below(5000) + 5000);
            }
        } else if (TimeUntilDeadline(state.pose_clock) == 0) {
            int pose = state.pose;
            SetCurrentPortraitPose(state,
                                   g_portrait_pose_transition[pose - 1][state.target_pose - 1]);
            // Reopening and event-pose transitions continue toward the
            // target; only an idle blink enters the closing phase.
            state.pose_clock = MonotonicMilliseconds() + (CurrentGame().rng.below(50) + 50);
        }
    } else if (TimeUntilDeadline(state.pose_clock) == 0) {
        int pose = state.pose;
        if (pose != 2) {
            SetCurrentPortraitPose(state, g_portrait_pose_transition[pose - 1][2 - 1]);
            state.blink_closing = true;
            state.pose_clock = MonotonicMilliseconds() + (CurrentGame().rng.below(50) + 50);
        }
        if (state.pose == 2) {
            state.blink_closing = false;
        }
    }
}

void AdvancePartyVoices(void)
{
    for (unsigned int party_slot = 0; party_slot < 8; ++party_slot) {
        W8MonsterManagerEntry* record = &CurrentGame().monster_manager_entries[party_slot];
        bool sound_active = false;

        if (!CurrentGame().party.members[party_slot].has_value()) {
            continue;
        }
        if (record->portrait_event_active) {
            if (record->voice_sound_handle == nullptr) {
                if (record->portrait.voice_remaining_ms == 0) {
                    FinishPortraitSpeech(party_slot);
                }
            } else {
                UpdateMouthGapTrack(record->voice_sound_handle, record->mouth_gap);
                sound_active = record->mouth_gap.mouth_open;
            }
        }

        if (!record->portrait_event_active) {
            if (PartyPortraitEventsIdle() != 0) {
                MaybeStartIncapacitationEvent(party_slot);
            }
        } else {
            W8Character* character = &CurrentGame().party.characters[party_slot];
            if ((character->highest_condition > W8_CONDITION_WEBBED ||
                 character->hp_current == 0) &&
                record->active_character_event != 0) {
                CurrentGame().speech.CompleteActiveEvent(record->active_character_event);
            }
            if (!record->portrait_event_active) {
                if (PartyPortraitEventsIdle() != 0) {
                    MaybeStartIncapacitationEvent(party_slot);
                }
            } else {
                AdvancePortraitSpeech(record->portrait, CurrentGame().time, sound_active,
                                      g_event_range_min <= record->pending_event_type &&
                                          record->pending_event_type <= g_event_range_max);
            }
        }
    }
}

void AnimatePartyPortraits(void)
{
    for (unsigned int party_slot = 0; party_slot < 8; ++party_slot) {
        W8MonsterManagerEntry* record = &CurrentGame().monster_manager_entries[party_slot];
        if (!CurrentGame().party.members[party_slot].has_value()) {
            continue;
        }
        if (CurrentGame().activity.conversation && (party_slot & 1) != 0 &&
            IsPortraitObscuredByNpcDialogue(party_slot)) {
            continue;
        }
        if (!record->damage_splat_active && !record->effect_icon_active &&
            (CurrentScreenState().id != W8_SCREEN_CHARACTER || record->portrait_event_active)) {
            AdvancePortraitPose(record->portrait);
            if ((record->portrait.pose_dirty || record->portrait.frame_dirty) &&
                !(MainGame() && MainGame()->keyboardMenuForSlot(party_slot))) {
                RefreshPartySlotDisplay(party_slot);
            }
        }
    }
}

/* Draw one party member's portrait at a screen position. When the caller asks
   for the animated form the frame blitter runs first, and the death,
   in-combat or exhausted state adds the darkened overlay. */
void RenderPartyPortrait(SDL_Surface& target, int portrait, int left, int top, SpriteDraw options,
                         unsigned char value, int party_slot)
{
    CatalogArtwork::Load(0x12, portrait).draw(target, 0, left, top, options);
    if (party_slot == -1) {
        return;
    }
    if (value != 0 && g_portrait_frame_flags[portrait] != 0) {
        bool drawn =
            BlitPartyPortraitAnimation(target, portrait, left, top, options, party_slot, true);
        value = !drawn;
    }
    if ((((CurrentGame().combat.has_value() && CurrentGame().combat->characters[party_slot].dead) ||
          CurrentGame().surprise_possible) ||
         CurrentGame().party.characters[party_slot].highest_condition == W8_CONDITION_MISSING) &&
        value != 0) {
        ShadowRect(target, {left, top, 0x5a, 0x48});
    }
}

/* Blit the two animated frame tracks for one party slot and mark the union
   of each track's old and new frames dirty. The B track draws the current
   frame and its blend predecessor; the A track draws the frame the animation
   is moving to, and a dead character stops after the B track. */
bool BlitPartyPortraitAnimation(SDL_Surface& target, int portrait, int left, int top,
                                SpriteDraw options, int party_slot, bool animate)
{
    const bool dead =
        CurrentGame().combat.has_value() && CurrentGame().combat->characters[party_slot].dead;
    const bool drawn = BlitPortraitAnimation(
        target, portrait, left, top, options,
        CurrentGame().monster_manager_entries[party_slot].portrait, animate,
        {.dead = dead,
         .surprised = bool(CurrentGame().surprise_possible),
         .missing =
             CurrentGame().party.characters[party_slot].highest_condition == W8_CONDITION_MISSING,
         .zero_hp = CurrentGame().party.characters[party_slot].hp_current == 0});
    return drawn;
}

bool BlitPortraitAnimation(SDL_Surface& target, int portrait, int left, int top, SpriteDraw options,
                           W8PortraitAnimation& animation, bool animate,
                           W8PortraitCondition condition)
{
    auto* state = &animation;
    const bool shadow = condition.dead || condition.surprised || condition.missing;
    const auto draw_base = [&] {
        CatalogArtwork::Load(0x12, portrait).draw(target, 0, left, top, options);
        if (shadow) ShadowRect(target, {left, top, 0x5a, 0x48});
    };
    DesignRect rect;
    DesignRect other;
    short width;
    short height;
    short image_x;
    short image_y;
    bool drawn = false;

    if (g_portrait_frame_flags[portrait] == 0) {
        return drawn;
    }
    const auto artwork = CatalogArtwork::Load(0x12, portrait);
    if (state->pose_dirty || state->pose != state->rendered_pose || (animate && state->pose != 1)) {
        const SDL_Point pose_size = artwork.size(state->pose);
        width = pose_size.x;
        height = pose_size.y;
        const SDL_Point image_position = artwork.position(state->pose);
        image_x = image_position.x;
        image_y = image_position.y;
        rect.left = image_x + left;
        rect.top = image_y + top;
        rect.right = width + rect.left;
        rect.bottom = height + rect.top;
        if (!animate && (condition.dead || condition.surprised)) {
            draw_base();
        }
        if (condition.zero_hp) {
            return true;
        }
        artwork.draw(target, state->pose, left, top, options);
        drawn = true;
        if (state->rendered_pose != -1) {
            const SDL_Point previous_size = artwork.size(state->rendered_pose);
            width = previous_size.x;
            height = previous_size.y;
            const SDL_Point image_position = artwork.position(state->rendered_pose);
            image_x = image_position.x;
            image_y = image_position.y;
            other.left = image_x + left;
            other.top = image_y + top;
            other.right = width + other.left;
            other.bottom = height + other.top;
            rect = UnionScreenRects(other, rect);
        }
        InvalidateScreenRects(&rect, 1);
        state->rendered_pose = state->pose;
        state->pose_dirty = false;
    }
    if (!state->frame_dirty && state->frame == state->rendered_frame &&
        (!animate || state->frame == 6)) {
        if (!drawn) {
            return false;
        }
    } else {
        const SDL_Point frame_size = artwork.size(state->frame);
        width = frame_size.x;
        height = frame_size.y;
        const SDL_Point image_position = artwork.position(state->frame);
        image_x = image_position.x;
        image_y = image_position.y;
        if (!animate && !drawn && condition.dead) {
            draw_base();
        }
        artwork.draw(target, state->frame, left, top, options);
        rect.left = image_x + left;
        rect.top = image_y + top;
        rect.right = width + rect.left;
        rect.bottom = height + rect.top;
        drawn = true;
        if (state->rendered_frame != -1) {
            const SDL_Point previous_size = artwork.size(state->rendered_frame);
            width = previous_size.x;
            height = previous_size.y;
            const SDL_Point image_position = artwork.position(state->rendered_frame);
            image_x = image_position.x;
            image_y = image_position.y;
            other.left = image_x + left;
            other.top = image_y + top;
            other.right = width + other.left;
            other.bottom = height + other.top;
            rect = UnionScreenRects(other, rect);
        }
        InvalidateScreenRects(&rect, 1);
        state->rendered_frame = state->frame;
        state->frame_dirty = false;
    }
    if (shadow) {
        ShadowRect(target, {left, top, 0x5a, 0x48});
    }
    return drawn;
}

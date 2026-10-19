#include "wiz8/npc_interaction.h"

#include <optional>

#include "conditions.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/npc_dialogue_view.h"
#include "wiz8/local_code/npc_story_events.h"

// GLOBAL: WIZ8 0x0068C430
W8NpcDialogue g_npc_dialogue;

/* NPC interaction availability and its party-slot eligibility query. Live
   query: 0x00524A10 is a gap between Local Code\Conditions & Enchantments.cpp
   (upper 0x00524780) and Local Code\NPC Scripting.cpp (lower 0x00524CA0). */

/* Report whether a party slot can be picked: in range, occupied, still on its
   feet, and not Turncoat or a more severe condition. */
// FUNCTION: WIZ8 0x00524a10
bool IsPartySlotEligible(std::optional<int> slot)
{
    W8Character* character;
    bool eligible;

    if (!slot || *slot < 0 || *slot >= 8) {
        return false;
    }
    if (!g_status.buffers.XChar[*slot].fOccupied) {
        return false;
    }
    character = &g_status.buffers.Char[*slot];
    if (character->hp_current == 0) {
        return false;
    }
    eligible = character->highest_condition < W8_CONDITION_TURNCOAT;
    return eligible;
}

// FUNCTION: WIZ8 0x00524c50
void ClearNpcMessageQueue(void)
{

    ResetNpcPresentation();
    g_npc_dialogue.Reset();
    ResetSedexusScene();
}

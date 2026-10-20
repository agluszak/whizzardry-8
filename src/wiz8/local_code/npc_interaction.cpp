
#include "wiz8/npc_interaction.h"

#include "wiz8/combat/combat.h"
#include "wiz8/game.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/npc_dialogue_view.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"

W8NpcDialogue g_npc_dialogue;

/* NPC interaction availability and its party-slot eligibility query. Live
   query: 0x00524A10 is a gap between Local Code\Conditions & Enchantments.cpp
   (upper 0x00524780) and Local Code\NPC Scripting.cpp (lower 0x00524CA0). */

/* Report whether a party slot can be picked: in range, occupied, still on its
   feet, and not Turncoat or a more severe condition. */
bool IsPartySlotEligible(int slot)
{
    W8Character* character;
    bool eligible;

    if (slot < 0) {
        return false;
    }
    if (slot >= 8) {
        return false;
    }
    if (!CurrentGame().party.members[slot].has_value()) {
        return false;
    }
    character = &CurrentGame().party.characters[slot];
    if (character->hp_current == 0) {
        return false;
    }
    eligible = character->highest_condition < W8_CONDITION_TURNCOAT;
    return eligible;
}

void ClearNpcMessageQueue(void)
{

    ResetNpcPresentation();
    g_npc_dialogue.Reset();
    ResetSedexusScene();
}

#include "wiz8/containers.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/message_box.h"
#include "soundman.h"

// GLOBAL: WIZ8 0x0068C430
W8NpcScriptingState g_npc_scripting;

/* NPC interaction availability and its party-slot eligibility query. Live
   query: 0x00524A10 is a gap between Local Code\Conditions & Enchantments.cpp
   (upper 0x00524780) and Local Code\NPC Scripting.cpp (lower 0x00524CA0). */

/* Report whether a party slot can be picked: in range, occupied, still on its
   feet, and not Turncoat or a more severe condition. */
// FUNCTION: WIZ8 0x00524a10
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
    if (!g_status.buffers.XChar[slot].fOccupied) {
        return false;
    }
    character = &g_status.buffers.Char[slot];
    if (character->hp_current == 0) {
        return false;
    }
    eligible = character->highest_condition < W8_CONDITION_TURNCOAT;
    return eligible;
}

// FUNCTION: WIZ8 0x00524c50
void ClearNpcMessageQueue(void)
{

    if (g_npc_scripting.voice_playing) {
        g_npc_scripting.voice_playing = false;
        g_npc_scripting.stopping_voice_playback = true;
        SoundStop(g_npc_scripting.voice_handle);
    }
    FreeMouthGapTrack(&g_npc_scripting.gap_track);
    for (auto* line : g_npc_scripting.message_lines) {
        // These payloads have not been handed to a portrait bubble yet.
        switch (line->type) {
        case W8_NPC_MSG_SKILL_NOTICES:
            delete line->extra.skill_notices;
            delete[] line->payload.text;
            break;
        case W8_NPC_MSG_PORTRAIT_EXTRA:
            delete line->extra.experience;
            delete[] line->payload.text;
            break;
        case W8_NPC_MSG_LEVEL_UP:
            delete line->extra.level_up_slot;
            delete[] line->payload.text;
            break;
        case W8_NPC_MSG_PORTRAIT_MESSAGE:
            delete[] line->payload.text;
            break;
        default:
            break;
        }
        delete line;
    }
    g_npc_scripting = {};
}

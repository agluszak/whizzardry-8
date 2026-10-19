#include "wiz8/live_character_rules.h"

#include "wiz8/character_stat_rules.h"
#include "wiz8/fact_state.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/party_stat_rules.h"

CharacterStatRules CaptureLiveCharacterStatRules(const W8Character& character)
{
    CharacterStatRules rules;
    const auto slot = g_status.party.slotOf(character);
    if (!slot) return rules;
    if (g_combat.active())
        rules.defensive_action = TryCharacterAction(*slot, W8_ACTION_DEFEND, false) ||
                                 TryCharacterAction(*slot, W8_ACTION_PROTECT, false);
    if (character.fInParty && character.iRace == W8_RACE_ANDROID) {
        const auto* npc = GetNpcState(g_status.party.rows[*slot].npc_index);
        rules.damaged_android =
            npc && npc->name_style == W8_NPC_RFS81_A && !GetFact(W8_FACT_RFS81_HAS_BEEN_FIXED);
    }
    return rules;
}

PartyStatRules CaptureLivePartyStatRules()
{
    PartyStatRules rules;
    if (!g_status.game_started)
        rules.load_timing = PartyLoadTiming::Disabled;
    else if (g_combat.active())
        rules.load_timing = PartyLoadTiming::AfterCombat;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (g_status.party.rows[slot].fOccupied)
            rules.characters[slot] = CaptureLiveCharacterStatRules(g_status.party.characters[slot]);
    return rules;
}

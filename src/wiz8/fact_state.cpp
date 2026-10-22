#include "wiz8/fact_state.h"

#include <algorithm>
#include <array>
#include <vector>

#include "wiz8/combat/combat.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/story.h"

/* Runs once the new-game level has finished loading: records the two starting
   transcript keywords from string-table entries 0x7e7/0x7e8 and
   seeds the starting fact set, all with notifications suppressed. */
void PostNewGameLoad(void)
{
    g_fact_notifications_suppressed = true;
    LearnDialogueKeyword(Data().strings[0x7e7], W8_DIALOGUE_CATEGORY_MISC, false);
    LearnDialogueKeyword(Data().strings[0x7e8], W8_DIALOGUE_CATEGORY_MISC, false);
    SetFact(W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY, 1, false);
    SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 1, false);
    SetFact(W8_FACT_RAPAX_QUEEN_LOCKED_UP, 1, false);
    SetFact(W8_FACT_RODAN_LOCKED_IN_CAGE, 1, false);
    SetFact(W8_FACT_DRAZIC_LOCKED_IN_CAGE, 1, false);
    SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 1, false);
    SetFact(W8_FACT_FACTION_RATTKIN_COMMON_FRIENDLY, 1, false);
    g_fact_notifications_suppressed = false;
}

/* Install a save's facts in the game a load just began, then re-apply the consequences
   that do not survive a save. The game has no level yet, so no NPC stands in its world. */
void InstallFacts(Game& game, const std::array<unsigned char, W8_FACT_SLOT_COUNT>& facts)
{
    auto& stored = game.story.facts = facts;
    if (stored[W8_FACT_RFS81_HAS_BEEN_FIXED]) {
        const auto npc = std::ranges::find_if(
            game.story.npcs, [](const auto& state) { return state->record->kind == 0x20; });
        if (npc != game.story.npcs.end() && (*npc)->has_monster)
            (*npc)->script_file = LoadNpcScriptResources(**npc, nullptr);
    }
    // The derived import fact keeps its side effects, which act on the game just begun.
    if (!stored[W8_FACT_IMPORT_TRANG] && !stored[W8_FACT_IMPORT_UMPANI] && !stored[W8_FACT_VIRGIN])
        SetFact(W8_FACT_IMPORT_NOALIGN, 1, false);
}

/* Evaluate one derived fact from the party's live state. Most facts either
   read the stored fact array directly or combine a handful of primary facts;
   the hard-coded rules below are the original's. The helper evaluates
   recursively for the combined facts and logs each dependency when the
   fact-check log is enabled. */
unsigned char EvaluateFact(W8FactId fact_id)
{
    if (static_cast<unsigned>(fact_id) >= W8_FACT_SLOT_COUNT) return 0;
    unsigned char value;
    unsigned char count;

    if (fact_id == W8_FACT_DEVICE_ONE || fact_id == W8_FACT_DEVICE_TWO ||
        fact_id == W8_FACT_DEVICE_THREE) {
        count = PartyHasItem(CurrentGame().party, 0x243);
        if (PartyHasItem(CurrentGame().party, 0x242)) {
            ++count;
        }
        if (PartyHasItem(CurrentGame().party, 0x244)) {
            ++count;
        }
        if (fact_id == W8_FACT_DEVICE_ONE) {
            return count == 1;
        }
        if (fact_id == W8_FACT_DEVICE_TWO) {
            return count == 2;
        }
        return count == 3;
    }

    if (fact_id < W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY) {
        if (fact_id == W8_FACT_FACTION_HIGARDI_BANK_FRIENDLY) {
            return GetFactionDisposition(W8_FACTION_HIGARDI_BANK) == W8_FACTION_FRIENDLY;
        }
        switch (fact_id) {
        case W8_FACT_ARNIKA_PARTY_HAS_MARTEN_BADGE:
            return PartyHasItem(CurrentGame().party, 0x27b);
        case W8_FACT_FACTION_HIGARDI_COMMON_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_HIGARDI_COMMON) == W8_FACTION_FRIENDLY;
        case W8_FACT_ALIGNMENT_NONE:
            value = GetFact(W8_FACT_ALIGNMENT_TRANG);
            if (value == 0) {
                value = GetFact(W8_FACT_ALIGNMENT_UMPANI);
                if (value == 0) {
                    return 1;
                }
            }
            return 0;
        case W8_FACT_FACTION_HIGARDI_HLL_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_HIGARDI_HLL) == W8_FACTION_FRIENDLY;
        case W8_FACT_TWO_IN_PARTY:
            return CountLeadingPartySlots() == 2;
        case W8_FACT_MOOK_MOOK_IN_PARTY: {
            unsigned int slot = 0;
            while (!CurrentGame().party.members[slot].has_value() ||
                   CurrentGame().party.characters[slot].iRace != W8_RACE_MOOK ||
                   CurrentGame().party.characters[slot].highest_condition > W8_CONDITION_WEBBED) {
                if (slot >= 7) {
                    return 0;
                }
                ++slot;
            }
            break;
        }
        case W8_FACT_PEACE_DRAZIC_IN_PARTY:
            return NpcLeadHasNameStyle(W8_NPC_DRAZIC);
        case W8_FACT_PEACE_RODAN_IN_PARTY:
            return NpcLeadHasNameStyle(W8_NPC_RODAN);
        case W8_FACT_PEACE_TWO_IN_PARTY:
            if (!NpcLeadHasNameStyle(W8_NPC_RODAN) || !NpcLeadHasNameStyle(W8_NPC_DRAZIC)) {
                return 0;
            }
            break;
        case W8_FACT_VI_IN_PARTY:
            return NpcLeadHasNameStyle(W8_NPC_VI_DOMINA);
        case W8_FACT_GLUMPH_DEAD: {
            W8NpcState* npc = GetNpcStateByKind(0x2b);
            if (npc == 0) {
                return 0;
            }
            return static_cast<unsigned char>(npc->spawned);
        }
        case W8_FACT_UMISSION_TRAIN_FIRE_NOAMMO:
            value = GetFact(W8_FACT_UMISSION_TRAIN_COVERT_ASSIGN);
            if (value == 0) {
                value = GetFact(W8_FACT_UMISSION_TRAIN_FIRE_ASSIGN);
                if (value != 0 && !PartyHasItem(CurrentGame().party, 0x271) &&
                    !PartyHasItem(CurrentGame().party, 0x272)) {
                    return 1;
                }
            }
            return 0;
        case W8_FACT_UMISSION_TRAIN_COVERT_DONE:
            if (!CurrentGame().status.fact_88_latch) {
                if (CountPartyItem(CurrentGame().party, 0x1c4) < 5) {
                    return 0;
                }
                CurrentGame().status.fact_88_latch = true;
                return 1;
            }
            break;
        case W8_FACT_UMISSION_TRAIN_WETSUIT:
            return EveryCharacterHasItem(CurrentGame().party, 0x1e5, W8ItemReach::Worn);
        case W8_FACT_UMISSION_IUFPASS_LEVEL2:
            return PartyHasItem(CurrentGame().party, 0x268);
        case W8_FACT_RAPAX_AWAY_CAMP_EXISTS:
            count = PartyHasItem(CurrentGame().party, 0x242);
            if (PartyHasItem(CurrentGame().party, 0x243)) {
                ++count;
            }
            if (PartyHasItem(CurrentGame().party, 0x244)) {
                ++count;
            }
            return count >= 2;
        default:
            break;
        }
    } else if (fact_id < W8_FACT_TRYNNIE_FOUNTAIN_ANSWERED_CORRECT) {
        if (fact_id == W8_FACT_FACTION_TRYNNIE_COMMON_FRIENDLY) {
            return GetFactionDisposition(W8_FACTION_TRYNNIE) == W8_FACTION_FRIENDLY;
        }
        switch (fact_id) {
        case W8_FACT_TRYNNIE_PC_HAS_HELM:
            return PartyHasItem(CurrentGame().party, 0x239);
        case W8_FACT_FACTION_UMPANI_COMMON_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_UMPANI) == W8_FACTION_FRIENDLY;
        case W8_FACT_FACTION_TRANG_COMMON_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_TRANG) == W8_FACTION_FRIENDLY;
        case W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_BROTHERHOOD) == W8_FACTION_FRIENDLY;
        case W8_FACT_MYLES_IN_PARTY:
            return NpcLeadHasNameStyle(W8_NPC_MYLES);
        case W8_FACT_FATHER_IN_PARTY:
            if (CurrentGame().status.rpc_active) {
                for (unsigned int slot = 0; slot < 8; ++slot) {
                    if (CurrentGame().party.members[slot].has_value() &&
                        slot ==
                            static_cast<unsigned int>(CurrentGame().status.sedexus_party_slot)) {
                        return 1;
                    }
                }
            }
            return 0;
        case W8_FACT_PARTY_HAS_BLOODLUST_SWORD:
            return PartyHasItem(CurrentGame().party, 0x294);
        case W8_FACT_RAPAX_SAVANT_ALLIANCE:
            value = GetFact(W8_FACT_ASTRAL_POSSESS);
            if (value != 0) {
                return 1;
            }
            value = GetFact(W8_FACT_DESTINAE_POSSESS);
            if (value != 0) {
                return 1;
            }
            value = GetFact(W8_FACT_CHAOS_POSSESS);
            if (value != 0) {
                return 1;
            }
            return 0;
        default:
            break;
        }
    } else if (fact_id < W8_FACT_QUEST_SHAMAN_BRIDGE_EASY) {
        if (fact_id == W8_FACT_QUEST_SHAMAN_GET_HELM) {
            return !PartyHasItem(CurrentGame().party, 0x239);
        }
        switch (fact_id) {
        case W8_FACT_FACTION_RAPAX_COMMON_FRIENDLY:
            return GetFactionDisposition(W8_FACTION_RAPAX_COMMON) == W8_FACTION_FRIENDLY;
        case W8_FACT_TRYNNIE_SPARKLE_IN_PARTY:
            return NpcLeadHasNameStyle(W8_NPC_SPARKLE);
        case W8_FACT_VI_IS_DEAD: {
            if (!NpcLeadHasNameStyle(W8_NPC_VI_DOMINA)) {
                return CurrentGame().story.facts[fact_id];
            }
            W8NpcState* npc = GetNpcStateByKind(0x18);
            if (npc != 0 && CurrentGame().party.characters[npc->group_index].highest_condition >=
                                W8_CONDITION_ASLEEP) {
                return 1;
            }
            return 0;
        }
        case W8_FACT_QUEST_SHAMAN_DESTINY_TWO:
            value = GetFact(W8_FACT_QUEST_MARTEN_DIARY);
            if (value == 0) {
                return 1;
            }
            value = GetFact(W8_FACT_QUEST_MARTEN_IDOL);
            if (value == 0) {
                return 1;
            }
            return 0;
        default:
            break;
        }
    } else {
        if (fact_id == W8_FACT_QUEST_PEACE_UNLOCK_CAGES) {
            value = GetFact(W8_FACT_RODAN_LOCKED_IN_CAGE);
            if (value != 0) {
                return 1;
            }
            value = GetFact(W8_FACT_DRAZIC_LOCKED_IN_CAGE);
            if (value != 0) {
                return 1;
            }
            return 0;
        }
        if (fact_id == W8_FACT_FACTION_RAPAX_TEMPLAR_FRIENDLY) {
            return GetFactionDisposition(W8_FACTION_RAPAX_TEMPLAR) == W8_FACTION_FRIENDLY;
        }
        if (fact_id == W8_FACT_PARTY_DEACTIVATED_BOMB) {
            value = GetFact(W8_FACT_DS_BOMB_DEACTIVATED);
            return value;
        }
    }
    return CurrentGame().story.facts[fact_id];
}

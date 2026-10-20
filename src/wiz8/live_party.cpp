#include "wiz8/live_party.h"

#include <bitset>

#include "wiz8/character_skills.h"
#include "wiz8/character_stats.h"
#include "wiz8/data/records.h"
#include "wiz8/fact_state.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/party_stats.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/xstatus.h"

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

void RecalculateHitPoints(W8Character& character)
{
    if (!RecalculateCharacterHitPoints(&character)) return;
    if (const auto slot = g_status.party.slotOf(character))
        SetCharacterCondition(*slot, W8_CONDITION_DEAD, W8_CONDITION_INDEFINITE, 0, 0, 1);
}

void RecalculateArmorClass(W8Character& character)
{
    CalcArmorClasses(&character, CaptureLiveCharacterStatRules(character).defensive_action);
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

void PresentInventoryWeightChange(W8Character& character, InventoryWeightChange change)
{
    if (change.became_overloaded) {
        const int event = CurrentScreenState().id == W8_SCREEN_CAMP ? 8 : g_special_event13;
        QueueCharacterEvent(&character, event, 0, kCharacterEventFullVolume);
    }
}
void PresentPartyLoadChange(bool redistributed)
{
    if (!redistributed) return;
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && !gXStatus.fNpcDialogueMode) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
    } else if (CurrentScreenState().id == W8_SCREEN_CAMP && CampScreenState()) {
        CampScreenState()->redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO | W8_CAMP_REDRAW_PORTRAIT;
    }
}

void RedistributePartyEncumbrance()
{
    if (g_status.game_started)
        PresentPartyLoadChange(RefreshPartyLoad(g_status.party, CaptureLivePartyStatRules()));
}

/* Combat choices that depend on the weapons in hand follow a change of them: berserk
   follows its eligibility, an attack keeps a reachable target, and the hand-attack
   values are rebuilt through the alternate-hand toggle. */
static void ReconcileHandChange(unsigned int party_slot)
{
    W8PartySlotRow* row = &g_status.party.rows[party_slot];
    W8ActionKind action_kind = row->action_kind;
    if (action_kind == W8_ACTION_BERSERK) {
        if (!CanCharacterBerserk(party_slot)) {
            row->action_kind = W8_ACTION_ATTACK;
            if (row->committed_action.kind == W8_ACTION_BERSERK) {
                row->committed_action.kind = W8_ACTION_ATTACK;
            }
        }
    } else if (action_kind == W8_ACTION_ATTACK && row->action_is_berserk &&
               CanCharacterBerserk(party_slot)) {
        row->action_kind = W8_ACTION_BERSERK;
        if (row->committed_action.kind == W8_ACTION_ATTACK) {
            row->committed_action.kind = W8_ACTION_BERSERK;
        }
    }

    if (g_combat.active()) {
        W8ActionKind action = row->committed_action.kind;
        if (action == W8_ACTION_ATTACK || action == W8_ACTION_BERSERK) {
            if (!CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, true, false)) {
                AimByKind(party_slot, W8_TARGET_KIND_NONE, W8_TARGETING_CONTEXT_IN_COMBAT);
            } else if (!TargetIsInPlay(
                           party_slot, 2,
                           SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT))) {
                RepickActionTarget(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
            }
        }
        g_combat.state()->characters[party_slot].alternate_hand ^= 1;
        if (party_slot == static_cast<unsigned int>(g_status.selected_character)) {
            RequestRefreshPartyState();
        }
    }
    RequestRedraw(1 << (party_slot & 0x1f));
}

void SettleActiveParty(std::bitset<W8_PARTY_SLOT_COUNT> changed_hands)
{
    auto& party = g_status.party;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.rows[slot].fOccupied && IsCharacterDefined(party.characters[slot]) &&
            UnequipUnusableItems(slot))
            changed_hands.set(slot);
    const auto stats = UpdatePartyStats(party, CaptureLivePartyStatRules());
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.rows[slot].fOccupied) continue;
        auto& character = party.characters[slot];
        if (changed_hands[slot] && character.fInParty && g_status.game_started)
            ReconcileHandChange(slot);
        PresentInventoryWeightChange(character, stats.characters[slot].weight);
        if (character.fInParty && CurrentScreenState().id != W8_SCREEN_CHARACTER)
            RequestPartySlotRedraw(slot);
    }
    PresentPartyLoadChange(stats.load_redistributed);
    // Death is a condition change with consequences of its own.
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (stats.characters[slot].hp_exhausted)
            SetCharacterCondition(slot, W8_CONDITION_DEAD, W8_CONDITION_INDEFINITE, 0, 0, 1);
}

void ApplyPartyModifiers(W8GameplayModifierBlock modifiers)
{
    g_status.party.modifiers = modifiers;
    SettleActiveParty();
}

void RecalculateCharacterDerivedStats(W8Character& character)
{
    if (!IsCharacterDefined(character)) return;
    if (character.uiExpLevel > 50) character.uiExpLevel = 50;
    for (auto& level : character.profession_levels)
        if (static_cast<unsigned int>(level) > 50) level = 50;
    if (const auto member = g_status.party.slotOf(character);
        member && g_status.party.rows[*member].fOccupied) {
        SettleActiveParty();
        return;
    }
    UpdateStandaloneCharacterStats(character, {});
}

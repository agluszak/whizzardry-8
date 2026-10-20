#include "wiz8/live_party.h"

#include <bitset>

#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"

// The game's inputs to statistic calculations: combat actions and NPC story state.
static CharacterStatRules CaptureLiveCharacterStatRules(const Game& game,
                                                        const W8Character& character)
{
    CharacterStatRules rules;
    const auto slot = game.party.slotOf(character);
    if (!slot) return rules;
    if (game.combat.has_value())
        rules.defensive_action = TryCharacterAction(*slot, W8_ACTION_DEFEND, false) ||
                                 TryCharacterAction(*slot, W8_ACTION_PROTECT, false);
    if (character.fInParty && character.iRace == W8_RACE_ANDROID) {
        const auto* npc = GetNpcState(game.party.rows[*slot].npc_index);
        rules.damaged_android =
            npc && npc->name_style == W8_NPC_RFS81_A && !GetFact(W8_FACT_RFS81_HAS_BEEN_FIXED);
    }
    return rules;
}

static PartyStatRules CaptureLivePartyStatRules(const Game& game)
{
    PartyStatRules rules;
    if (!game.status.game_started)
        rules.load_timing = PartyLoadTiming::Disabled;
    else if (game.combat.has_value())
        rules.load_timing = PartyLoadTiming::AfterCombat;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (game.party.rows[slot].fOccupied)
            rules.characters[slot] =
                CaptureLiveCharacterStatRules(game, game.party.characters[slot]);
    return rules;
}

// A member who became overloaded says so.
static void PresentOverload(W8Character& character)
{
    const int event = CurrentScreenState().id == W8_SCREEN_CAMP ? 8 : g_special_event13;
    QueueCharacterEvent(&character, event, 0, kCharacterEventFullVolume);
}

void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands)
{
    auto& party = game.party;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.rows[slot].fOccupied && IsCharacterDefined(party.characters[slot]) &&
            UnequipUnusableItems(game, slot))
            changed_hands.set(slot);
    const auto stats = UpdatePartyStats(Data(), party, CaptureLivePartyStatRules(game));
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.rows[slot].fOccupied) continue;
        auto& character = party.characters[slot];
        // Combat choices that depend on the weapons in hand follow a change of them.
        if (changed_hands[slot] && character.fInParty && game.status.game_started) {
            auto& row = party.rows[slot];
            ReconcileBerserkChoice(row.remembered, row.committed, CanCharacterBerserk(slot));
            ReconcileCombatHandChange(game, slot);
            game.events.push(PartySlotChanged{slot});
        }
        if (stats.characters[slot].became_overloaded) PresentOverload(character);
        if (character.fInParty) game.events.push(PartySlotChanged{slot});
    }
    if (stats.load_redistributed) game.events.push(PartyLoadChanged{});
    // Death is a condition change with consequences of its own.
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (stats.characters[slot].hp_exhausted)
            SetCharacterCondition(slot, W8_CONDITION_DEAD, W8_CONDITION_INDEFINITE, 0, 0, 1);
}

void RecalculateCharacterDerivedStats(Game& game, W8Character& character)
{
    if (!IsCharacterDefined(character)) return;
    if (const auto member = game.party.slotOf(character);
        member && game.party.rows[*member].fOccupied) {
        SettleActiveParty(game);
        return;
    }
    UpdateStandaloneCharacterStats(Data(), character, {});
}

void RecalculateEditedCharacterStats(Game& game, W8Character& character)
{
    const auto slot = game.party.slotOf(character);
    const bool member = slot && game.party.rows[*slot].fOccupied;
    if (!member) character.party_weight_share = 0;
    const auto update = UpdateCharacterStatsFromInputs(
        Data(), character,
        member ? CaptureLiveCharacterStatRules(game, character) : CharacterStatRules{});
    if (update.became_overloaded) PresentOverload(character);
    if (member && update.hp_exhausted)
        SetCharacterCondition(*slot, W8_CONDITION_DEAD, W8_CONDITION_INDEFINITE, 0, 0, 1);
}

#include "wiz8/live_party.h"

#include <bitset>
#include <optional>
#include <utility>

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
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/rule_context.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/world/level_session.h"

// The game's inputs to statistic calculations: combat actions and NPC story state.
static CharacterStatRules CaptureLiveCharacterStatRules(const Game& game, unsigned int slot)
{
    CharacterStatRules rules;
    const auto& character = game.party.characters[slot];
    if (game.combat.has_value())
        rules.defensive_action = TryCharacterAction(slot, W8_ACTION_DEFEND, false) ||
                                 TryCharacterAction(slot, W8_ACTION_PROTECT, false);
    if (character.fInParty && character.iRace == W8_RACE_ANDROID) {
        const auto* npc = GetNpcState(game.party.npcIndex(slot));
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
        if (game.party.members[slot].has_value())
            rules.characters[slot] = CaptureLiveCharacterStatRules(game, slot);
    return rules;
}

// A member who became overloaded says so.
static void PresentOverload(const Game& game, W8Character& character)
{
    const int event = game.activity.in_camp() ? 8 : g_special_event13;
    QueueCharacterEvent(&character, event, 0, kCharacterEventFullVolume);
}

namespace {
/* The game around its party: what the party rules read from the game, and what they owe
   its fight, story, world, speech and interface. */
class LivePartyHost final : public PartyHost {
public:
    explicit LivePartyHost(Game& game) : game_(game) {}

    PartyStatRules statRules() override { return CaptureLivePartyStatRules(game_); }
    bool worldSuspended() override { return game_.status.world_suspended; }
    int levelBand() override { return GetLevelBand(GetLoadedLevelID()); }
    std::optional<int> swallowerLevel(const W8CharacterConditionRecord& record) override
    {
        if (record.level_acquired != GetLoadedLevelID()) return std::nullopt;
        auto* monster = FindMonster(record.source_monster, false);
        if (!monster) return std::nullopt;
        return GetMonsterDataForInfo(monster)->effective_level;
    }
    bool spendFirstCombatRevival(unsigned int slot) override
    {
        if (!game_.combat.has_value() || game_.combat->characters[slot].cheat_death_used)
            return false;
        game_.combat->characters[slot].cheat_death_used = true;
        return true;
    }

    bool unequipUnusable(unsigned int slot) override { return UnequipUnusableItems(game_, slot); }
    void handsChanged(unsigned int slot) override
    {
        auto& row = game_.party.rows[slot];
        ReconcileBerserkChoice(row.remembered, row.committed, CanCharacterBerserk(slot));
        ReconcileCombatHandChange(game_, slot);
    }
    void overloaded(unsigned int slot) override
    {
        PresentOverload(game_, game_.party.characters[slot]);
    }
    void conditionStarted(unsigned int slot, W8Condition condition) override
    {
        if (condition == W8_CONDITION_HEXED || condition == W8_CONDITION_BLIND)
            game_.sight_refresh_pending = true;
        else if (condition == W8_CONDITION_TURNCOAT)
            SetTargetToCharacter(slot, true);
    }
    void conditionLifted(unsigned int slot, W8Condition condition) override
    {
        auto& row = game_.party.rows[slot];
        switch (condition) {
        case W8_CONDITION_HEXED:
        case W8_CONDITION_BLIND:
            game_.sight_refresh_pending = true;
            break;
        case W8_CONDITION_INSANE:
            // A berserker who comes to their senses goes back to their chosen target.
            if (game_.combat.has_value() && game_.combat->characters[slot].berserk)
                row.executing.target = row.committed.target;
            break;
        case W8_CONDITION_TURNCOAT:
            SetTargetToCharacter(slot, true);
            RepickActionTarget(slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, 0);
            RepickActionTarget(slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
            break;
        default:
            break;
        }
    }
    void standingChanged(unsigned int slot) override
    {
        // A member who can no longer hold a seat leaves it, and one who can again returns.
        UpdateFormationSlotState(&game_.party.formation, slot);
        if (game_.combat.has_value()) {
            UpdateFormationSlotState(&game_.planned_formation, slot);
            UpdateFormationSlotState(&game_.combat->saved_formation, slot);
        }
        // The portrait lies down from sleep upward and gets up again below it.
        auto& entry = game_.monster_manager_entries[slot];
        const bool lying = game_.party.characters[slot].highest_condition >= W8_CONDITION_ASLEEP;
        const bool lies = entry.portrait.target_pose == 2;
        if (lying != lies) SetPortraitTargetPose(&entry, lying ? 2 : 1);
    }
    void enchantmentChanged(unsigned int, W8EnchantmentSlot enchantment) override
    {
        if (enchantment == W8_ENCHANTMENT_SUPERMAN) game_.sight_refresh_pending = true;
    }
    void died(unsigned int slot) override
    {
        if (game_.combat.has_value()) RecordCharacterDeath(slot);
        QueuePartyDeathReaction(slot);
        if (game_.combat.has_value()) {
            game_.combat->endTurnOf(PartyActor{static_cast<int>(slot)});
            game_.party.rows[slot].executing.kind = W8_ACTION_NONE;
            game_.combat->characters[slot].phase = 0;
            game_.combat->characters[slot].dead = true;
            DropCharacterFromRound(slot);
        }
        if (auto* npc = GetNpcState(game_.party.npcIndex(slot))) npc->spawned = 1;
    }
    void incapacitated(unsigned int slot) override { SetTargetToCharacter(slot, false); }
    void conditionReaction(unsigned int slot) override
    {
        QueueConditionChangeReaction(&game_.party.characters[slot]);
    }
    void recoveryReaction(unsigned int slot, W8Condition condition) override
    {
        QueueConditionClearedReaction(&game_.party.characters[slot], condition);
    }

private:
    Game& game_;
};

// Apply one party rule to the game's party with the game as its host.
template<class Rule> decltype(auto) WithLiveParty(Game& game, Rule&& rule)
{
    auto rules = game.rules();
    LivePartyHost host(game);
    return std::forward<Rule>(rule)(rules, host);
}
} // namespace

void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        SettleParty(rules, host, changed_hands);
    });
}

bool InflictCondition(Game& game, unsigned int slot, const ConditionInfliction& infliction)
{
    return WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        return InflictCondition(rules, host, slot, infliction);
    });
}

void LiftCondition(Game& game, unsigned int slot, W8Condition condition, bool announce)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        LiftCondition(rules, host, slot, condition, announce);
    });
}

void AgeCondition(Game& game, unsigned int slot, W8Condition condition, unsigned int minutes)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        AgeCondition(rules, host, slot, condition, minutes);
    });
}

void LiftConditionFromParty(Game& game, W8Condition condition)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        LiftConditionFromParty(rules, host, condition);
    });
}

void LiftAllConditionsFromParty(Game& game)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        LiftAllConditionsFromParty(rules, host);
    });
}

void ReviveByCheatingDeath(Game& game, unsigned int slot)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        ReviveByCheatingDeath(rules, host, slot);
    });
}

void Enchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment, int power,
             unsigned int turns, unsigned int percent)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        Enchant(rules, host, slot, enchantment, power, turns, percent);
    });
}

void Disenchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        Disenchant(rules, host, slot, enchantment);
    });
}

void AgeEnchantment(Game& game, unsigned int slot, W8EnchantmentSlot enchantment,
                    unsigned int turns)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        AgeEnchantment(rules, host, slot, enchantment, turns);
    });
}

void RefreshConditionModifiers(Game& game, unsigned int slot)
{
    WithLiveParty(game, [&](RuleContext& rules, PartyHost& host) {
        RebuildConditionModifiers(rules, host, slot);
        SettleParty(rules, host);
    });
}

void RecalculateCharacterDerivedStats(Game& game, W8Character& character)
{
    if (!IsCharacterDefined(character)) return;
    // Character creation edits a party storage slot before the slot has a member.
    if (const auto slot = game.party.slotOf(character); slot && game.party.members[*slot]) {
        SettleActiveParty(game);
        return;
    }
    UpdateStandaloneCharacterStats(Data(), character, {});
}

void RecalculateEditedCharacterStats(W8Character& draft)
{
    draft.party_weight_share = 0;
    const auto update = UpdateCharacterStatsFromInputs(Data(), draft, {});
    if (update.became_overloaded) PresentOverload(CurrentGame(), draft);
}

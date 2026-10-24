#include "wiz8/live_party.h"

#include <bitset>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/portrait_animation.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/story.h"
#include "wiz8/world/world.h"

// The game's inputs to statistic calculations: combat actions and NPC story state.
static CharacterStatRules CaptureLiveCharacterStatRules(const Game& game, unsigned int slot)
{
    CharacterStatRules rules;
    const auto& character = game.party.characters[slot];
    if (game.combat.has_value())
        rules.defensive_action = TryCharacterAction(slot, W8_ACTION_DEFEND, false) ||
                                 TryCharacterAction(slot, W8_ACTION_PROTECT, false);
    if (character.fInParty && character.iRace == W8_RACE_ANDROID) {
        const auto* npc = game.story.npc(game.party.npcIndex(slot));
        rules.damaged_android = npc && npc->name_style == W8_NPC_RFS81_A &&
                                !game.story.facts[W8_FACT_RFS81_HAS_BEEN_FIXED];
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
static void PresentOverload(Game& game, W8Character& character)
{
    const int event = game.activity.in_camp() ? 8 : g_special_event13;
    game.speech.Queue(&character, event, 0, kCharacterEventFullVolume);
}

namespace {
void PushSlotDisplayChanged(Events& events, unsigned int slot)
{
    events.push(PartySlotChanged{slot});
    events.push(CharacterActionChanged{});
    events.push(PartyPanelChanged{});
}

void UpdateFormationPresence(Game& game, W8PartyFormationState& formation, unsigned int slot)
{
    const int previous = formation.rowOf(slot);
    const int remembered = formation.positions[slot].revival_row;
    formation.updateLiveness(slot,
                             game.party.characters[slot].highest_condition >= W8_CONDITION_DEAD);
    const int row = formation.rowOf(slot);
    if (row == previous || !game.status.game_started || game.activity.conversation) return;
    const bool live = &formation == &game.party.formation;
    if (live) game.events.push(FormationChanged{});
    if (row != -1 && (live || row != remembered))
        game.events.push(
            CharacterNotice{slot, fmt::sprintf(game.data.strings[0x24c], kFormationRowNames[row])});
}

/* Rescan member `slot`'s highest condition. A member who can no longer hold a formation
   seat leaves it, and one who can again returns; the portrait lies down from sleep upward
   and gets up again below it. */
void RecomputeHighestCondition(Game& game, unsigned int slot)
{
    auto& character = game.party.characters[slot];
    character.highest_condition = HighestCondition(character);
    UpdateFormationPresence(game, game.party.formation, slot);
    if (game.combat.has_value()) {
        UpdateFormationPresence(game, game.planned_formation, slot);
        UpdateFormationPresence(game, game.combat->saved_formation, slot);
    }
    auto& entry = game.monster_manager_entries[slot];
    const bool lying = character.highest_condition >= W8_CONDITION_ASLEEP;
    const bool lies = entry.portrait.target_pose == 2;
    if (lying != lies) entry.portrait.setTargetPose(lying ? 2 : 1, lying || game.surprise_possible);
    PushSlotDisplayChanged(game.events, slot);
}

/* The level of the monster that swallowed the member, when it is on the loaded level;
   otherwise a swallowed member regenerates at the default penalty. */
std::optional<int> SwallowerLevel(const Game& game, const W8Character& character)
{
    if (!Swallowed(character)) return std::nullopt;
    const auto& record = character.conditions[W8_DEPENDENCE_SWALLOWED];
    const auto* world = game.level.primary.get();
    if (!world || !world->population || record.level_acquired != world->level_id)
        return std::nullopt;
    const auto* monster = world->population->findMonster(record.source_monster);
    if (!monster) return std::nullopt;
    return game.data.monsters[monster->monster_species].effective_level;
}

/* A cheat-death revival in a fight restores a share of the maximum the first time; true
   when that share is still owed, which it no longer is afterwards. */
bool SpendFirstCombatRevival(Game& game, unsigned int slot)
{
    if (!game.combat.has_value() || game.combat->characters[slot].cheat_death_used) return false;
    game.combat->characters[slot].cheat_death_used = true;
    return true;
}

/* Clear the condition and what it alone maintained; true when it was lifted. A dying
   member's conditions are cleared without settling each time. */
bool ClearCondition(Game& game, unsigned int slot, W8Condition condition, bool announce)
{
    auto& character = game.party.characters[slot];
    if (character.uiCondition[condition] == 0 && game.status.world_suspended) return false;
    assert(game.party.members[slot].has_value());
    assert(character.uiCondition[condition] != 0);
    if (condition == W8_CONDITION_HEXED &&
        InfatuationHoldsHex(character,
                            game.levelBand(game.level.primary ? game.level.primary->level_id : -1)))
        return false;
    if (condition == W8_CONDITION_INSANE &&
        MadnessRingHoldsInsanity(game.party, game.inventory.find(ItemWithId(kItemMadnessRing)),
                                 slot))
        return false;
    const auto& strings = game.data.strings;
    if (announce) {
        game.events.push(CharacterNotice{
            slot, fmt::sprintf(strings[0x243], strings[kConditionNotices[condition].name])});
    }
    character.uiCondition[condition] = 0;
    RecomputeHighestCondition(game, slot);
    auto& row = game.party.rows[slot];
    switch (condition) {
    case W8_CONDITION_POISONED:
        character.condition_argument = 0;
        break;
    case W8_CONDITION_DRAINED:
        character.hp_adjustment = 0;
        character.fatigue_penalty = 0;
        break;
    case W8_CONDITION_HEXED:
    case W8_CONDITION_BLIND:
        game.sight_refresh_pending = true;
        break;
    case W8_CONDITION_INSANE:
        // A berserker who comes to their senses goes back to their chosen target.
        if (game.combat.has_value() && game.combat->characters[slot].berserk)
            row.executing.target = row.committed.target;
        break;
    case W8_CONDITION_TURNCOAT:
        game.invalidateAimsAt(W8CombatSlot::character(slot), TargetChange::allegiance);
        RepickActionTarget(slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, 0);
        RepickActionTarget(slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
        break;
    default:
        break;
    }
    return true;
}

/* Death's own consequences, after the death condition took hold: its conditions clear,
   its pools empty, and its fight, its NPC and the party react. */
void Die(Game& game, unsigned int slot)
{
    auto& character = game.party.characters[slot];
    assert(game.party.members[slot].has_value());
    ++character.death_count;
    bool lifted = false;
    for (unsigned int condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition) {
        if (condition != W8_CONDITION_INFATUATED && character.uiCondition[condition] != 0 &&
            ClearCondition(game, slot, static_cast<W8Condition>(condition), false)) {
            game.speech.QueueConditionClearedReaction(&character,
                                                      static_cast<W8Condition>(condition));
            lifted = true;
        }
    }
    if (lifted) RefreshConditionModifiers(game, slot);
    character.hp_current = 0;
    character.stamina = 0;
    auto& row = game.party.rows[slot];
    row.executing.target = W8CombatSlot{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    row.committed.target = W8CombatSlot{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    game.events.push(CharacterDied{slot});
    if (game.combat) game.combat->retirePartyMember(slot);
    game.speech.QueuePartyDeathReaction(slot);
    if (game.combat.has_value()) {
        const bool was_equipping = row.committed.kind == W8_ACTION_EQUIP;
        row.executing.kind = W8_ACTION_NONE;
        row.committed = row.remembered.basic();
        game.events.push(PartySlotChanged{slot});
        if (slot == static_cast<unsigned int>(game.status.selected_character))
            game.events.push(MonstersChanged{});
        if (was_equipping) game.events.push(CharacterNotice{slot, game.data.strings[0x226]});
        SettleActiveParty(game);
    }
    if (auto* npc = game.story.npc(game.party.npcIndex(slot))) npc->spawned = 1;
}
} // namespace

void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands)
{
    Events::Operation outcomes{game.events};
    auto& party = game.party;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value() && IsCharacterDefined(party.characters[slot]) &&
            game.inventory.unequipUnusable(slot))
            changed_hands.set(slot);
    const auto stats =
        UpdatePartyStats(game.data, party, game.inventory.pool(), CaptureLivePartyStatRules(game));
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value()) continue;
        auto& character = party.characters[slot];
        if (changed_hands[slot] && character.fInParty && game.status.game_started) {
            // Combat choices follow a change of the weapons in the member's hands.
            auto& row = party.rows[slot];
            ReconcileBerserkChoice(row.remembered, row.committed,
                                   CanCharacterBerserk(game.data, &character));
            ReconcileCombatHandChange(game, slot);
            game.events.push(PartySlotChanged{slot});
        }
        if (stats.characters[slot].became_overloaded) PresentOverload(game, character);
        if (character.fInParty) game.events.push(PartySlotChanged{slot});
    }
    if (stats.load_redistributed) game.events.push(PartyLoadChanged{});
    // Death is a condition change with consequences of its own.
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (stats.characters[slot].hp_exhausted)
            InflictCondition(game, slot,
                             {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE});
}

bool InflictCondition(Game& game, unsigned int slot, const ConditionInfliction& infliction)
{
    Events::Operation outcomes{game.events};
    auto& character = game.party.characters[slot];
    const auto& strings = game.data.strings;
    const auto condition = infliction.condition;
    assert(game.party.members[slot].has_value());
    assert(infliction.poison_strength == 0 || condition == W8_CONDITION_POISONED);
    if (condition == W8_CONDITION_POISONED && infliction.poison_strength == 0) return false;
    if (condition == W8_CONDITION_DEAD && CheatsDeath(character)) {
        ReviveByCheatingDeath(game, slot);
        return false;
    }
    const int duration = ConditionDuration(condition, infliction.duration);
    if (duration == 0) return false;
    const auto refuse = [&](std::string text) {
        game.events.push(CharacterNotice{slot, std::move(text)});
        return false;
    };
    if (game.status.world_suspended)
        return refuse(fmt::sprintf(strings[0x242], strings[kConditionNotices[condition].name]));
    switch (ResistCondition(character, condition, duration)) {
    case ConditionResistance::None:
        break;
    case ConditionResistance::Silent:
        return false;
    case ConditionResistance::Fearless:
        return refuse(strings[0x180]);
    case ConditionResistance::MentallyImmune:
        return refuse(strings[0x181]);
    }
    const auto old_highest = character.highest_condition;
    const unsigned int old_duration = character.uiCondition[condition];
    const bool took_hold = old_duration < static_cast<unsigned int>(duration);
    if (took_hold) {
        if (old_duration == 0) {
            if (condition == W8_CONDITION_HEXED || condition == W8_CONDITION_BLIND)
                game.sight_refresh_pending = true;
            else if (condition == W8_CONDITION_TURNCOAT)
                game.invalidateAimsAt(W8CombatSlot::character(slot), TargetChange::allegiance);
        }
        character.uiCondition[condition] = duration;
        RecomputeHighestCondition(game, slot);
    }
    if (static_cast<unsigned int>(character.condition_argument) <
        static_cast<unsigned int>(infliction.poison_strength)) {
        character.condition_argument = infliction.poison_strength;
    }
    RefreshConditionModifiers(game, slot);
    if (!took_hold) return false;
    if (condition == W8_CONDITION_DEAD) Die(game, slot);
    // A member who is dead or missing is no longer the party's target.
    if (condition >= W8_CONDITION_DEAD)
        game.invalidateAimsAt(W8CombatSlot::character(slot), TargetChange::removed);
    if (old_highest != character.highest_condition)
        game.speech.QueueConditionChangeReaction(&character);
    if (infliction.announce) {
        if (condition == W8_CONDITION_MISSING && infliction.alternate_missing_notice) {
            game.events.push(CharacterNotice{slot, strings[0x1d5]});
        } else {
            game.events.push(CharacterNotice{
                slot, fmt::format("{}!", strings[kConditionNotices[condition].singular])});
        }
    }
    return true;
}

void LiftCondition(Game& game, unsigned int slot, W8Condition condition, bool announce)
{
    Events::Operation outcomes{game.events};
    if (!ClearCondition(game, slot, condition, announce)) return;
    RefreshConditionModifiers(game, slot);
    game.speech.QueueConditionClearedReaction(&game.party.characters[slot], condition);
}

void AgeCondition(Game& game, unsigned int slot, W8Condition condition, unsigned int minutes)
{
    auto& character = game.party.characters[slot];
    assert(game.party.members[slot].has_value());
    assert(character.uiCondition[condition] != 0);
    if (character.uiCondition[condition] <= minutes) {
        LiftCondition(game, slot, condition, true);
        return;
    }
    if (condition == W8_CONDITION_POISONED) {
        character.condition_argument = AgedPoisonStrength(
            game.rng, character.condition_argument, character.uiCondition[condition], minutes);
    }
    character.uiCondition[condition] -= minutes;
}

void LiftConditionFromParty(Game& game, W8Condition condition)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (game.party.members[slot].has_value() &&
            game.party.characters[slot].uiCondition[condition] != 0)
            LiftCondition(game, slot, condition, true);
}

void LiftAllConditionsFromParty(Game& game)
{
    for (unsigned int condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition)
        if (condition != W8_CONDITION_INFATUATED)
            LiftConditionFromParty(game, static_cast<W8Condition>(condition));
}

void ReviveByCheatingDeath(Game& game, unsigned int slot)
{
    Events::Operation outcomes{game.events};
    auto& character = game.party.characters[slot];
    game.events.push(CharacterNotice{slot, game.data.strings[0x173]});
    InflictCondition(
        game, slot,
        {.condition = W8_CONDITION_UNCONSCIOUS, .duration = CheatDeathUnconsciousTurns(character)});
    const bool first_in_fight = SpendFirstCombatRevival(game, slot);
    character.hp_current = CheatDeathHitPoints(game.rng, character, first_in_fight);
}

void Enchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment, int power,
             unsigned int turns, unsigned int percent)
{
    if (StrengthenEnchantment(game.rng, game.data, game.party.characters[slot], enchantment, power,
                              turns, percent)) {
        PushSlotDisplayChanged(game.events, slot);
        if (enchantment == W8_ENCHANTMENT_SUPERMAN) game.sight_refresh_pending = true;
    }
    RefreshConditionModifiers(game, slot);
}

void Disenchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment)
{
    auto& character = game.party.characters[slot];
    character.enchantments[enchantment] = {};
    character.enchantment_top = TopEnchantment(character);
    PushSlotDisplayChanged(game.events, slot);
    RefreshConditionModifiers(game, slot);
    if (enchantment == W8_ENCHANTMENT_SUPERMAN) game.sight_refresh_pending = true;
}

void AgeEnchantment(Game& game, unsigned int slot, W8EnchantmentSlot enchantment,
                    unsigned int turns)
{
    auto& entry = game.party.characters[slot].enchantments[enchantment];
    if (entry.turns <= turns) {
        Disenchant(game, slot, enchantment);
        return;
    }
    entry.turns -= turns;
}

void RefreshConditionModifiers(Game& game, unsigned int slot)
{
    auto& character = game.party.characters[slot];
    const auto& member = game.party.members[slot];
    character.condition_modifiers =
        ConditionModifiers(character, member && member->npc_bound, SwallowerLevel(game, character));
    SettleActiveParty(game);
}

void RecalculateCharacterDerivedStats(Game& game, W8Character& character)
{
    if (!IsCharacterDefined(character)) return;
    // Character creation edits a party storage slot before the slot has a member.
    if (const auto slot = game.party.slotOf(character); slot && game.party.members[*slot]) {
        SettleActiveParty(game);
        return;
    }
    UpdateStandaloneCharacterStats(game.data, character, {});
}

void RecalculateEditedCharacterStats(Game& game, W8Character& draft)
{
    draft.party_weight_share = 0;
    const auto update = UpdateCharacterStatsFromInputs(game.data, draft, {});
    if (update.became_overloaded) PresentOverload(game, draft);
}

unsigned int Game::damageCharacter(unsigned int party_slot, unsigned int amount,
                                   DamageOptions options, W8SpellEffectResult* result_stats)
{
    Events::Operation outcomes{events};
    W8Character* character = &party.characters[party_slot];

    assert(party.members[party_slot].has_value());
    if (character->hp_current == 0) {
        return 0;
    }
    if (status.world_suspended) {
        events.push(CharacterNotice{party_slot, fmt::sprintf(data.strings[0x253], amount)});
        return 0;
    }

    if (const auto absorbed = character->absorbDamage(amount)) {
        events.push(CharacterNotice{party_slot,
                                    fmt::sprintf(data.strings[0x193 - options.poison], *absorbed)});
        if (*absorbed == amount) {
            if (result_stats) ++result_stats->count;
            return 0;
        }
        amount -= *absorbed;
        Disenchant(*this, party_slot, W8_ENCHANTMENT_GUARDIAN_ANGEL);
        events.push(CharacterNotice{party_slot, data.strings[0x194]});
    }

    fatigueCharacter(party_slot, (amount * 2) / 3, false, result_stats);
    if (result_stats != 0) {
        result_stats->amount += amount;
        ++result_stats->count;
    }

    if (options.notice != DamageNotice::None) {
        if (options.notice == DamageNotice::Detailed) {
            events.push(CharacterNotice{party_slot, fmt::sprintf(data.strings[0x268], amount)});
        } else if (options.notice == DamageNotice::AmountOnly) {
            events.push(
                TextNotice{W8_FONT_PALETTE_RUST, fmt::sprintf(data.strings[0x254], amount)});
        } else {
            events.push(CharacterNotice{party_slot,
                                        fmt::sprintf(data.strings[0x255], amount,
                                                     options.poison ? data.strings[0x257] : "")});
        }
    }

    const auto previous_hp = character->hp_current;
    if (previous_hp <= amount && CheatsDeath(*character)) {
        ReviveByCheatingDeath(*this, party_slot);
        if (!surprise_possible)
            events.push(CharacterDamaged{party_slot, amount, character->hp_current == 0});
        return previous_hp;
    }

    amount = character->takeDamage(amount);
    if (!surprise_possible)
        events.push(CharacterDamaged{party_slot, amount, character->hp_current == 0});
    if (character->hp_current != 0) {
        if (!surprise_possible) {
            speech.QueueDamageReactionEvents(character);
        }
    } else {
        if (result_stats != 0) {
            auto report = std::make_unique<W8SpellDamageReport>();
            report->kind = W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH;
            report->value = party_slot;
            result_stats->reports.push_back(std::move(report));
        }
        InflictCondition(*this, party_slot,
                         {.condition = W8_CONDITION_DEAD,
                          .duration = W8_CONDITION_INDEFINITE,
                          .announce = result_stats == 0});
    }

    if (character->uiCondition[W8_CONDITION_ASLEEP] != 0 && !options.poison &&
        rng.below(100) < (character->attributes[W8_ATTRIBUTE_SENSES].effective >> 1) + 0x32) {
        LiftCondition(*this, party_slot, W8_CONDITION_ASLEEP, true);
    }
    return amount;
}

void Game::fatigueCharacter(unsigned int slot, int amount, bool scale_by_load,
                            W8SpellEffectResult* report)
{
    auto& character = party.characters[slot];
    if (character.stamina <= 0 || character.hp_current == 0 || amount == 0) return;
    const bool band_changed = character.spendStamina(amount, scale_by_load);
    const int spent_band = character.fatigue_band;
    if (band_changed) SettleActiveParty(*this);
    if (character.stamina < 1) {
        if (character.uiCondition[W8_CONDITION_UNCONSCIOUS] < W8_CONDITION_INDEFINITE) {
            InflictCondition(*this, slot,
                             {.condition = W8_CONDITION_UNCONSCIOUS,
                              .duration = W8_CONDITION_INDEFINITE,
                              .announce = report == nullptr});
            if (report) ++report->condition_counts[W8_CONDITION_UNCONSCIOUS];
        }
    } else if (band_changed && spent_band > 2) {
        if (!character.deep_fatigue_applied) {
            speech.Queue(&character, 4, 0, kCharacterEventFullVolume);
            character.deep_fatigue_applied = true;
        }
        if (static_cast<unsigned int>(character.fatigue_band) < 2)
            character.deep_fatigue_applied = false;
    }
}

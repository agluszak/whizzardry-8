#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"

/* Local Code\Health Stamina Mana.cpp, named by the assertions these bodies
   embed. The party sweeps in here all share one shape: walk the eight party
   slots, skip the empty ones, and hand each occupied slot to a per-character
   worker. */

/* The eligibility window the party sweeps use, the same one W8Party::randomSlot
   and AnyPartyMemberCanUseItem apply: highest_condition below death. */

/* A negative amount means "as much as they could possibly hold", which the
   restore computes by summing the whole spell-point ceiling. */
enum { W8_RESTORE_EVERYTHING = -1 };

/* Roll the dice once per eligible party member and apply the result to each of
   them. The roll is separate per character rather than shared. */
void ApplyRolledHealthChangeToParty(const W8Dice* dice, W8SpellEffectResult* result, int announce)
{
    int party_slot;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value() &&
            CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_DEAD) {
            CurrentGame().damageCharacter(
                party_slot, RollDice(dice),
                {.notice = announce ? DamageNotice::Standard : DamageNotice::None}, result);
        }
    }
}

/* Heal every occupied slot by its own roll, announced. The dice are assembled
   from the caller's own arguments rather than passed as a record, which is why
   the two count bytes arrive separately from the base. */
void HealPartyByDice(unsigned char count, unsigned char sides, short base)
{
    W8Dice dice;
    unsigned int party_slot;

    dice.base = base;
    dice.count = count;
    dice.sides = sides;
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value()) {
            HealCharacter(party_slot, RollDice(&dice), true);
        }
    }
}

/* The stamina form, which is the same sweep unannounced. */
void RestorePartyStaminaByDice(unsigned char count, unsigned char sides, short base)
{
    W8Dice dice;
    unsigned int party_slot;

    dice.base = base;
    dice.count = count;
    dice.sides = sides;
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value()) {
            RestoreCharacterStamina(party_slot, RollDice(&dice), false);
        }
    }
}

/* Spend spell points from one realm. Spending more than is left is a caller
   error rather than something to clamp. */
void SpendCharacterSpellPoints(int party_slot, W8SpellRealm realm, int amount)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    if (amount != 0) {
        assert(character->iSPLeft[realm] >= amount);
        character->iSPLeft[realm] -= amount;
        CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
    }
}

/* Give spell points back to one realm, never past its ceiling. */
void RestoreCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, int amount)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    character->iSPLeft[realm] += amount;
    if (character->sp_max[realm] < character->iSPLeft[realm]) {
        character->iSPLeft[realm] = character->sp_max[realm];
    }
    CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
}

/* Drain spell points across the party. Unlike its neighbours this does not
   filter on the eligibility window - an unconscious character still loses
   points. */
void DrainPartySpellPoints(int amount, bool announce)
{
    unsigned int party_slot;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value()) {
            DrainCharacterSpellPoints(party_slot, amount, announce);
        }
    }
}

/* Restore spell points across the party. A negative amount means as much as
   the character could possibly hold, which is the sum of all six realm
   ceilings. */
void RestorePartySpellPoints(int amount)
{
    int party_slot;
    int granted;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value() &&
            CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_DEAD &&
            CurrentGame().party.characters[party_slot].hp_current != 0) {
            granted = amount;
            if (amount < 0) {
                granted = SumCharacterSpellPoints(&CurrentGame().party.characters[party_slot]);
            }
            RestoreCharacterSpellPointsEvenly(party_slot, granted);
        }
    }
}

/* Suffix the damage notice carries when a poison tick is what hurt the
   monster. */
char g_poison_suffix[] = "POISON ";

/* Roll the dice once for every live monster inside the radius of a point and
   apply each roll as damage through the monster-side effect pass. */
void DamageMonstersInRadius(const glm::vec3& center, float radius, const W8Dice* dice,
                            W8TargetSource* source, W8SpellEffectResult* result)
{
    unsigned int index;
    int damage;
    W8MonsterInfo* monster_info;
    glm::vec3 location;
    glm::vec3 offset;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->fActive) {
            location = monster_info->entity->GetRepresentationLocation();
            offset = glm::vec3(center.x - location.x, center.y - location.y, center.z - location.z);
            if (glm::length(offset) <= radius) {
                damage = RollDice(dice);
                ApplyDamageToMonster(monster_info, damage, source, false, 1, 0, result, false);
            }
        }
    }
}

/* Apply rolled damage to a monster: the slot-2 enchantment absorbs first,
   the result block collects the hit, combat and threat state decide whether
   the blow is announced and remembered, two thirds of it fatigue the monster,
   and the remainder comes off hit points - a lethal hit logs a kill record
   and starts the death sequence. Returns the amount actually applied. */
unsigned int ApplyDamageToMonster(W8MonsterInfo* monster_info, unsigned int amount,
                                  W8TargetSource* source, bool quiet, unsigned char in_combat,
                                  signed char a, W8SpellEffectResult* result_stats, bool c)
{
    unsigned int absorbed;
    unsigned int applied;
    unsigned int category;

    if (monster_info->hp_current == 0) {
        return 0;
    }
    if (monster_info->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].turns != 0) {
        absorbed = monster_info->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].magnitude;
        if (amount <= absorbed) {
            PostMonsterNotice(monster_info, fmt::sprintf(Data().strings[0x193 - (quiet)], amount));
            monster_info->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].magnitude =
                static_cast<unsigned short>(absorbed - amount);
            if (result_stats != 0) {
                ++result_stats->count;
            }
            return 0;
        }
        PostMonsterNotice(monster_info, fmt::sprintf(Data().strings[0x193 - (quiet)], absorbed));
        amount -= absorbed;
        ClearMonsterEnchantmentSlot(monster_info->location_id, W8_ENCHANTMENT_GUARDIAN_ANGEL);
    }
    if (result_stats != 0) {
        result_stats->amount += amount;
        ++result_stats->count;
    }
    if (amount != 0) {
        if (CurrentGame().combat.has_value() || monster_info->party_threat.visible_to_player) {
            CurrentGame().pointCameraAtMonster(*monster_info, false, true);
            category = 9;
            if (TargetSourceIsCharacter(source, 0) && source->iChar != -1) {
                category = 8;
            }
            if (in_combat != 0) {
                if (c) {
                    ShowNotice(category, fmt::format("{} {}", GetMonsterName(monster_info, 0, 0),
                                                     Data().strings[0x268]));
                } else if (a != 0) {
                    ShowNotice(category, fmt::sprintf(Data().strings[0x254], amount));
                } else {
                    ShowNotice(category, fmt::sprintf(Data().strings[0x256],
                                                      GetMonsterName(monster_info, 0, 0), amount,
                                                      quiet ? g_poison_suffix : ""));
                }
            }
        }
        if (!source->fBackfire && !source->fReflection && !source->target_diverted && !quiet) {
            monster_info->condition_target = *source;
            if (monster_info->pCombat && TargetSourceIsCharacter(source, 0) &&
                source->iChar != -1) {
                monster_info->pCombat->character_hate[source->iChar] += amount;
            }
        }
        applied = monster_info->hp_current;
        if (amount < applied) {
            monster_info->hp_current = applied - amount;
            FatigueMonster(monster_info, (amount * 2) / 3, result_stats);
            MonsterReactsToBeingStruck(monster_info, source, quiet);
        } else {
            monster_info->hp_current = 0;
            if (result_stats != 0) {
                auto death_report = std::make_unique<W8SpellDamageReport>();
                death_report->kind = W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH;
                death_report->text = GetMonsterName(monster_info, 0, 0);
                result_stats->reports.push_back(std::move(death_report));
            }
            MonsterStartsDying(monster_info, result_stats == 0);
        }
        ClearHighlightIfItIs(&monster_info->location_id);
        UpdateMonsterDamageAppearance(monster_info);
        if (monster_info->entity) {
            monster_info->entity->SpawnDamageNumber(amount);
        }
    }
    return amount;
}

/* Heal one monster. A monster that is already dead or already whole is left
   alone; healing it to full says so differently from healing it partway. */
void HealMonster(W8MonsterInfo* monster_info, unsigned int amount, bool announce)
{
    if (monster_info->hp_current == 0 ||
        monster_info->hp_current == static_cast<unsigned int>(monster_info->uiHPMax) ||
        amount == 0) {
        return;
    }

    monster_info->hp_current += amount;
    if (monster_info->hp_current > static_cast<unsigned int>(monster_info->uiHPMax)) {
        monster_info->hp_current = monster_info->uiHPMax;
    }
    ClearHighlightIfItIs(&monster_info->location_id);
    UpdateMonsterDamageAppearance(monster_info);

    if (announce) {
        if (monster_info->hp_current == static_cast<unsigned int>(monster_info->uiHPMax)) {
            ShowNotice(W8_FONT_PALETTE_RUST,
                       fmt::sprintf(Data().strings[0x259], GetMonsterName(monster_info, 0, 0)));
        } else {
            ShowNotice(
                W8_FONT_PALETTE_RUST,
                fmt::sprintf(Data().strings[0x25b], GetMonsterName(monster_info, 0, 0), amount));
        }
    }
}

/* What one monster action costs in fatigue, before it is spent. Four of the
   ten actions are free; the plain attack costs markedly more when its detail
   is three. A monster that tires twice as fast pays double. */
int MonsterActionFatigueCost(const W8MonsterInfo* monster_info)
{
    int cost = 0;

    switch (monster_info->action_kind) {
    case W8_MONSTER_ACTION_ADVANCE:
    case W8_MONSTER_ACTION_BACK_OFF:
        cost = CurrentGame().rng.below(5) + 5;
        break;
    case W8_MONSTER_ACTION_APPROACH:
    case W8_MONSTER_ACTION_RETURN_TO_START:
        cost = CurrentGame().rng.below(3) + 3;
        break;
    case W8_MONSTER_ACTION_WAIT:
    case W8_MONSTER_ACTION_CONTROLLED_MOVE:
        cost = CurrentGame().rng.below(2) + 1;
        break;
    case W8_MONSTER_ACTION_ATTACK:
        if (monster_info->action_detail == 3) {
            cost = CurrentGame().rng.below(8) + 5;
        } else {
            cost = CurrentGame().rng.below(3) + 2;
        }
        break;
    case W8_MONSTER_ACTION_NONE:
    case W8_MONSTER_ACTION_SPELL:
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
    case W8_MONSTER_ACTION_PROTECT:
        break;
    }

    if (monster_info->uiCondition[W8_CONDITION_DISEASED] == 0) {
        return cost;
    }
    return cost * 2;
}

/* How much stamina shakes exhaustion off again. */
enum { W8_STAMINA_TO_SHAKE_OFF_EXHAUSTION = 9 };

int GetCharacterRealmSpellPoints(const W8Character* character, W8SpellRealm realm)
{
    int points = character->iSPLeft[realm];
    return points > 0 ? points : 0;
}

int SumCharacterSpellPointsLeft(const W8Character* character)
{
    int total = 0;
    for (int realm : character->iSPLeft) {
        total += realm > 0 ? realm : 0;
    }
    return total;
}

/* Heal one character, never past their maximum. Recovering enough of their
   hit points breaks the effects that only held while they were badly hurt -
   the deeper threshold breaks two more than the shallower one. */
void HealCharacter(int party_slot, int amount, bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int hp_max;
    unsigned int fraction;

    assert(CurrentGame().party.members[party_slot].has_value());

    if (character->hp_current == 0) {
        return;
    }
    hp_max = character->uiHPMax;
    if (character->hp_current == hp_max) {
        return;
    }

    character->hp_current += amount;
    if (character->hp_current > hp_max) {
        character->hp_current = hp_max;
    }
    if (announce) {
        if (character->hp_current == hp_max) {
            PostCharacterNotice(party_slot, Data().strings[0x258]);
        } else {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x25a], amount));
        }
    }

    fraction = (character->hp_current * 100) / static_cast<unsigned int>(character->uiHPMax);
    if (fraction >= 50u) {
        if (CurrentGame().speech.HasEventCharacter(3, party_slot)) {
            CurrentGame().speech.SetEventCharacterMask(3, party_slot, false);
        }
        if (fraction >= 70u) {
            if (CurrentGame().speech.HasEventCharacter(2, party_slot)) {
                CurrentGame().speech.SetEventCharacterMask(2, party_slot, false);
            }
            if (CurrentGame().speech.HasEventCharacter(28, party_slot)) {
                CurrentGame().speech.SetEventCharacterMask(28, party_slot, false);
            }
        }
    }
}

/* Give one character stamina back, never past their maximum. Any move re-bands
   their fatigue, and a change of band re-runs the armour class pass because
   fatigue feeds it. Enough stamina also shakes off exhaustion. */
void RestoreCharacterStamina(int party_slot, int amount, bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    int stamina_max;
    int previous_band;
    int band;

    if (character->highest_condition >= W8_CONDITION_DEAD || character->hp_current == 0) {
        return;
    }
    stamina_max = character->uiStaminaMax;
    if (character->stamina == stamina_max) {
        return;
    }

    character->stamina += amount;
    if (character->stamina > stamina_max) {
        character->stamina = stamina_max;
    }
    if (announce) {
        if (character->stamina == stamina_max) {
            PostCharacterNotice(party_slot, Data().strings[0x25c]);
        } else {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x25e], amount));
        }
    }

    previous_band = character->fatigue_band;
    band = FatigueBand(character->stamina, character->uiStaminaMax);
    character->fatigue_band = band;
    if (band != previous_band) {
        SettleActiveParty(CurrentGame());
    }
    if (character->uiCondition[W8_CONDITION_UNCONSCIOUS] == W8_CONDITION_INDEFINITE &&
        character->stamina > W8_STAMINA_TO_SHAKE_OFF_EXHAUSTION) {
        LiftCondition(CurrentGame(), party_slot, W8_CONDITION_UNCONSCIOUS, true);
    }
}

/* Drain spell points from one character, taking them from randomly chosen
   realms until the whole amount is gone or fifty attempts have been spent.
   Each realm only gives up what it has, and each withdrawal is announced with
   that realm's name. */
void DrainCharacterSpellPoints(int party_slot, unsigned int amount, bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int remaining = amount;
    unsigned int taken;
    int attempts;
    W8SpellRealm realm;

    if (character->hp_current == 0) {
        return;
    }
    if (CurrentGame().status.world_suspended) {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x260], amount));
        return;
    }

    for (attempts = 0x32; remaining != 0 && attempts != 0; --attempts) {
        realm = static_cast<W8SpellRealm>(CurrentGame().rng.below(W8_SPELL_REALM_COUNT));
        if (character->iSPLeft[realm] > 0) {
            taken = remaining;
            if (static_cast<unsigned int>(character->iSPLeft[realm]) <= remaining) {
                taken = character->iSPLeft[realm];
            }
            SpendCharacterSpellPoints(party_slot, realm, taken);
            if (announce) {
                ShowNotice(W8_FONT_PALETTE_WHITE,
                           fmt::sprintf(Data().strings[0x263], amount,
                                        Data().strings[g_realm_message_offsets[realm]]));
            }
            remaining = amount - taken;
            amount = remaining;
        }
    }
}

/* Spread spell points across a character's six realms, always topping up the
   realm that is furthest from full. The realms are sorted by how far short
   they are, then handed a point each in turn; ties are given a point together
   so the deficits stay level. */
struct W8SpellPointDeficit {
    unsigned int realm;
    unsigned int deficit;
};

void RestoreCharacterSpellPointsEvenly(int party_slot, int amount)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    W8SpellPointDeficit order[W8_SPELL_REALM_COUNT];
    unsigned int index;
    int granted = 0;
    bool tied;

    for (index = 0; index < W8_SPELL_REALM_COUNT; ++index) {
        order[index].realm = index;
        order[index].deficit = character->sp_max[index] - character->iSPLeft[index];
    }
    std::ranges::sort(order, std::greater{}, &W8SpellPointDeficit::deficit);

    for (;;) {
        if (amount == 0) {
            break;
        }
        for (index = 0; index < W8_SPELL_REALM_COUNT; ++index) {
            if (order[index].deficit == 0 || (index != W8_SPELL_REALM_COUNT - 1 &&
                                              order[index].deficit < order[index + 1].deficit)) {
                if (index == W8_SPELL_REALM_COUNT - 1) {
                    PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x1a3], granted));
                    return;
                }
                continue;
            }
            tied = index < W8_SPELL_REALM_COUNT && order[index].deficit == order[index + 1].deficit;
            ++character->iSPLeft[order[index].realm];
            --amount;
            ++granted;
            --order[index].deficit;
            if (!tied) {
                break;
            }
            if (amount == 0) {
                PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x1a3], granted));
                return;
            }
        }
    }
    PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x1a3], granted));
}

/* Wound one character. Two thirds of the damage also tires them, the damage
   itself is booked against the hit-point adjustment and the pools rebuilt from
   it, and a character with no protection against it is put under condition
   one. */
void DamageCharacter(int party_slot, unsigned int damage, bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    assert(CurrentGame().party.members[party_slot].has_value());

    if (character->uiHPMax != 0 && character->hp_current != 0) {
        CurrentGame().fatigueCharacter(party_slot, (damage * 2) / 3, false, 0);
        if (announce) {
            ShowNotice(W8_FONT_PALETTE_WHITE, fmt::sprintf(Data().strings[0x1c4], damage));
        }
        character->hp_adjustment -= damage;
        SettleActiveParty(CurrentGame());
        if (character->uiCondition[W8_CONDITION_DRAINED] == 0) {
            InflictCondition(CurrentGame(), party_slot,
                             {.condition = W8_CONDITION_DRAINED,
                              .duration = W8_CONDITION_INDEFINITE,
                              .announce = false});
        }
        if (character->hp_current != 0) {
            CurrentGame().speech.QueueDamageReactionEvents(character);
        }
    }
}

/* Tire one monster. Running its pool down to nothing puts it under the
   exhausted condition indefinitely, and tells whoever asked for the fatigue
   that it landed. */
void FatigueMonster(W8MonsterInfo* monster_info, unsigned int amount,
                    W8SpellEffectResult* report_to)
{
    W8TargetSource target_block;

    if (monster_info->hp_current == 0) {
        return;
    }
    if (amount != 0) {
        GetMonsterDataForInfo(monster_info);
        if (amount < static_cast<unsigned int>(monster_info->stamina)) {
            monster_info->stamina -= amount;
        } else {
            monster_info->stamina = 0;
        }
    }

    monster_info->fatigue_band = FatigueBand(monster_info->stamina, monster_info->stamina_max);

    if (monster_info->stamina == 0 &&
        monster_info->uiCondition[W8_CONDITION_UNCONSCIOUS] < W8_CONDITION_INDEFINITE) {
        ResetTargetSource(&target_block);
        SetMonsterCondition(monster_info->location_id, W8_CONDITION_UNCONSCIOUS,
                            W8_CONDITION_INDEFINITE, 0, &target_block, report_to == 0);
        if (report_to != 0) {
            ++report_to->condition_counts[W8_CONDITION_UNCONSCIOUS];
        }
    }
}

/* A completed spell costs its database level plus the cast result. Result
   eight is the one outcome that carries no stamina charge. MonsterCastsSpell
   deliberately returns this value to its caller. */
int SpellCastFatigueCost(int spell_id, int result)
{
    if (result == 8) {
        return 0;
    }
    return Data().spells[spell_id].spell_level + result;
}

/* Give one monster stamina back, the mirror of the character form down to the
   band ladder and the exhaustion it shakes off. */
void RestoreMonsterStamina(W8MonsterInfo* monster_info, int amount, bool announce)
{
    unsigned int stamina_max;

    if (monster_info->highest_condition >= W8_CONDITION_DEAD || monster_info->hp_current == 0) {
        return;
    }
    stamina_max = monster_info->stamina_max;
    if (static_cast<unsigned int>(monster_info->stamina) == stamina_max) {
        return;
    }

    monster_info->stamina += amount;
    if (static_cast<unsigned int>(monster_info->stamina) > stamina_max) {
        monster_info->stamina = stamina_max;
    }
    if (announce) {
        if (static_cast<unsigned int>(monster_info->stamina) == stamina_max) {
            ShowNotice(W8_FONT_PALETTE_RUST,
                       fmt::sprintf(Data().strings[0x25d], GetMonsterName(monster_info, 0, 0)));
        } else {
            ShowNotice(
                W8_FONT_PALETTE_RUST,
                fmt::sprintf(Data().strings[0x25f], GetMonsterName(monster_info, 0, 0), amount));
        }
    }

    monster_info->fatigue_band = FatigueBand(monster_info->stamina, monster_info->stamina_max);

    if (monster_info->uiCondition[W8_CONDITION_UNCONSCIOUS] == W8_CONDITION_INDEFINITE &&
        static_cast<unsigned int>(monster_info->stamina) > W8_STAMINA_TO_SHAKE_OFF_EXHAUSTION) {
        ClearMonsterCondition(monster_info->location_id, W8_CONDITION_UNCONSCIOUS);
    }
}

/* How a monster answers being struck. It plays the struck cycle; a monster
   with the right make-up may be knocked into another condition, its chance
   riding on half its fifth converted attribute; a monster the party had under
   control is handed back to itself; and if the attacker is not already an
   enemy, the disposition check decides whether being hit makes them one. */
void MonsterReactsToBeingStruck(W8MonsterInfo* monster_info, W8TargetSource* attacker, bool quiet)
{
    monster_info->startCycle(W8_MONSTER_CYCLE_GET_HIT, W8_ANIMATION_PLAY_ONCE);

    if (monster_info->uiCondition[W8_CONDITION_ASLEEP] != 0 && !quiet &&
        CurrentGame().rng.below(100) <
            static_cast<unsigned int>((monster_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES] >> 1) +
                                      0x32)) {
        ClearMonsterCondition(monster_info->location_id, W8_CONDITION_ASLEEP);
    }
    if (monster_info->control_state == W8_MONSTER_CONTROL_LURED) {
        SetMonsterControlState(monster_info, W8_MONSTER_CONTROL_NONE);
    }

    if (!TargetSourceIsCharacter(attacker, 0) && !TargetSourceIsMonster(attacker, 0)) {
        return;
    }
    if (!attacker->fBackfire && !attacker->fReflection && !attacker->target_diverted && !quiet &&
        monster_info->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
        if (TargetSourceIsCharacter(attacker, 0)) {
            if (CurrentGame().party.dispositionToward(
                    attacker->iChar, (monster_info)->ubDisposition) == W8_DISPOSITION_FRIENDLY) {
                TickMonsterCondition(monster_info->location_id, W8_CONDITION_TURNCOAT, 1);
            }
        } else if (MonsterHostility(FindMonster(attacker->iMonsterID), monster_info) ==
                   W8_DISPOSITION_FRIENDLY) {
            TickMonsterCondition(monster_info->location_id, W8_CONDITION_TURNCOAT, 1);
        }
    }
}

/* What one character action costs in fatigue before it is spent. An attack
   costs by the weight of what is swung - unarmed and the two special attack
   modes are flat rolls instead - a run costs a fifth of the character's whole
   stamina with a floor, and the rest are free. A character who tires twice as
   fast pays double. */
unsigned int CharacterActionFatigueCost(int party_slot, W8ActionKind action_kind)
{
    unsigned int cost = 0;
    W8AttackMode attack_mode;
    int item_id;
    int weight_bands;

    switch (action_kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK: {
        const auto& combat_row = CurrentGame().combat->characters[party_slot];
        attack_mode = static_cast<W8AttackMode>(
            CurrentGame().party.rows[party_slot].executing.attack_mode[combat_row.current_hand]);
        if (attack_mode == W8_ATTACK_MODE_PUNCH) {
            cost = CurrentGame().rng.below(3) + 2;
        } else if (attack_mode == W8_ATTACK_MODE_KICK) {
            cost = CurrentGame().rng.below(3) + 3;
        } else {
            item_id = CurrentGame()
                          .party.characters[party_slot]
                          .EquippedItem[combat_row.current_equip_slot]
                          .iItemNo;
            if (item_id == -1) {
                cost = CurrentGame().rng.below(4) + 3;
            } else {
                weight_bands = Data().items[item_id].weight / 0x28 + 1;
                cost = CurrentGame().rng.below(weight_bands) + 1 + weight_bands;
            }
        }
        if (action_kind == W8_ACTION_BERSERK) {
            cost *= 2;
        }
        break;
    }
    case W8_ACTION_BREATHE:
    case W8_ACTION_DEFEND:
    case W8_ACTION_PROTECT:
    case W8_ACTION_CAST_SPELL:
        break;
    case W8_ACTION_TURN_UNDEAD:
    case W8_ACTION_PRAY:
        cost =
            static_cast<unsigned int>(CurrentGame().party.characters[party_slot].uiStaminaMax) / 5;
        if (cost < 0x14) {
            cost = 0x14;
        }
        break;
    case W8_ACTION_USE_ITEM:
        cost = CurrentGame().rng.below(2) + 1;
        break;
    case W8_ACTION_EQUIP:
        cost = CurrentGame().rng.below(4) + 3;
        break;
    default:
        cost = 0;
    }

    if (CurrentGame().party.characters[party_slot].uiCondition[W8_CONDITION_DISEASED] != 0) {
        cost *= 2;
    }
    return cost;
}

/* Drain spell points from one named realm, taking no more than it holds.
   Announced with the realm's own name. */
void DrainCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, unsigned int amount,
                                    bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int available;

    if (character->hp_current == 0) {
        return;
    }
    available = character->iSPLeft[realm];
    if (available == 0) {
        return;
    }

    if (CurrentGame().status.world_suspended) {
        PostCharacterNotice(party_slot,
                            fmt::sprintf(Data().strings[0x262], amount,
                                         Data().strings[g_realm_message_offsets[realm]]));
        return;
    }

    if (static_cast<int>(available) < 0) {
        return;
    }
    if (amount >= available) {
        amount = available;
    }
    if (amount == 0) {
        return;
    }
    SpendCharacterSpellPoints(party_slot, realm, amount);
    if (announce) {
        ShowNotice(W8_FONT_PALETTE_WHITE,
                   fmt::sprintf(Data().strings[0x263], amount,
                                Data().strings[g_realm_message_offsets[realm]]));
    }
}

int SumCharacterSpellPoints(const W8Character* character)
{
    int total = 0;
    for (int realm : character->sp_max) {
        total += realm;
    }
    return total;
}

/* Occupied slots below the dead condition compete on hit-point percent; the
   weakest one wins. */
unsigned int FindPartySlotWithLowestHitPoints(void)
{
    unsigned int best_slot = 0;
    unsigned int best_percent = 100;

    for (unsigned int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() &&
            character->highest_condition < W8_CONDITION_DEAD) {
            unsigned int percent =
                (character->hp_current * 100) / static_cast<unsigned int>(character->uiHPMax);
            if (percent < best_percent) {
                best_slot = slot;
                best_percent = percent;
            }
        }
    }
    return best_slot;
}

/* Same sweep over the spell-point pools: the numerator counts only realms
   with points still remaining, so the member with the lowest remaining
   percentage wins. */
unsigned int FindPartySlotWithLowestSpellPoints(void)
{
    unsigned int best_slot = 0;
    unsigned int best_percent = 100;

    for (unsigned int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() &&
            character->highest_condition < W8_CONDITION_DEAD) {
            unsigned int pool_max = SumCharacterSpellPoints(character);
            if (pool_max > 0) {
                int pool_left = SumCharacterSpellPointsLeft(character);
                unsigned int percent = static_cast<unsigned int>(pool_left * 100) / pool_max;
                if (percent < best_percent) {
                    best_percent = percent;
                    best_slot = slot;
                }
            }
        }
    }
    return best_slot;
}

/* Applies one queued fatigue op: a character target fatigues that party
   slot, a monster target fatigues the monster spawned from its location
   id. */
void ApplyQueuedFatigue(W8CombatSlot* op, unsigned int amount, int)
{
    if (op->iType == W8_TARGET_KIND_CHARACTER) {
        CurrentGame().fatigueCharacter(op->iChar, amount, false, 0);
        return;
    }
    if (op->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = FindMonster(op->iMonsterID);
        FatigueMonster(monster_info, amount, 0);
    }
}

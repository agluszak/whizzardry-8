#include <algorithm>
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
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatHostility.h"
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
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"

/* Local Code\Health Stamina Mana.cpp, named by the assertions these bodies
   embed. The party sweeps in here all share one shape: walk the eight party
   slots, skip the empty ones, and hand each occupied slot to a per-character
   worker. */

unsigned int ApplyDamageToCharacter(int party_slot, unsigned int amount, bool quiet, bool announce,
                                    bool short_notice, W8SpellEffectResult* result_stats,
                                    bool detailed)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int absorbed;
    unsigned int applied;

    if (!CurrentGame().party.rows[party_slot].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }
    if (character->hp_current == 0) {
        return 0;
    }
    if (CurrentGame().status.world_suspended) {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x253], amount));
        return 0;
    }

    if (character->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].turns != 0) {
        absorbed = character->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].magnitude;
        if (amount <= absorbed) {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x193 - (quiet)], amount));
            character->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].magnitude =
                static_cast<unsigned short>(absorbed - amount);
            if (result_stats != 0) {
                ++result_stats->count;
            }
            return 0;
        }

        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x193 - (quiet)], absorbed));
        amount -= absorbed;
        ClearCharacterEnchantmentSlot(party_slot, W8_ENCHANTMENT_GUARDIAN_ANGEL);
        PostCharacterNotice(party_slot, Data().strings[0x194]);
    }

    FatigueCharacter(party_slot, (amount * 2) / 3, false, result_stats);
    if (result_stats != 0) {
        result_stats->amount += amount;
        ++result_stats->count;
    }

    if (announce) {
        if (detailed) {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x268], amount));
        } else if (short_notice) {
            ShowNotice(W8_FONT_PALETTE_RUST, fmt::sprintf(Data().strings[0x254], amount));
        } else {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x255], amount,
                                                         quiet ? Data().strings[0x257] : ""));
        }
    }

    applied = character->hp_current;
    if (applied <= amount) {
        if (CharacterHasTrait(character, W8_TRAIT_CHEAT_DEATH) &&
            character->uiCondition[W8_CONDITION_UNCONSCIOUS] < 7) {
            CheatDeathRevive(CurrentGame(), party_slot);
            RecordCharacterDamage(party_slot, amount);
            return applied;
        }
        if (applied < amount) {
            amount = applied;
        }
    }

    character->hp_current = applied - amount;
    RecordCharacterDamage(party_slot, amount);
    if (character->hp_current != 0) {
        if (!CurrentGame().surprise_possible) {
            QueueDamageReactionEvents(character);
        }
    } else {
        if (result_stats != 0) {
            auto report = std::make_unique<W8SpellDamageReport>();
            report->kind = W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH;
            report->value = party_slot;
            result_stats->reports.push_back(std::move(report));
        }
        SetCharacterCondition(party_slot, W8_CONDITION_DEAD, W8_CONDITION_INDEFINITE, 0, 0,
                              result_stats == 0);
    }

    if (character->uiCondition[W8_CONDITION_ASLEEP] != 0 && !quiet &&
        CurrentGame().rng.below(100) <
            (character->attributes[W8_ATTRIBUTE_SENSES].effective >> 1) + 0x32) {
        RemoveCharacterCondition(party_slot, W8_CONDITION_ASLEEP, true);
    }
    return amount;
}

/* The eligibility window the party sweeps use, the same one GetRandomCharacter
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
        if (CurrentGame().party.rows[party_slot].fOccupied &&
            CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_DEAD) {
            ApplyDamageToCharacter(party_slot, RollDice(dice), false, announce, false, result,
                                   false);
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
        if (CurrentGame().party.rows[party_slot].fOccupied) {
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
        if (CurrentGame().party.rows[party_slot].fOccupied) {
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
        if (character->iSPLeft[realm] < amount) {
            Fail("pPC->iSPLeft[uiRealm] >= (INT32) uiSPs");
        }
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
        if (CurrentGame().party.rows[party_slot].fOccupied) {
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
        if (CurrentGame().party.rows[party_slot].fOccupied &&
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
            location = monster_info->p3D->GetRepresentationLocation();
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
                                  char a, W8SpellEffectResult* result_stats, bool c)
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
            PointCameraAtMonster(monster_info, false, true);
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
        if (monster_info->p3D != 0) {
            monster_info->p3D->SpawnDamageNumber(amount);
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
    default:
        Fail("FALSE", fmt::format("MonsterActionFatigueCost: ERROR - Invalid action {:d}",
                                  fmt::underlying(monster_info->action_kind)));
        break;
    }

    if (monster_info->uiCondition[W8_CONDITION_DISEASED] == 0) {
        return cost;
    }
    return cost * 2;
}

/* The two fatigue-band thresholds and the four bands they cut the stamina
   fraction into. Both characters and monsters use the same ladder. */
enum {
    W8_FATIGUE_BAND_1 = 0x32,
    W8_FATIGUE_BAND_2 = 0x46,
    W8_FATIGUE_BAND_3 = 0x55,
    W8_FATIGUE_BAND_4 = 0x5e
};

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

/* One notice index per spell realm, giving the realm's name. */
/* Turn a pool fraction into a band. The same ladder decides a character's
   fatigue band and a monster's, from the percentage of the pool that is
   missing rather than the part that is left. */
static int FatigueBandFromMissing(int missing_percent)
{
    if (missing_percent < W8_FATIGUE_BAND_1) {
        return 0;
    }
    if (missing_percent < W8_FATIGUE_BAND_2) {
        return 1;
    }
    if (missing_percent < W8_FATIGUE_BAND_3) {
        return 2;
    }
    return (missing_percent > W8_FATIGUE_BAND_4) + 3;
}

/* Heal one character, never past their maximum. Recovering enough of their
   hit points breaks the effects that only held while they were badly hurt -
   the deeper threshold breaks two more than the shallower one. */
void HealCharacter(int party_slot, int amount, bool announce)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int hp_max;
    unsigned int fraction;

    if (!CurrentGame().party.rows[party_slot].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }

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
    band = FatigueBandFromMissing(
        100 - static_cast<int>((character->stamina * 100) /
                               static_cast<unsigned int>(character->uiStaminaMax)));
    character->fatigue_band = band;
    if (band != previous_band) {
        SettleActiveParty(CurrentGame());
    }
    if (character->uiCondition[W8_CONDITION_UNCONSCIOUS] == W8_CONDITION_INDEFINITE &&
        character->stamina > W8_STAMINA_TO_SHAKE_OFF_EXHAUSTION) {
        RemoveCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, true);
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

    if (!CurrentGame().party.rows[party_slot].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }

    if (character->uiHPMax != 0 && character->hp_current != 0) {
        FatigueCharacter(party_slot, (damage * 2) / 3, false, 0);
        if (announce) {
            ShowNotice(W8_FONT_PALETTE_WHITE, fmt::sprintf(Data().strings[0x1c4], damage));
        }
        character->hp_adjustment -= damage;
        SettleActiveParty(CurrentGame());
        if (character->uiCondition[W8_CONDITION_DRAINED] == 0) {
            SetCharacterCondition(party_slot, W8_CONDITION_DRAINED, W8_CONDITION_INDEFINITE, 0, 0,
                                  0);
        }
        if (character->hp_current != 0) {
            QueueDamageReactionEvents(character);
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

    monster_info->fatigue_band = FatigueBandFromMissing(
        100 - static_cast<int>((monster_info->stamina * 100) /
                               static_cast<unsigned int>(monster_info->stamina_max)));

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

    monster_info->fatigue_band = FatigueBandFromMissing(
        100 - static_cast<int>((monster_info->stamina * 100) /
                               static_cast<unsigned int>(monster_info->stamina_max)));

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
    StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_GET_HIT, W8_ANIMATION_PLAY_ONCE);

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
            if (MonsterVsCharDisposition(attacker->iChar, monster_info) ==
                W8_DISPOSITION_FRIENDLY) {
                TickMonsterCondition(monster_info->location_id, W8_CONDITION_TURNCOAT, 1);
            }
        } else if (MonsterHostility(FindMonster(attacker->iMonsterID), monster_info) ==
                   W8_DISPOSITION_FRIENDLY) {
            TickMonsterCondition(monster_info->location_id, W8_CONDITION_TURNCOAT, 1);
        }
    }
}

/* The band past which deep fatigue takes hold, and the band it lets go at. */
enum { W8_FATIGUE_BAND_DEEP = 2, W8_FATIGUE_BAND_RECOVERED = 2 };

/* Tire one character. The load they are carrying scales the cost - eased or
   worsened by the two load modifiers - and the result is taken out of their
   stamina without going below nothing. Running out puts them under the
   exhausted condition; merely dropping into the deep band applies the
   deep-fatigue effect once. */
void FatigueCharacter(int party_slot, int amount, bool scale_by_load,
                      W8SpellEffectResult* report_to)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    int previous_band;
    int band;
    int load_percent;

    if (character->stamina <= 0 || character->hp_current == 0 || amount == 0) {
        return;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored                                                                   \
    "-Wsometimes-uninitialized" // uninit-ok: retail's invalid-load assertion continues without
                                // assigning a percentage; that unresolved value still affects
                                // stamina
    if (scale_by_load) {
        switch (character->load_category) {
        case 0:
            load_percent = 0;
            break;
        case 1:
            load_percent = 0x19;
            break;
        case 2:
            load_percent = 0x32;
            break;
        case 3:
            load_percent = 100;
            break;
        case 4:
            load_percent = 200;
            break;
        default:
            Fail("FALSE", "FatigueCharacter: ERROR - Invalid load category");
            break;
        }
        if (character->uiCondition[W8_CONDITION_SLOWED] == 0) {
            if (character->enchantments[W8_ENCHANTMENT_HASTE].turns != 0) {
                load_percent += 0x19;
            }
        } else {
            load_percent -= 0x19;
        }
        amount += (load_percent * amount) / 100;
    }
#pragma clang diagnostic pop

    if (amount < 0) {
        amount = 0;
    } else if (amount > character->stamina) {
        amount = character->stamina;
    }
    character->stamina -= amount;

    previous_band = character->fatigue_band;
    band = FatigueBandFromMissing(
        100 - static_cast<int>((character->stamina * 100) /
                               static_cast<unsigned int>(character->uiStaminaMax)));
    character->fatigue_band = band;
    if (band != previous_band) {
        SettleActiveParty(CurrentGame());
    }

    if (character->stamina < 1) {
        if (character->uiCondition[W8_CONDITION_UNCONSCIOUS] < W8_CONDITION_INDEFINITE) {
            SetCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, W8_CONDITION_INDEFINITE, 0,
                                  0, report_to == 0);
            if (report_to != 0) {
                ++report_to->condition_counts[W8_CONDITION_UNCONSCIOUS];
            }
        }
    } else if (band != previous_band && band > W8_FATIGUE_BAND_DEEP) {
        if (!character->deep_fatigue_applied) {
            QueueCharacterEvent(character, 4, 0, kCharacterEventFullVolume);
            character->deep_fatigue_applied = true;
        }
        if (static_cast<unsigned int>(character->fatigue_band) < W8_FATIGUE_BAND_RECOVERED) {
            character->deep_fatigue_applied = false;
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

/* One character dies. Every condition but the tenth is lifted, both pools are
   emptied, the two targeting blocks on their slot row are cleared, combat
   forgets them as a participant, and whatever they were animating is told to
   stop. */
void CharacterDies(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    unsigned int condition;
    int animation;

    if (!row->fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }

    ++character->death_count;
    for (condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition) {
        if (condition != W8_CONDITION_INFATUATED && character->uiCondition[condition] != 0) {
            RemoveCharacterCondition(party_slot, static_cast<W8Condition>(condition), false);
        }
    }
    character->hp_current = 0;
    character->stamina = 0;

    ResetCombatSlot(&row->executing.target);
    ResetCombatSlot(&row->committed.target);
    if (CurrentGame().combat.has_value()) {
        RecordCharacterDeath(party_slot);
    }
    QueuePartyDeathReaction(party_slot);
    wiz8::sound::play("Data\\Sound\\Misc\\CharacterDead.wav");

    if (CurrentGame().combat.has_value()) {
        CurrentGame().combat->endTurnOf(PartyActor{party_slot});
        row->executing.kind = W8_ACTION_NONE;
        CurrentGame().combat->characters[party_slot].phase = 0;
        CurrentGame().combat->characters[party_slot].dead = true;
        DropCharacterFromRound(party_slot);
    }

    animation = row->npc_index;
    if (animation != -1) {
        W8NpcState* npc = GetNpcState(animation);
        if (npc != 0) {
            npc->spawned = 1;
        }
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
        if (CurrentGame().party.rows[slot].fOccupied &&
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
        if (CurrentGame().party.rows[slot].fOccupied &&
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
        FatigueCharacter(op->iChar, amount, false, 0);
        return;
    }
    if (op->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = FindMonster(op->iMonsterID);
        FatigueMonster(monster_info, amount, 0);
    }
}

W8Character* FindPartyMemberWithLowestResistance4(void)
{
    unsigned int lowest = 999;
    int selected = 0;
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (CurrentGame().party.rows[party_slot].fOccupied &&
            character->highest_condition < W8_CONDITION_DEAD &&
            character->resistances[4].total < lowest) {
            selected = party_slot;
            lowest = character->resistances[4].total;
        }
    }
    if (lowest == 999) return 0;
    return &CurrentGame().party.characters[selected];
}

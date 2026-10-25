#include "wiz8/combat/group_attacks.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "sight_state.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/records.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/utility.h"

/* The per-special-attack condition each attack slot tries to inflict, indexing the same
   rows as kConditionNotices. */
static W8Condition g_special_attack_condition_table[32][2] = {
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_BLIND, W8_CONDITION_NONE},
    {W8_CONDITION_BLIND, W8_CONDITION_NONE},       {W8_CONDITION_NONE, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_AFRAID, W8_CONDITION_INSANE},
    {W8_CONDITION_AFRAID, W8_CONDITION_TURNCOAT},  {W8_CONDITION_AFRAID, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_NONE, W8_CONDITION_NONE},
    {W8_CONDITION_INSANE, W8_CONDITION_NONE},      {W8_CONDITION_INSANE, W8_CONDITION_TURNCOAT},
    {W8_CONDITION_INSANE, W8_CONDITION_NONE},      {W8_CONDITION_IRRITATED, W8_CONDITION_NONE},
    {W8_CONDITION_IRRITATED, W8_CONDITION_NONE},   {W8_CONDITION_NAUSEATED, W8_CONDITION_NONE},
    {W8_CONDITION_NAUSEATED, W8_CONDITION_NONE},   {W8_CONDITION_PARALYZED, W8_CONDITION_NONE},
    {W8_CONDITION_PARALYZED, W8_CONDITION_NONE},   {W8_CONDITION_POISONED, W8_CONDITION_IRRITATED},
    {W8_CONDITION_POISONED, W8_CONDITION_NONE},    {W8_CONDITION_ASLEEP, W8_CONDITION_NONE},
    {W8_CONDITION_SLOWED, W8_CONDITION_PARALYZED}, {W8_CONDITION_SLOWED, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_NONE, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_NONE, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_NONE, W8_CONDITION_NONE},
    {W8_CONDITION_NONE, W8_CONDITION_NONE},        {W8_CONDITION_NONE, W8_CONDITION_NONE},
};

/* The per-special-attack realm/effect id handed to ApplyEffectAndAnnounce and the
   realm drain. */
static W8SpellRealm g_special_attack_realm_table[32] = {
    W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_WATER,  W8_SPELL_REALM_WATER,  W8_SPELL_REALM_DIVINE,
    W8_SPELL_REALM_WATER,  W8_SPELL_REALM_MENTAL, W8_SPELL_REALM_DIVINE, W8_SPELL_REALM_MENTAL,
    W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_MENTAL, W8_SPELL_REALM_DIVINE,
    W8_SPELL_REALM_MENTAL, W8_SPELL_REALM_EARTH,  W8_SPELL_REALM_AIR,    W8_SPELL_REALM_EARTH,
    W8_SPELL_REALM_AIR,    W8_SPELL_REALM_MENTAL, W8_SPELL_REALM_EARTH,  W8_SPELL_REALM_WATER,
    W8_SPELL_REALM_WATER,  W8_SPELL_REALM_AIR,    W8_SPELL_REALM_WATER,  W8_SPELL_REALM_EARTH,
    W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_FIRE,
    W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_FIRE,   W8_SPELL_REALM_WATER,  W8_SPELL_REALM_FIRE,
};

/* Whether a special attack kind still fires while the attacker's spellcasting
   is blocked: kinds 5-7 and 0x19-0x1d minus 0x1b ignore the block. */
bool MonsterSpecialAttackHonorsCastingBlock(int special_attack_kind)
{
    switch (special_attack_kind) {
    case 5:
    case 6:
    case 7:
    case 0x19:
    case 0x1a:
    case 0x1c:
    case 0x1d:
        return true;
    }
    return false;
}

static unsigned int RollSpecialAttackSum(unsigned int bound)
{
    unsigned int first = CurrentGame().rng.below(bound);
    unsigned int second = CurrentGame().rng.below(bound);
    unsigned int third = CurrentGame().rng.below(bound);
    return first + second + third;
}

/* Resolve one kind-table entry against every character and monster target in
   the group's attack. The per-slot bound comes from the attacker's current
   stamina fraction - a character source scales off level and stamina, a
   monster source off its missile value and current runtime stat - and a kind
   five table doubles it. Kind six delegates to the summon. */
void ResolveMonsterGroupAttack(int special_attack_kind, W8TargetSource* pSource,
                               W8CombatSlot* pAttackerSlot, std::vector<int> char_targets,
                               std::vector<int> monster_targets)
{
    bool announce;
    int iTarget;
    int iChar;
    int iMonsterID;
    int i;
    int drain;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;
    W8CombatSlot target;
    unsigned int uiHits[2];
    unsigned int uiTotals[2];
    unsigned int uiBounds[2];
    unsigned int uiMinRoll;
    unsigned int uiBound;
    unsigned int uiDamage;
    int extra;
    bool resolved;
    std::string text;

    announce = static_cast<signed char>(g_settings.verbose_combat_messages);
    if (pSource->iType == W8_TARGET_SOURCE_CHARACTER) {
        unsigned int level;
        unsigned int bound;
        unsigned int stamina;
        unsigned int stamina_max;

        iChar = pSource->iChar;
        level = CurrentGame().party.characters[iChar].uiExpLevel;
        bound = 0xf;
        if (level < 0x10) {
            bound = level;
        }
        stamina = CurrentGame().party.characters[iChar].stamina;
        stamina_max = CurrentGame().party.characters[iChar].uiStaminaMax;
        uiMinRoll = (stamina * (level + bound)) / stamina_max;
        if (uiMinRoll == 0) {
            uiMinRoll = 1;
        }
        uiBounds[1] = (stamina * (level + bound + 5)) / stamina_max;
        if (uiBounds[1] == 0) {
            uiBounds[1] = 1;
        }
        uiBounds[0] = uiBounds[1];
    } else if (pSource->iType == W8_TARGET_SOURCE_MONSTER) {
        unsigned int value;

        monster_info = FindMonster(pSource->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        if (record->effective_level < 0x10) {
            value = record->effective_level;
        } else {
            value = 0xf;
        }
        uiMinRoll =
            (static_cast<unsigned int>(monster_info->stamina) * (record->effective_level + value)) /
            static_cast<unsigned int>(monster_info->stamina_max);
        if (uiMinRoll == 0) {
            uiMinRoll = 1;
        }
        uiBounds[0] = (static_cast<unsigned int>(monster_info->stamina) *
                       (record->effective_level + value + 5)) /
                      static_cast<unsigned int>(monster_info->stamina_max);
        if (uiBounds[0] == 0) {
            uiBounds[0] = 1;
        }
        uiBounds[1] = uiBounds[0];
        if (g_special_attack_table[0][0] == 5) {
            uiBounds[0] *= 2;
        } else if (g_special_attack_table[1][0] == 5) {
            uiBounds[1] = uiBounds[0] * 2;
        }
    } else {
        throw std::logic_error(
            "ResolveMonsterGroupAttack: source is neither character nor monster");
    }

    if (g_special_attack_table[special_attack_kind][0] == 6) {
        SpawnSummonedMonsterGroup(special_attack_kind, pSource, pAttackerSlot);
    } else {
        uiHits[0] = 0;
        uiTotals[0] = 0;
        uiHits[1] = 0;
        uiTotals[1] = 0;
        for (iTarget = 0; iTarget < static_cast<int>(char_targets.size()); ++iTarget) {
            iChar = char_targets[iTarget];
            assert(iChar != -1);
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_CHARACTER;
            target.iChar = iChar;
            for (i = 0; i < 2; ++i) {
                if (i == 0) {
                    if (pSource->iType == W8_TARGET_SOURCE_MONSTER &&
                        CurrentGame().rng.below(100) <= 9) {
                        continue;
                    }
                } else if (CurrentGame().rng.below(100) <= 0x31) {
                    continue;
                }
                switch (g_special_attack_table[special_attack_kind][i]) {
                case 1:
                    uiBound = uiBounds[i];
                    uiDamage = CurrentGame().rng.below(uiBound);
                    if (g_special_attack_condition_table[special_attack_kind][i] == 7) {
                        extra = CurrentGame().rng.below(uiBound) + 1;
                    } else {
                        extra = 0;
                    }
                    resolved = ResolveAttackOnTarget(
                        pSource, &target, g_special_attack_condition_table[special_attack_kind][i],
                        g_special_attack_realm_table[special_attack_kind], uiMinRoll, extra,
                        uiDamage, announce, announce, 0);
                    if (!resolved && !announce) {
                        ++uiHits[i];
                    }
                    break;
                case 2:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 3 + 1;
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        DamageCharacter(iChar, uiDamage, true);
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                case 3:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 3 + 1;
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        for (drain = 0; drain < 6; ++drain) {
                            DrainCharacterRealmSpellPoints(
                                iChar, g_special_attack_realm_table[special_attack_kind], uiDamage,
                                true);
                        }
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                case 4:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) + 1;
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        CurrentGame().fatigueCharacter(iChar, uiDamage, false, 0);
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                case 5:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 3 + 1;
                    if (special_attack_kind != 10) {
                        ApplyEffectAndAnnounce(&uiDamage, &target,
                                               g_special_attack_realm_table[special_attack_kind],
                                               uiMinRoll);
                    }
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        CurrentGame().damageCharacter(
                            iChar, uiDamage,
                            {.notice = announce ? DamageNotice::Standard : DamageNotice::None});
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                }
            }
        }
        for (iTarget = 0; iTarget < static_cast<int>(monster_targets.size()); ++iTarget) {
            iMonsterID = monster_targets[iTarget];
            assert(iMonsterID != -1);
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_MONSTER;
            target.iMonsterID = iMonsterID;
            monster_info = FindMonster(iMonsterID);
            for (i = 0; i < 2; ++i) {
                if (i == 0) {
                    if (CurrentGame().rng.below(100) <= 9) {
                        continue;
                    }
                } else if (CurrentGame().rng.below(100) <= 0x31) {
                    continue;
                }
                switch (g_special_attack_table[special_attack_kind][i]) {
                case 1:
                    uiBound = 1;
                    if (uiBounds[i] / 3 != 0) {
                        uiBound = uiBounds[i] / 3;
                    }
                    if (g_special_attack_condition_table[special_attack_kind][i] == 7) {
                        extra = CurrentGame().rng.below(2) + 1;
                    } else {
                        extra = 0;
                    }
                    resolved = ResolveAttackOnTarget(
                        pSource, &target, g_special_attack_condition_table[special_attack_kind][i],
                        g_special_attack_realm_table[special_attack_kind], uiMinRoll, extra,
                        uiBound, announce, announce, 0);
                    if (!resolved && !announce) {
                        ++uiHits[i];
                    }
                    break;
                case 2:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 3 + 1;
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        CurrentGame().damageMonster(*monster_info, uiDamage, *pSource, {});
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                case 3:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 6 + 1;
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage != 0) {
                        monster_info->spell_points = monster_info->spell_points - (uiDamage >> 1);
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    } else {
                        AnnounceEffectResisted(&target);
                    }
                    break;
                case 4:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound);
                    ApplyEffectAndAnnounce(&uiDamage, &target,
                                           g_special_attack_realm_table[special_attack_kind],
                                           uiMinRoll);
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        CurrentGame().fatigueMonster(monster_info, uiDamage, 0);
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                case 5:
                    uiBound = uiBounds[i];
                    uiDamage = RollSpecialAttackSum(uiBound) / 3 + 1;
                    if (special_attack_kind != 10) {
                        ApplyEffectAndAnnounce(&uiDamage, &target,
                                               g_special_attack_realm_table[special_attack_kind],
                                               uiMinRoll);
                    }
                    if (uiDamage == 0) {
                        AnnounceEffectResisted(&target);
                    } else {
                        CurrentGame().damageMonster(
                            *monster_info, uiDamage, *pSource,
                            {.notice = announce ? DamageNotice::Standard : DamageNotice::None});
                        if (!announce) {
                            uiTotals[i] += uiDamage;
                            ++uiHits[i];
                        }
                    }
                    break;
                }
            }
        }
        if (!announce) {
            if (uiHits[0] == 0) {
                CurrentGame().events.push(TextFragment{.text = Data().strings[0x1a5]});
            } else {
                for (i = 0; i < 2; ++i) {
                    if (uiHits[i] == 0) {
                        continue;
                    }
                    if (i == 1 && uiHits[0] != 0) {
                        CurrentGame().events.push(TextFragment{.text = ","});
                    }
                    switch (g_special_attack_table[special_attack_kind][i]) {
                    case 1:
                        text = fmt::format(
                            "{:d} {}", uiHits[i],
                            Data().strings[kConditionNotices[g_special_attack_condition_table
                                                                 [special_attack_kind][i]]
                                               .plural]);
                        break;
                    case 2:
                        text = fmt::sprintf(Data().strings[0x1c1], uiHits[i],
                                            uiTotals[i] / uiHits[i], -1);
                        break;
                    case 3:
                        text = fmt::sprintf(Data().strings[0x1c2], uiHits[i],
                                            uiTotals[i] / uiHits[i], -1);
                        break;
                    case 4:
                        text = fmt::sprintf(Data().strings[0x1c3], uiHits[i],
                                            uiTotals[i] / uiHits[i], -1);
                        break;
                    case 5:
                        text = fmt::sprintf(Data().strings[0x199], uiHits[i],
                                            uiTotals[i] / uiHits[i], -1);
                        break;
                    default:
                        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
                        continue;
                    }
                    CurrentGame().events.push(TextFragment{.text = text, .leaves_open = true});
                }
            }
        }
    }
}

/* Kind six: the kind selects the summoned species, the record's group-size
   dice sizes it and the new group is created at the attacker's position,
   then walked in toward where the camera can see it - straight to the slot
   when the attacker is unseen, otherwise up to three camera-near placements.
   Kinds 0x18..0x1d and 0x1f map to fixed species; anything else keeps the
   kind id. */
void SpawnSummonedMonsterGroup(int special_attack_kind, W8TargetSource* pSource,
                               W8CombatSlot* pAttackerSlot)
{
    const W8MonsterRecord* record;
    int count;
    glm::vec3* position;
    W8MonsterGroup* group;
    W8MonsterInfo* monster_info;
    W8Disposition disposition;
    bool placed;
    float yaw;

    switch (special_attack_kind) {
    case 0x18:
        special_attack_kind = 0x99;
        break;
    case 0x19:
        special_attack_kind = 0x95;
        break;
    case 0x1a:
        special_attack_kind = 0x96;
        break;
    case 0x1b:
        special_attack_kind = 0x97;
        break;
    case 0x1c:
        special_attack_kind = 0x98;
        break;
    case 0x1d:
        special_attack_kind = 0x94;
        break;
    default:
        break;
    case 0x1f:
        special_attack_kind = 0xc;
        break;
    }

    record = MonsterDBFromSpecies(special_attack_kind);
    count = RollDice(&record->group_size_dice);
    position = &pAttackerSlot->point;
    group = CurrentGame().createMonsterGroup(special_attack_kind, count, position, false);
    assert(group != 0);
    FindMonster(group->leader_location_id);
    monster_info = FindMonster(pAttackerSlot->iMonsterID);
    if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
        disposition = W8_DISPOSITION_FRIENDLY;
    } else {
        disposition = W8_DISPOSITION_HOSTILE;
    }
    CurrentGame().setGroupHostility(CurrentGame().level.loaded(), group, disposition, false);
    if (monster_info->player_visibility.sight_state == W8_SIGHT_SEEN) {
        placed = PositionMonsterGroupNearCamera(group, 0.0f, 0.0f, true);
        if (!placed) {
            placed = PositionMonsterGroupNearCamera(group, 1500.0f, 0.0f, true);
        }
        if (!placed) {
            placed = PositionMonsterGroupNearCamera(group, 3000.0f, 0.0f, true);
        }
    } else {
        yaw = GetCameraFacingYaw(position);
        placed = CurrentGame().placeMonsterGroup(group, position, yaw, true, false, false, false);
    }
    if (!placed) {
        RemoveAllGroupMembers(group);
        CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_RUST, Data().strings[0x26a]});
        return;
    }
    CurrentGame().refreshSight();
    SetMonsterGroupNavigatorDirty(group, false);
    MonsterGroupEnterCombat(group);
}

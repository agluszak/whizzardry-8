#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "monster_spell_icons.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSSpellIcons.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kCameraShakeIntensityBase = 0.7900000214576721f;

/* Eight bytes per effect id. The leading dword is the icon the
   party and combat HUD strips show for the effect (-1 means none); the second
   dword is the visual resource SetMonsterSpellIcon attaches to the monster.
   The array fills the region between g_spellbook_name_ids and
   g_spell_usage_name_ids exactly. */
W8EffectVisual g_effect_visual_table[150] = {
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {224, SPELL_ICON_BLESS},
    {-1, SPELL_ICON_CHARMED},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_DISEASED},
    {213, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_PARALYZED},
    {-1, SPELL_ICON_ASLEEP},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_BLIND},
    {-1, SPELL_ICON_NONE},
    {211, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {212, SPELL_ICON_ENCHANTED_BLADE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_INSANE},
    {-1, SPELL_ICON_NONE},
    {215, SPELL_ICON_MISSILE_SHIELD},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_SLOWED},
    {-1, SPELL_ICON_WEBBED},
    {209, SPELL_ICON_ARMOR_PLATE},
    {210, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {214, SPELL_ICON_MAGIC_SCREEN},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {216, SPELL_ICON_NONE},
    {-1, SPELL_ICON_SILENCED},
    {-1, SPELL_ICON_NONE},
    {219, SPELL_ICON_ACID_CLOUD},
    {218, SPELL_ICON_ARMOR_MELT},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {225, SPELL_ICON_ELEMENT_SHIELD},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_HASTE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {226, SPELL_ICON_SOUL_SHIELD},
    {-1, SPELL_ICON_SUMMONED},
    {-1, SPELL_ICON_SUPERMAN},
    {227, SPELL_ICON_RING_OF_FIRE},
    {-1, SPELL_ICON_NONE},
    {217, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_HEXED},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {220, SPELL_ICON_TOXIC_CLOUD},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {223, SPELL_ICON_DRAINING_CLOUD},
    {221, SPELL_ICON_FIRE_STORM},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_TURNCOAT},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {222, SPELL_ICON_DEATH_CLOUD},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
    {-1, SPELL_ICON_NONE},
};

/* The monster group Summon Elemental summons - one row of four
   per power level, the column the elemental realm the caster's realm skills
   weighted the roll toward. */
int g_elemental_summon_group_ids[8][4] = {
    {24, 23, 21, 22}, {24, 23, 21, 22}, {28, 27, 25, 26}, {32, 31, 29, 30},
    {36, 35, 33, 34}, {40, 39, 37, 38}, {44, 43, 41, 42}, {48, 47, 45, 46},
};

/* The affliction spell's weighted pick - the index is the
   condition id, the value its share of a fifty-point roll. */
int g_affliction_condition_weights[W8_CONDITION_COUNT] = {
    0, 0, 0, 0, 10, 0, 10, 0, 0, 5, 0, 5, 5, 1, 0, 10, 3, 0, 1, 0,
};

/* How big the effect lands. A permanent magnitude is taken as it is; anything
   else is scaled by the definition's percentage. */
unsigned int RollEffectMagnitude(W8SpellEffectDefinition* definition)
{
    unsigned int magnitude = RollDice(&definition->magnitude);

    if (magnitude != W8_CONDITION_INDEFINITE) {
        AdjustIntegerByPercent(&magnitude, definition->percent);
    }
    return magnitude;
}

static unsigned int RollEffectDuration(int per_power, int scale, int base, unsigned int percent)
{
    unsigned int duration = per_power * scale + base;

    if (duration != W8_CONDITION_INDEFINITE) {
        ++duration;
        switch (CurrentGame().rng.below(4)) {
        case 0:
            if (duration > 1) {
                ++duration;
            }
            break;
        case 1:
            if (duration <= 2) {
                ++duration;
            }
            break;
        }
        AdjustIntegerByPercent(&duration, percent);
    }
    return duration;
}

/* How long it lasts. The three duration values combine and, unless that is
   the permanent marker, one is added for the turn it starts on. A four-way
   roll then adds one more turn on a 0 when the duration is above one, or on
   a 1 when it is at most two, before the definition's percentage scales it. */
unsigned int RollEffectDuration(W8SpellEffectDefinition* definition)
{
    return RollEffectDuration(definition->duration_per_power, definition->duration_scale,
                              definition->duration_base, definition->percent);
}

/* Take the named effect off a monster: say so, lower the flag, and drop the
   visual. */
void ClearMonsterCharm(W8MonsterInfo* monster_info)
{
    if (monster_info->charm_strength != 0) {
        PostMonsterNotice(monster_info, Data().strings[0x1ad]);
        monster_info->charm_strength = 0;
        SetMonsterSpellIcon(monster_info->p3D, SPELL_ICON_CHARMED, false);
    }
}

/* Whether the monster shrugs off an effect of the given power: effect kind
   0x14 and a monster already past the last condition tier always resist,
   otherwise the roll pits effective level against the incoming power, scaled
   by three and boosted by the magic resistance bonus and the stored
   resistance, clamped to the 5..95 band. */
bool MonsterResistsSpellEffect(const W8CombatSlot* target, int power)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    int score;

    monster_info = FindMonster(target->iMonsterID);
    monster = GetMonsterDataForInfo(monster_info);
    if (monster->kind == 0x14) {
        return true;
    }
    if (monster_info->highest_condition >= W8_CONDITION_DEAD) {
        return true;
    }
    score = (monster->effective_level - power) * 3 + monster_info->modifiers.resistance_bonus[4] +
            monster->resistances[4];
    score = std::clamp(score, 5, 0x5f);
    return static_cast<int>(CurrentGame().rng.below(100)) < score;
}

/* Empty one effect slot, dropping its visual first if it was running. The
   clear is written field by field rather than as a block, which is what leaves
   the four bytes at 0x09 untouched. */
void ClearEffectSlot(W8MonsterInfo* monster_info, W8EffectSlot* slot)
{
    if (slot->active) {
        SetMonsterSpellIcon(monster_info->p3D, g_effect_visual_table[slot->effect_id].monster_icon,
                            false);
    }
    slot->Reset();
    RebuildMonsterDerivedStats(monster_info->location_id);
}

/* Clear every live combat effect at battle end: the party blocks get the
   field-by-field wipe plus the rebuild/HUD/redraw trio, and each active
   in-combat monster loses the matching mirrored slot, its visual and a
   derived-stat rebuild. */
void ResetCombatEffects(void)
{
    W8MonsterInfo* monster_info;
    W8EffectSlot* slot;
    unsigned int index;
    int i;

    for (i = 0; i < 9; ++i) {
        if (CurrentGame().combat->effect_slots[i].active) {
            ResetPartyEffectBlock(&CurrentGame().combat->effect_slots[i]);
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            monster_info = GetMonsterListEntry(index);
            if (monster_info->fActive && monster_info->pCombat) {
                slot = &monster_info->pCombat->combat_effects[i];
                if (slot->active) {
                    ClearEffectSlot(monster_info, slot);
                }
            }
        }
    }
    for (i = 0; i < 6; ++i) {
        if (CurrentGame().combat->effect_slots0[i].active) {
            ResetPartyEffectBlock(&CurrentGame().combat->effect_slots0[i]);
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            monster_info = GetMonsterListEntry(index);
            if (monster_info->fActive && monster_info->pCombat) {
                slot = &monster_info->pCombat->combat_effects_2[i];
                if (slot->active) {
                    ClearEffectSlot(monster_info, slot);
                }
            }
        }
    }
}

/* Wipe the party-wide effect block and tell the three displays that read it. */
void ResetPartyEffectBlock(W8EffectSlot* slot)
{
    slot->Reset();
    RebuildPartyEffectBlock();
    InvalidateMainGameEffectHud();
    CurrentGame().events.push(PartyEffectsChanged{});
    CurrentGame().events.push(CombatEffectsChanged{});
}

/* How much harder each condition is to shrug off, added to the
   saving throw. */
int g_condition_resist_base[W8_CONDITION_COUNT] = {0, 1,  2,  3,  4,  4,  5,  6,  7,  8,
                                                   9, 10, 12, 14, 16, 20, 18, 22, 24, 22};

/* The target's standing against one realm: a character's clamped total, a
   monster's own figure plus its gameplay-modifier bonus. The saving throw and
   the magnitude reduction both start from it, with the target's level set
   against the effect's power level three points a level. */
void ReduceMagnitudeByResistance(unsigned int* magnitude, W8CombatSlot* target, W8SpellRealm realm,
                                 int power_level)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    W8Character* character;
    int resistance;
    unsigned int level;
    int chance;
    unsigned int half;
    unsigned int percent;

    if (*magnitude == W8_CONDITION_INDEFINITE) {
        return;
    }
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        monster = GetMonsterDataForInfo(monster_info);
        resistance = monster_info->modifiers.resistance_bonus[realm] + monster->resistances[realm];
        level = monster->effective_level;
    } else {
        character = &CurrentGame().party.characters[target->iChar];
        resistance = character->resistances[realm].total;
        level = character->uiExpLevel;
    }
    chance = resistance + (level - power_level) * 3;
    if (chance > 0) {
        ++chance;
        half = chance / 2;
        percent = CurrentGame().rng.below(chance - half) + half + CurrentGame().rng.below(half);
        if (percent > 100) {
            percent = 100;
        }
        *magnitude -= (*magnitude * percent) / 100;
    }
}

/* The saving throw. The chance to resist is the realm standing plus the
   condition's own figure plus three a level over the effect's power level,
   scaled for difficulty and held between 5 and 95. A character who knows the
   resistance skill practises it on a success. */
bool TargetResistsCondition(W8CombatSlot* target, W8SpellRealm realm, unsigned int power_level,
                            W8Condition condition_id)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    W8Character* character;
    int resistance;
    unsigned int level;
    W8Condition highest_condition;
    int chance;

    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        monster = GetMonsterDataForInfo(monster_info);
        resistance = monster_info->modifiers.resistance_bonus[realm] + monster->resistances[realm];
        level = monster->effective_level;
        highest_condition = monster_info->highest_condition;
    } else {
        assert(target->iType == W8_TARGET_KIND_CHARACTER);
        assert(target->iChar != -1);
        character = &CurrentGame().party.characters[target->iChar];
        resistance = character->resistances[realm].total;
        level = character->uiExpLevel;
        highest_condition = character->highest_condition;
    }
    if (highest_condition >= W8_CONDITION_DEAD) {
        return true;
    }

    chance = (level - power_level) * 3 + g_condition_resist_base[condition_id] + resistance;
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        ScaleValueForMonsterDifficulty(monster_info, &chance);
    } else {
        ScaleValueForCharacterDifficulty(target->iChar, &chance);
    }
    chance = std::clamp(chance, 5, 95);
    if (static_cast<int>(CurrentGame().rng.below(100)) < chance) {
        if (target->iType == W8_TARGET_KIND_CHARACTER) {
            character = &CurrentGame().party.characters[target->iChar];
            if (character->skills[W8_SKILL_IRON_WILL].active) {
                PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_IRON_WILL, 2, false);
            }
        }
        return true;
    }
    return false;
}

/* Land a condition the target failed to resist. The magnitude, cut down by
   resistance, becomes how long the condition runs; nothing lands from zero.
   Condition five first spends itself against enchantment slot five, and is
   announced as absorbed if the slot swallows all of it. A single turn is
   raised to two. Monsters always count as hit; a character's answer comes from
   InflictCondition. */
signed char InflictConditionOnTarget(W8CombatSlot* target, W8Condition condition_id,
                                     W8SpellRealm realm, unsigned int power_level, int argument,
                                     unsigned int magnitude, int source_character, int duration,
                                     bool announce)
{
    W8TargetSource source;
    W8MonsterInfo* monster_info;
    unsigned int absorbed;
    int remaining;
    signed char result = 0;

    ResetTargetSource(&source);
    ReduceMagnitudeByResistance(&magnitude, target, realm, power_level);
    if (magnitude > 0) {
        result = 1;
        if (condition_id == W8_CONDITION_SLOWED) {
            if (target->iType == W8_TARGET_KIND_CHARACTER) {
                absorbed = CurrentGame()
                               .party.characters[target->iChar]
                               .enchantments[W8_ENCHANTMENT_HASTE]
                               .turns;
                if (absorbed > 0) {
                    remaining = magnitude - absorbed;
                    AgeEnchantment(CurrentGame(), target->iChar, W8_ENCHANTMENT_HASTE, magnitude);
                    if (remaining > 0) {
                        magnitude = remaining;
                    } else {
                        if (announce) {
                            CurrentGame().events.push(CharacterNotice{
                                static_cast<unsigned int>(target->iChar),
                                fmt::format("{}!",
                                            Data().strings[kConditionNotices[W8_CONDITION_SLOWED]
                                                               .singular])});
                        }
                        return 1;
                    }
                }
            } else {
                monster_info = FindMonster(target->iMonsterID);
                absorbed = monster_info->enchantments[W8_ENCHANTMENT_HASTE].turns;
                if (absorbed > 0) {
                    remaining = magnitude - absorbed;
                    TickMonsterEnchantmentSlot(target->iMonsterID, W8_ENCHANTMENT_HASTE, magnitude);
                    if (remaining > 0) {
                        magnitude = remaining;
                    } else {
                        if (announce) {
                            CurrentGame().events.push(TextNotice{
                                W8_FONT_PALETTE_RUST,
                                fmt::format("{} {}!", GetMonsterName(monster_info, 0, 0),
                                            Data().strings[kConditionNotices[W8_CONDITION_SLOWED]
                                                               .singular])});
                        }
                        return 1;
                    }
                }
            }
        }
        if (magnitude == 1) {
            magnitude = 2;
        }
        if (target->iType == W8_TARGET_KIND_MONSTER) {
            SetMonsterCondition(target->iMonsterID, condition_id, magnitude, argument, &source,
                                announce);
        } else {
            result = InflictCondition(CurrentGame(), target->iChar,
                                      {.condition = condition_id,
                                       .duration = static_cast<int>(magnitude),
                                       .poison_strength = argument,
                                       .alternate_missing_notice = duration != 0,
                                       .announce = announce != 0});
        }
    }
    return result;
}

/* Say that whoever was aimed at shrugged the effect off. A monster target is
   told through the monster notice path and anything else through the
   character one, which is what splits the two here. */
void AnnounceEffectResisted(W8CombatSlot* target)
{
    if (g_settings.verbose_combat_messages == 0) {
        return;
    }
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        PostMonsterNotice(FindMonster(target->iMonsterID), Data().strings[0x1b3]);
        return;
    }
    CurrentGame().events.push(
        CharacterNotice{static_cast<unsigned int>(target->iChar), Data().strings[0x1b3]});
}

/* Apply an effect and say so if it did not take. Only a zero result counts as
   shrugged off. */
void ApplyEffectAndAnnounce(unsigned int* result, W8CombatSlot* target, W8SpellRealm realm,
                            int power_level)
{
    ReduceMagnitudeByResistance(result, target, realm, power_level);
    if (*result == 0) {
        AnnounceEffectResisted(target);
    }
}

/* Which of the seven enchantment slots one spell owns. Anything not among the
   seven is a caller error rather than a missing slot. */
W8EnchantmentSlot GetConditionDisplaySlot(int spell_id)
{
    switch (spell_id) {
    case W8_SPELL_DRACON_BREATH:
        return W8_ENCHANTMENT_DRACON_BREATH;
    case W8_SPELL_GUARDIAN_ANGEL:
        return W8_ENCHANTMENT_GUARDIAN_ANGEL;
    case W8_SPELL_RAZOR_CLOAK:
        return W8_ENCHANTMENT_RAZOR_CLOAK;
    case W8_SPELL_EYE_FOR_AN_EYE:
        return W8_ENCHANTMENT_EYE_FOR_AN_EYE;
    case W8_SPELL_SUPERMAN:
        return W8_ENCHANTMENT_SUPERMAN;
    case W8_SPELL_BODY_OF_STONE:
        return W8_ENCHANTMENT_BODY_OF_STONE;
    case W8_SPELL_HASTE:
        return W8_ENCHANTMENT_HASTE;
    default:
        return W8_ENCHANTMENT_NONE;
    }
}

/* A heading from a world point toward the nearest live monster, or the
   camera-facing yaw for a hostile disposition - which is also the fallback
   when the extreme-range box query turns up no monster at all. */
float HeadingTowardNearestMonster(glm::vec3 point, W8Disposition disposition, int exclusion)
{
    Monster* nearest;
    Monster* monster;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 position;
    std::vector<std::uint32_t> location_ids;
    double nearest_distance;
    float distance;
    float range;
    std::size_t count;
    unsigned int index;

    nearest = 0;
    nearest_distance = 0.0;
    lower = point;
    upper = point;
    range = CalcRangeDistance(W8_RANGE_EXTREME);
    if (disposition == W8_DISPOSITION_HOSTILE) {
        return HeadingToTargetCPP(&point);
    }
    lower.x -= range;
    lower.y -= range;
    lower.z -= range;
    upper.x += range;
    upper.y += range;
    upper.z += range;

    location_ids = GetWorldSpatialIndex()->locationsInBox({lower, upper},
                                                          static_cast<unsigned short>(exclusion));
    count = location_ids.size();
    if (count == 0) {
        return HeadingToTargetCPP(&point);
    }
    for (index = 0; index < count; ++index) {
        monster = GetMonsterByLocationID(location_ids[index]);
        if (!monster) continue;
        position = monster->nav.GetPosition();
        distance = sqrtf((position.x - point.x) * (position.x - point.x) +
                         (position.y - point.y) * (position.y - point.y) +
                         (position.z - point.z) * (position.z - point.z));
        if (!nearest || distance < nearest_distance) {
            nearest_distance = distance;
            nearest = monster;
        }
    }
    if (!nearest) return HeadingToTargetCPP(&point);
    position = nearest->nav.GetPosition();
    return GetHeadingAngle(&point, &position);
}

/* Summon Elemental chooses one of four elemental kinds, weighted by a
   character caster's realm skills. An existing bound summon prevents a new
   one; turncoat and backfire flip its disposition. A secondary roll grants
   the new group a matching enchantment. */
void ApplySummonElementalEffect(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* source_monster = nullptr;
    W8MonsterInfo* summon;
    W8MonsterInfo* member;
    W8MonsterGroup* group;
    W8Character* caster;
    W8SpellEffectDefinition attack_block;
    W8EffectSlot* effect_slot;
    unsigned int weights[4];
    unsigned int duration;
    unsigned int roll;
    W8Disposition disposition;
    int spell_id;
    W8EnchantmentSlot slot;
    int tier;
    unsigned int index;

    if (TargetSourceIsCharacter(&effect->Source, 0) &&
        GetConditionRecordFlag(effect->Source.iChar, W8_DEPENDENCE_SUMMON)) {
        if (g_settings.verbose_combat_messages != 0) {
            CurrentGame().events.push(CharacterNotice{
                static_cast<unsigned int>(effect->Source.iChar), Data().strings[0x1a8]});
        } else {
            AppendToLastTextLine(" -- ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            AppendToLastTextLine(Data().strings[0x1a8], -1);
        }
        effect->reported = true;
        return;
    }
    if (TargetSourceIsMonster(&effect->Source, 0)) {
        source_monster = FindMonster(effect->Source.iMonsterID);
        if (source_monster->elemental_summon != -1) {
            return;
        }
    }
    weights[0] = 20;
    weights[1] = 20;
    weights[2] = 20;
    weights[3] = 20;
    if (TargetSourceIsCharacter(&effect->Source, 0)) {
        caster = &CurrentGame().party.characters[effect->Source.iChar];
        weights[0] = caster->skills[W8_SKILL_FIRE_MAGIC].level + 20;
        weights[1] = caster->skills[W8_SKILL_WATER_MAGIC].level + 20;
        weights[2] = caster->skills[W8_SKILL_AIR_MAGIC].level + 20;
        weights[3] = caster->skills[W8_SKILL_EARTH_MAGIC].level + 20;
    }
    if (g_camera_sway_active) {
        weights[0] = 0;
    }
    roll = CurrentGame().rng.below(weights[0] + weights[1] + weights[2] + weights[3]);
    tier = 0;
    for (index = 0; index < 4; ++index) {
        if (roll < weights[index]) {
            tier = index;
            break;
        }
        roll -= weights[index];
    }
    if (TargetSourceIsCharacter(&effect->Source, 0)) {
        disposition = CurrentGame()
                                  .party.characters[effect->Source.iChar]
                                  .uiCondition[W8_CONDITION_TURNCOAT] == 0
                          ? W8_DISPOSITION_FRIENDLY
                          : W8_DISPOSITION_HOSTILE;
    } else if (source_monster != nullptr) {
        disposition = source_monster->ubDisposition;
    } else {
        disposition = W8_DISPOSITION_HOSTILE;
    }
    if (effect->Source.fBackfire) {
        disposition = disposition == W8_DISPOSITION_HOSTILE ? W8_DISPOSITION_FRIENDLY
                                                            : W8_DISPOSITION_HOSTILE;
    }
    group = CreateGroup(g_elemental_summon_group_ids[effect->definition.duration_scale][tier], 1,
                        &effect->target.point, true, false, true);
    assert(group != 0);
    summon = FindMonster(group->leader_location_id);
    summon->p3D->nav.SetAngles(
        HeadingTowardNearestMonster(effect->target.point, disposition, summon->location_id));
    SetMonsterGroupHostility(group, disposition, false);
    const auto summoned_state = disposition == W8_DISPOSITION_FRIENDLY ? W8_MONSTER_SUMMON_FRIENDLY
                                                                       : W8_MONSTER_SUMMON_HOSTILE;
    for (index = 0; index < group->MemberCount(); ++index) {
        member = FindMonster(group->MemberLocationId(index));
        assert(member);
        if (member->summoned == W8_MONSTER_SUMMON_NONE) {
            member->summoned = summoned_state;
            SetMonsterSpellIcon(member->p3D, SPELL_ICON_SUMMONED, true);
        }
    }
    RefreshAllSight();
    if (TargetSourceIsCharacter(&effect->Source, 0)) {
        BindMonsterToCharacterDependence(effect->Source.iChar, W8_DEPENDENCE_SUMMON,
                                         summon->location_id);
    } else if (source_monster != nullptr) {
        source_monster->elemental_summon = summon->location_id;
    }
    if (effect->definition.percent != 0 &&
        CurrentGame().rng.below(100) < effect->definition.percent) {
        attack_block = {};
        attack_block.duration_scale = effect->definition.duration_scale;
        switch (tier) {
        case 2:
        case 3:
            spell_id = tier == 2 ? 0x1a : 0x20;
            attack_block.duration_base = Data().spells[spell_id].duration_per_level;
            attack_block.duration_per_power = Data().spells[spell_id].duration;
            for (index = 0; index < 12; ++index) {
                if (g_being_effect_slot_spells[index] == spell_id) {
                    duration = RollEffectDuration(&attack_block);
                    effect_slot = &summon->effect_slots[index];
                    if (!effect_slot->active || effect_slot->effect_id != spell_id) {
                        SetMonsterSpellIcon(summon->p3D,
                                            g_effect_visual_table[spell_id].monster_icon, true);
                    }
                    effect_slot->Activate(spell_id, attack_block.duration_scale, duration);
                    RebuildMonsterDerivedStats(summon->location_id);
                    break;
                }
            }
            break;
        case 0:
        case 1:
            spell_id = tier == 1 ? 0x3d : 0x38;
            attack_block.duration_base = Data().spells[spell_id].duration_per_level;
            attack_block.duration_per_power = Data().spells[spell_id].duration;
            duration = RollEffectDuration(&attack_block);
            switch (spell_id) {
            case W8_SPELL_DRACON_BREATH:
                slot = W8_ENCHANTMENT_DRACON_BREATH;
                break;
            case W8_SPELL_GUARDIAN_ANGEL:
                slot = W8_ENCHANTMENT_GUARDIAN_ANGEL;
                break;
            case W8_SPELL_RAZOR_CLOAK:
                slot = W8_ENCHANTMENT_RAZOR_CLOAK;
                break;
            case W8_SPELL_EYE_FOR_AN_EYE:
                slot = W8_ENCHANTMENT_EYE_FOR_AN_EYE;
                break;
            case W8_SPELL_SUPERMAN:
                slot = W8_ENCHANTMENT_SUPERMAN;
                break;
            case W8_SPELL_BODY_OF_STONE:
                slot = W8_ENCHANTMENT_BODY_OF_STONE;
                break;
            case W8_SPELL_HASTE:
                slot = W8_ENCHANTMENT_HASTE;
                break;
            default:
                slot = W8_ENCHANTMENT_NONE;
                break;
            }
            ApplyMonsterCondition(summon->location_id, slot, attack_block.duration_scale, duration,
                                  0);
            break;
        }
    }
    SetMonsterGroupNavigatorDirty(group, false);
    MonsterGroupEnterCombat(group);
    effect->applied = true;
}

/* Return the casting character to the CamPos spell 0x4b stored. Nothing
   happens unless the anchor was ever set. On the same level RestoreWorldCameraState
   applies the full pose and the renderer is told to catch up; on any other
   level the 0x3c-byte record is staged into pending_move_location for LoadLevel. */
void RecallCasterToSavedLocation(W8SpellEffectEntry* pQueue)
{
    W8Character* caster;
    glm::vec3 point;

    assert(TargetSourceIsCharacter(&pQueue->Source, 0));
    caster = &CurrentGame().party.characters[pQueue->Source.iChar];
    if (caster->has_saved_location) {
        if (caster->saved_level == GetLoadedLevelID()) {
            RestoreWorldCameraState(&caster->saved_location);
            point = caster->saved_location.position;
            PlacePartyAtPoint(&point);
            return;
        }
        CurrentGame().status.pending_move_location = caster->saved_location;
        BeginLevelTransition(CurrentGame().party.characters[pQueue->Source.iChar].saved_level, -1);
    }
}

bool ResolveAttackOnTarget(const W8TargetSource* source, W8CombatSlot* target,
                           W8Condition condition_id, W8SpellRealm realm, unsigned int power_level,
                           int argument, int magnitude, bool announce_resistance,
                           bool announce_condition, int duration)
{
    W8Character* character;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    W8Condition highest_condition;
    unsigned int index;
    int source_character;
    bool resolved;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        character = &CurrentGame().party.characters[target->iChar];
        highest_condition = character->highest_condition;
    } else {
        monster_info = FindMonster(target->iMonsterID);
        monster = GetMonsterDataForInfo(monster_info);
        if (monster->instant_death_immune != 0 && condition_id == W8_CONDITION_DEAD) {
            return true;
        }
        W8ConditionImmunity* immunity = g_condition_immunities;
        for (; immunity < g_condition_immunities + 3; ++immunity) {
            if (monster->kind != immunity->kind) {
                continue;
            }
            for (index = 0; index < W8_CONDITION_COUNT; ++index) {
                if (condition_id == immunity->conditions[index]) {
                    return true;
                }
            }
        }
        highest_condition = monster_info->highest_condition;
    }

    if (highest_condition >= W8_CONDITION_DEAD) {
        return true;
    }
    if (condition_id == W8_CONDITION_MISSING) {
        if (target->iType == W8_TARGET_KIND_MONSTER) {
            return true;
        }
        if (CurrentGame().party.characters[target->iChar].uiCondition[W8_CONDITION_MISSING] != 0) {
            return true;
        }
    }

    if (TargetResistsCondition(target, realm, power_level, condition_id)) {
        resolved = true;
    } else {
        resolved = false;
        if (target->iType == W8_TARGET_KIND_CHARACTER) {
            switch (condition_id) {
            case W8_CONDITION_AFRAID:
                if (CharacterHasTrait(character, W8_TRAIT_FEARLESS)) {
                    if (announce_resistance) {
                        CurrentGame().events.push(CharacterNotice{
                            static_cast<unsigned int>(target->iChar), Data().strings[0x180]});
                    }
                    return true;
                }
                [[fallthrough]];
            case W8_CONDITION_DISEASED:
            case W8_CONDITION_IRRITATED:
            case W8_CONDITION_NAUSEATED:
            case W8_CONDITION_POISONED:
            case W8_CONDITION_ASLEEP:
                if (CharacterHasTrait(character, static_cast<W8Trait>(0x1e))) {
                    return true;
                }
                break;
            case W8_CONDITION_INSANE:
                /* Indefinite insanity gets past the mental-condition
                   immunity; the turns ride in `magnitude`. */
                if (magnitude == W8_CONDITION_INDEFINITE) {
                    break;
                }
                [[fallthrough]];
            case W8_CONDITION_TURNCOAT:
                if (CharacterHasTrait(character, W8_TRAIT_MENTAL_CONDITION_IMMUNITY)) {
                    if (announce_resistance) {
                        CurrentGame().events.push(CharacterNotice{
                            static_cast<unsigned int>(target->iChar),
                            fmt::sprintf(Data().strings[0x181],
                                         Data().strings[kConditionNotices[condition_id].name])});
                    }
                    return true;
                }
                break;
            default:
                break;
            }
        }

        source_character = -1;
        if (target->iType == W8_TARGET_KIND_MONSTER && TargetSourceIsCharacter(source, 0)) {
            source_character = source->iChar;
        }
        resolved =
            InflictConditionOnTarget(target, condition_id, realm, power_level, argument, magnitude,
                                     source_character, duration, announce_condition) == 0;
    }

    if (resolved && announce_resistance) {
        AnnounceEffectResisted(target);
    }
    return resolved;
}

/* Give every listed target the enchantment for a rolled duration. A target
   already carrying condition five first spends the new duration against the
   turns it has left, and only the remainder is applied again. */
void ApplyConditionToTargets(W8SpellEffectEntry* effect, W8EnchantmentSlot slot)
{
    W8MonsterInfo* monster_info;
    unsigned int duration;
    int character_index;
    int remaining;
    int index;

    duration = RollEffectDuration(&effect->definition);
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        if (slot == W8_ENCHANTMENT_HASTE &&
            CurrentGame().party.characters[character_index].uiCondition[W8_CONDITION_SLOWED] != 0) {
            remaining =
                duration -
                CurrentGame().party.characters[character_index].uiCondition[W8_CONDITION_SLOWED];
            AgeCondition(CurrentGame(), character_index, W8_CONDITION_SLOWED, duration);
            if (remaining > 0) {
                Enchant(CurrentGame(), character_index, W8_ENCHANTMENT_HASTE,
                        effect->definition.duration_scale, remaining, effect->definition.percent);
            }
        } else {
            Enchant(CurrentGame(), character_index, slot, effect->definition.duration_scale,
                    duration, effect->definition.percent);
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = FindMonster(effect->monster_ids[index]);
        assert(monster_info != 0);
        if (slot == W8_ENCHANTMENT_HASTE && monster_info->uiCondition[W8_CONDITION_SLOWED] != 0) {
            remaining = duration - monster_info->uiCondition[W8_CONDITION_SLOWED];
            TickMonsterCondition(monster_info->location_id, W8_CONDITION_SLOWED, duration);
            if (remaining > 0) {
                ApplyMonsterCondition(monster_info->location_id, W8_ENCHANTMENT_HASTE,
                                      effect->definition.duration_scale, remaining,
                                      effect->definition.percent);
            }
        } else {
            ApplyMonsterCondition(monster_info->location_id, slot,
                                  effect->definition.duration_scale, duration,
                                  effect->definition.percent);
        }
    }
}

/* Roll the duration, then pick one of the afflictions at random: mostly a
   guaranteed condition attack, sometimes a stamina refill or a settled
   condition. Only a single character target ever reaches here. */
void ApplyRandomAfflictionToTarget(W8SpellEffectEntry* effect)
{
    unsigned int roll;

    RollEffectDuration(&effect->definition);
    assert(effect->target.iType == W8_TARGET_KIND_CHARACTER);
    roll = CurrentGame().rng.below(100);
    if (roll < 0x28) {
        InflictConditionAttack(effect, W8_CONDITION_NAUSEATED, 100, 0);
        return;
    }
    if (roll < 0x50) {
        RestoreTargetsStamina(effect);
        return;
    }
    if (roll < 0x5a) {
        InflictConditionAttack(effect, W8_CONDITION_ASLEEP, 100, 0);
        return;
    }
    if (roll < 0x5f) {
        InflictConditionAttack(effect, W8_CONDITION_HEXED, 100, 0);
        return;
    }
    ApplyConditionToTargets(effect, W8_ENCHANTMENT_SUPERMAN);
}

static void ReportSpellDeaths(W8SpellEffectEntry* effect)
{
    while (!effect->result.reports.empty()) {
        auto report = std::move(effect->result.reports.front());
        effect->result.reports.erase(effect->result.reports.begin());
        if (report != 0) {
            if (report->kind == W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH) {
                CurrentGame().events.push(CharacterNotice{
                    static_cast<unsigned int>(report->value),
                    fmt::format("{}!",
                                Data().strings[kConditionNotices[W8_CONDITION_DEAD].singular])});
            } else if (report->kind == W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH) {
                CurrentGame().events.push(TextNotice{
                    W8_FONT_PALETTE_RUST,
                    fmt::format("{} {}!", report->text,
                                Data().strings[kConditionNotices[W8_CONDITION_DEAD].singular])});
            }
        }
    }
}

/* Post what a non-verbose effect accumulated. The total goes out as one
   "<amount>" line for a lone hit or "<count> <average>" for several, each
   nonzero condition adds its own "<count> <name>", and the queued report
   records are then drained the same way the verbose pass drains them. */
void ReportSpellEffectResult(W8SpellEffectEntry* effect)
{
    W8TextBoxMode text_box_mode;
    int condition;
    std::string condition_name;

    if (g_settings.verbose_combat_messages != 0) {
        return;
    }
    if (effect->result.count != 0) {
        text_box_mode = GetTextBoxMode();
        if (text_box_mode != W8_TEXT_BOX_MODE_NOTICE) {
            AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        if (static_cast<int>(effect->target_indices.size()) +
                static_cast<int>(effect->monster_ids.size()) ==
            1) {
            AppendToLastTextLine(fmt::sprintf(Data().strings[0x19a], effect->result.amount, -1),
                                 -1);
        } else {
            AppendToLastTextLine(fmt::sprintf(Data().strings[0x199], effect->result.count,
                                              effect->result.amount / effect->result.count, -1),
                                 -1);
        }
        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        effect->reported = true;
        effect->applied = true;
    }
    for (condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition) {
        if (effect->result.condition_counts[condition] != 0) {
            text_box_mode = GetTextBoxMode();
            if (text_box_mode != W8_TEXT_BOX_MODE_NOTICE) {
                AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
                CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            }
            if (effect->result.condition_counts[condition] == 1) {
                condition_name = Data().strings[kConditionNotices[condition].singular];
            } else {
                condition_name = Data().strings[kConditionNotices[condition].plural];
            }
            AppendToLastTextLine(
                fmt::format("{:d} {}", effect->result.condition_counts[condition], condition_name),
                -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
    }

    ReportSpellDeaths(effect);
}

/* Resolve the queued monster target with the caller's original lookup and
   null-result diagnostics. These are distinct from FindMonster's policy. */
static W8MonsterInfo* GetEffectMonsterTarget(int location_id, int lookup_line, int check_line)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    assert(monster_info != 0);
    return monster_info;
}

/* How long the target's own copy of the condition still runs; condition
   seven also hands back the argument it was set with. Any other target kind
   is a caller error. */
unsigned int GetTargetConditionTurns(W8SpellEffectEntry* effect, W8Condition condition,
                                     int* argument)
{
    W8MonsterInfo* monster_info;

    if (effect->target.iType == W8_TARGET_KIND_CHARACTER) {
        if (condition == W8_CONDITION_POISONED && argument != 0) {
            *argument = CurrentGame().party.characters[effect->target.iChar].condition_argument;
        }
        return CurrentGame().party.characters[effect->target.iChar].uiCondition[condition];
    }
    if (effect->target.iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(effect->target.iMonsterID);
        if (condition == W8_CONDITION_POISONED && argument != 0) {
            *argument = monster_info->condition_argument;
        }
        return monster_info->uiCondition[condition];
    }
    if (argument != 0) *argument = 0;
    return 0;
}

/* Wear the listed targets down: each one with any stamina left takes a fresh
   rolled magnitude of fatigue. */
void FatigueTargets(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    unsigned int magnitude;
    int character_index;
    int index;

    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        if (CurrentGame().party.characters[character_index].stamina != 0) {
            magnitude = RollEffectMagnitude(&effect->definition);
            FatigueCharacter(character_index, magnitude, false, 0);
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x871, 0x872);
        if (monster_info->stamina != 0) {
            magnitude = RollEffectMagnitude(&effect->definition);
            FatigueMonster(monster_info, magnitude, 0);
        }
    }
}

enum class RestoredResource { health, stamina };

static signed char RestoreTargetResource(W8SpellEffectEntry* effect, RestoredResource resource)
{
    const bool stamina = resource == RestoredResource::stamina;
    const bool verbose = g_settings.verbose_combat_messages != 0;
    bool all_full = true;
    unsigned int total = 0;
    unsigned int count = 0;
    int target_id = 0;
    bool target_is_character = false;
    const auto record = [&](unsigned int restored, int id, bool character) {
        if (!verbose) {
            total += restored;
            ++count;
            target_id = id;
            target_is_character = character;
        }
    };

    for (int character_index : effect->target_indices) {
        assert(character_index != -1);
        W8Character* character = &CurrentGame().party.characters[character_index];
        unsigned int missing = stamina ? 0 : character->uiHPMax - character->hp_current;
        if (stamina ? character->stamina == character->uiStaminaMax : missing == 0) {
            continue;
        }
        unsigned int restored = RollEffectMagnitude(&effect->definition);
        // Stamina ignores zero rolls; healing still applies and counts a zero roll.
        if (stamina && restored == 0) {
            continue;
        }
        if (stamina) missing = character->uiStaminaMax - character->stamina;
        restored = std::min(missing, restored);
        if (stamina) {
            RestoreCharacterStamina(character_index, restored, verbose);
            if (character->stamina < character->uiStaminaMax) all_full = false;
        } else {
            HealCharacter(character_index, restored, verbose);
            if (character->hp_current < static_cast<unsigned int>(character->uiHPMax))
                all_full = false;
        }
        record(restored, character_index, true);
    }
    for (int id : effect->monster_ids) {
        W8MonsterInfo* monster =
            GetEffectMonsterTarget(id, stamina ? 0x800 : 0x77d, stamina ? 0x801 : 0x77e);
        unsigned int missing = stamina ? 0 : monster->uiHPMax - monster->hp_current;
        if (stamina ? monster->stamina == monster->stamina_max : missing == 0) {
            continue;
        }
        unsigned int restored = RollEffectMagnitude(&effect->definition);
        if (stamina && restored == 0) {
            continue;
        }
        if (stamina) missing = monster->stamina_max - monster->stamina;
        restored = std::min(missing, restored);
        if (stamina) {
            RestoreMonsterStamina(monster, restored, true);
            if (static_cast<unsigned int>(monster->stamina) <
                static_cast<unsigned int>(monster->stamina_max))
                all_full = false;
        } else {
            HealMonster(monster, restored, verbose);
            if (monster->hp_current < static_cast<unsigned int>(monster->uiHPMax)) all_full = false;
        }
        record(restored, monster->location_id, false);
    }
    if (!verbose && count != 0) {
        if (stamina) effect->applied = true;
        if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
            AppendToLastTextLine(" -- ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        if (count == 1) {
            const std::string name = target_is_character
                                         ? CurrentGame().party.characters[target_id].name
                                         : GetMonsterName(FindMonster(target_id), 0, 0);
            const int format = stamina ? (target_is_character ? (total != 1) + 0x1a1 : 0x1a2)
                                       : (total != 1) + 0x19d;
            AppendToLastTextLine(fmt::sprintf(Data().strings[format], name, total, -1), -1);
        } else {
            const int format = stamina ? (total != 1) + 0x19f : (total / count != 1) + 0x19b;
            AppendToLastTextLine(fmt::sprintf(Data().strings[format], count, total / count, -1),
                                 -1);
        }
        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        effect->reported = true;
        if (!stamina) effect->applied = true;
    }
    return all_full;
}

/* Put stamina back into the listed targets, capped at what each one is missing. */
signed char RestoreTargetsStamina(W8SpellEffectEntry* effect)
{
    return RestoreTargetResource(effect, RestoredResource::stamina);
}

/* Heal the listed targets, capped at what each one is missing. */
signed char HealTargets(W8SpellEffectEntry* effect)
{
    return RestoreTargetResource(effect, RestoredResource::health);
}

/* Roll a magnitude per listed target, cut it by the target's resistance in
   the spell's realm, and apply what is left as damage. A fully resisted hit
   is only announced; a monster's soak bookkeeping still runs on it. */
void ApplyDamageToTargets(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8CombatSlot target;
    W8SpellEffectResult* result;
    bool verbose;
    unsigned int magnitude;
    int character_index;
    W8SpellRealm realm;
    int power_level;
    int index;

    verbose = g_settings.verbose_combat_messages != 0;
    realm = Data().spells[effect->kind].realm;
    power_level = effect->definition.power_level;
    TargetSourceIsCharacter(&effect->Source, 1);
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        magnitude = RollEffectMagnitude(&effect->definition);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = character_index;
        ApplyEffectAndAnnounce(&magnitude, &target, realm, power_level);
        if (magnitude != 0) {
            if (!verbose) {
                result = &effect->result;
            } else {
                result = 0;
            }
            ApplyDamageToCharacter(character_index, magnitude, false, verbose, false, result,
                                   false);
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x6ac, 0x6ad);
        magnitude = RollEffectMagnitude(&effect->definition);
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = monster_info->location_id;
        ApplyEffectAndAnnounce(&magnitude, &target, realm, power_level);
        if (magnitude == 0) {
            monster_info->p3D->SpawnDamageNumber(magnitude);
        } else {
            if (!verbose) {
                result = &effect->result;
            } else {
                result = 0;
            }
            ApplyDamageToMonster(monster_info, magnitude, &effect->Source, false, verbose, 0,
                                 result, false);
        }
    }
    if (!verbose) {
        ReportSpellEffectResult(effect);
    }
}

/* Drain the listed targets: the resisted remainder of a rolled magnitude is
   taken as damage, never more than the target has left, and the total dealt
   is handed back to the caster - as healing for spell 0x52, as spell points
   for spell 0x54. */
void DrainTargetsLife(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8CombatSlot target;
    W8SpellEffectResult* result;
    bool announce;
    unsigned int magnitude;
    unsigned int drained;
    int character_index;
    W8SpellRealm realm;
    int power_level;
    int index;

    announce = g_settings.verbose_combat_messages != 0;
    realm = Data().spells[effect->kind].realm;
    power_level = effect->definition.power_level;
    drained = 0;
    TargetSourceIsCharacter(&effect->Source, 1);
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        magnitude = RollEffectMagnitude(&effect->definition);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = character_index;
        ApplyEffectAndAnnounce(&magnitude, &target, realm, power_level);
        if (CurrentGame().party.characters[character_index].hp_current <= magnitude) {
            magnitude = CurrentGame().party.characters[character_index].hp_current;
        }
        if (magnitude != 0) {
            if (!announce) {
                result = &effect->result;
            } else {
                result = 0;
            }
            ApplyDamageToCharacter(character_index, magnitude, false, announce, false, result,
                                   false);
            drained += magnitude;
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x704, 0x705);
        magnitude = RollEffectMagnitude(&effect->definition);
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = monster_info->location_id;
        ApplyEffectAndAnnounce(&magnitude, &target, realm, power_level);
        if (monster_info->hp_current <= magnitude) {
            magnitude = monster_info->hp_current;
        }
        if (magnitude != 0) {
            if (!announce) {
                result = &effect->result;
            } else {
                result = 0;
            }
            ApplyDamageToMonster(monster_info, magnitude, &effect->Source, false, announce, 0,
                                 result, false);
            drained += magnitude;
        }
    }
    if (!announce) {
        ReportSpellEffectResult(effect);
    }
    if (!TargetSourceIsCharacter(&effect->Source, 0)) {
        if (TargetSourceIsMonster(&effect->Source, 0)) {
            monster_info = FindMonster(effect->Source.iMonsterID);
            assert(monster_info != 0);
            if (effect->kind == W8_SPELL_LIFESTEAL) {
                HealMonster(monster_info, (drained * 7) / 10, announce);
            } else if (effect->kind == W8_SPELL_MIGHT_TO_MAGIC) {
                monster_info->spell_points += drained >> 1;
            }
        }
    } else if (effect->kind == W8_SPELL_LIFESTEAL) {
        unsigned int slot = FindPartySlotWithLowestHitPoints();
        if (CurrentGame().party.members[slot].has_value() &&
            CurrentGame().party.characters[slot].hp_current <
                static_cast<unsigned int>(CurrentGame().party.characters[slot].uiHPMax)) {
            HealCharacter(slot, (drained * 7) / 10, announce);
        }
    } else if (effect->kind == W8_SPELL_MIGHT_TO_MAGIC) {
        unsigned int slot = FindPartySlotWithLowestSpellPoints();
        if (CurrentGame().party.members[slot].has_value()) {
            W8Character* character = &CurrentGame().party.characters[slot];
            if (SumCharacterSpellPointsLeft(character) < SumCharacterSpellPoints(character)) {
                RestoreCharacterSpellPointsEvenly(slot, drained);
            }
        }
    }
}

/* Wear down or lift a condition on every listed target. A permanent
   condition, or a forced cure, only comes off on a removal-chance roll - a
   hundred percent for conditions one and eighteen and for anything the cure
   power already covers, argument times fifteen otherwise. Lifting death
   leaves the character barely alive and may queue the revive event. A
   condition the roll fails simply ticks down by the squared power and counts
   toward the resisted report. */
signed char TryCureConditionOnTargets(W8SpellEffectEntry* effect, W8Condition condition, bool force)
{
    W8NpcState* npc_state;
    W8MonsterInfo* monster_info;
    unsigned int power;
    unsigned int turns;
    unsigned int restored;
    unsigned int chance;
    int character_index;
    int remaining_count;
    bool all_cured;
    int index;

    power = effect->definition.duration_scale * effect->definition.duration_scale;
    all_cured = true;
    remaining_count = 0;
    AdjustIntegerByPercent(&power, effect->definition.percent);
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        W8Character* character = &CurrentGame().party.characters[character_index];
        if (character->uiCondition[condition] != 0) {
            if (character->uiCondition[condition] == W8_CONDITION_INDEFINITE || force) {
                if (condition == W8_CONDITION_DEAD ||
                    (condition == W8_CONDITION_DRAINED || force)) {
                    chance = 100;
                } else {
                    chance = effect->definition.duration_scale * 0xf;
                }
                if (CurrentGame().rng.below(100) < chance) {
                    LiftCondition(CurrentGame(), character_index, condition, true);
                    effect->applied = true;
                    effect->reported = true;
                    if (condition == W8_CONDITION_DEAD) {
                        restored = (character->attributes[W8_ATTRIBUTE_PIETY].effective +
                                    character->attributes[W8_ATTRIBUTE_VITALITY].effective) >>
                                   2;
                        turns = (character->uiHPMax * restored) / 100;
                        if (turns < 2) {
                            turns = 1;
                        }
                        character->hp_current = turns;
                        turns = (character->uiStaminaMax * restored) / 100;
                        if (turns < 2) {
                            turns = 1;
                        }
                        character->stamina = turns;
                        int npc_index = CurrentGame().party.npcIndex(character_index);
                        if (npc_index != -1 &&
                            (npc_state = GetNpcState(npc_index), npc_state != 0)) {
                            npc_state->spawned = 0;
                        }
                    }
                    if (CurrentGame().rng.below(100) < 0x32) {
                        QueueCharacterEvent(character, g_special_event14, 0,
                                            kCharacterEventFullVolume);
                    }
                } else {
                    all_cured = false;
                }
            } else {
                AgeCondition(CurrentGame(), character_index, condition, power);
                if (character->uiCondition[condition] == 0) {
                    effect->reported = true;
                } else {
                    ++remaining_count;
                    all_cured = false;
                    if (g_settings.verbose_combat_messages != 0) {
                        CurrentGame().events.push(CharacterNotice{
                            static_cast<unsigned int>(character_index),
                            fmt::sprintf(Data().strings[0x1b1],
                                         Data().strings[kConditionNotices[condition].noun])});
                        effect->applied = true;
                        continue;
                    }
                }
                effect->applied = true;
            }
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x558, 0x559);
        turns = monster_info->uiCondition[condition];
        if (turns != 0) {
            if (turns == W8_CONDITION_INDEFINITE || turns <= power || force) {
                if (condition == W8_CONDITION_DEAD || condition == W8_CONDITION_DRAINED ||
                    turns <= power || force) {
                    chance = 100;
                } else {
                    chance = effect->definition.duration_scale * 0xf;
                }
                if (CurrentGame().rng.below(100) < chance) {
                    ClearMonsterCondition(monster_info->location_id, condition);
                    effect->applied = true;
                } else {
                    all_cured = false;
                }
            } else {
                monster_info->uiCondition[condition] = turns - power;
                ++remaining_count;
                all_cured = false;
                effect->applied = true;
            }
        }
    }
    if (g_settings.verbose_combat_messages == 0 && remaining_count != 0) {
        if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
            AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        if (remaining_count == 1) {
            if (effect->target.iType == W8_TARGET_KIND_CHARACTER) {
                CurrentGame().events.push(CharacterNotice{
                    static_cast<unsigned int>(effect->target.iChar),
                    fmt::sprintf(Data().strings[0x1b1],
                                 Data().strings[kConditionNotices[condition].noun])});
                effect->reported = true;
                return all_cured;
            }
            if (effect->target.iType == W8_TARGET_KIND_MONSTER) {
                monster_info = FindMonster(effect->target.iMonsterID);
                PostMonsterNotice(monster_info,
                                  fmt::sprintf(Data().strings[0x1b1],
                                               Data().strings[kConditionNotices[condition].noun]));
                effect->reported = true;
                return all_cured;
            }
        }
        AppendToLastTextLine(fmt::sprintf(Data().strings[0x1b2], remaining_count,
                                          Data().strings[kConditionNotices[condition].noun], -1),
                             -1);
        effect->reported = true;
    }
    return all_cured;
}

/* Resolve the condition and any follow-up attack after the caller has
   selected a character or monster. Return true only for quiet-mode totals. */
static bool ApplyConditionAttackToTarget(W8SpellEffectEntry* effect, W8CombatSlot* target,
                                         W8Condition condition, int argument, unsigned int duration,
                                         bool verbose, W8MonsterInfo* monster_info)
{
    const bool monster = target->iType == W8_TARGET_KIND_MONSTER;
    const int character_index = monster ? -1 : target->iChar;
    if (effect->kind == W8_SPELL_SLOW) {
        const unsigned int haste_turns =
            monster ? monster_info->enchantments[W8_ENCHANTMENT_HASTE].turns
                    : CurrentGame()
                          .party.characters[character_index]
                          .enchantments[W8_ENCHANTMENT_HASTE]
                          .turns;
        if (haste_turns != 0) {
            const int remaining = static_cast<int>(duration) - static_cast<int>(haste_turns);
            if (monster)
                TickMonsterEnchantmentSlot(monster_info->location_id, W8_ENCHANTMENT_HASTE,
                                           duration);
            else
                AgeEnchantment(CurrentGame(), character_index, W8_ENCHANTMENT_HASTE, duration);
            return remaining > 0 &&
                   !ResolveAttackOnTarget(
                       &effect->Source, target, condition, Data().spells[effect->kind].realm,
                       effect->definition.power_level, argument, remaining, verbose, verbose, 0) &&
                   !verbose;
        }
    }

    if (ResolveAttackOnTarget(&effect->Source, target, condition, Data().spells[effect->kind].realm,
                              effect->definition.power_level, argument, duration, verbose, verbose,
                              0))
        return false;

    if (effect->kind == W8_SPELL_TERROR && !monster) {
        ResolveAttackOnTarget(&effect->Source, target, W8_CONDITION_UNCONSCIOUS,
                              Data().spells[W8_SPELL_TERROR].realm,
                              static_cast<unsigned int>(effect->definition.power_level) >> 1,
                              argument, duration >> 1, false, verbose, 0);
    } else if (monster &&
               (effect->kind == W8_SPELL_TERROR || effect->kind == W8_SPELL_SONIC_BOOM)) {
        ResolveAttackOnTarget(&effect->Source, target, condition, Data().spells[effect->kind].realm,
                              static_cast<unsigned int>(effect->definition.power_level) >> 1,
                              argument, duration >> 1, false, verbose, 0);
    }
    return !verbose;
}

/* Attack every listed target with a condition on a chance roll each. The
   rolled duration is what the condition runs for; spell 0x1d first spends it
   against enchantment slot five, and the elemental spells 0xe and 0x1e strike
   a monster a second time at half strength. Quiet mode counts the targets the
   attack landed on and reports the tally. */
void InflictConditionAttack(W8SpellEffectEntry* effect, W8Condition condition, int chance,
                            int argument)
{
    W8MonsterInfo* monster_info;
    W8CombatSlot target;
    bool verbose;
    unsigned int duration;
    int affected;
    int character_index;
    int index;
    std::string notice;

    verbose = g_settings.verbose_combat_messages != 0;
    affected = 0;
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        if (CurrentGame().rng.below(100) >= static_cast<unsigned int>(chance)) {
            continue;
        }
        duration = RollEffectDuration(&effect->definition);
        character_index = effect->target_indices[index];
        assert(character_index != -1);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = character_index;
        if (ApplyConditionAttackToTarget(effect, &target, condition, argument, duration, verbose,
                                         nullptr)) {
            ++affected;
        }
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        if (CurrentGame().rng.below(100) >= static_cast<unsigned int>(chance)) {
            continue;
        }
        duration = RollEffectDuration(&effect->definition);
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x43d, 0x43e);
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = monster_info->location_id;
        if (ApplyConditionAttackToTarget(effect, &target, condition, argument, duration, verbose,
                                         monster_info)) {
            ++affected;
        }
    }
    if (verbose || affected == 0) {
        return;
    }
    if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
        AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
    }
    if (static_cast<int>(effect->monster_ids.size()) +
            static_cast<int>(effect->target_indices.size()) !=
        1) {
        if (affected == 1) {
            notice = Data().strings[kConditionNotices[condition].singular];
        } else {
            notice = Data().strings[kConditionNotices[condition].plural];
        }
        AppendToLastTextLine(fmt::format("{:d} {}", affected, notice), -1);
        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
    } else {
        notice = Data().strings[kConditionNotices[condition].singular];
        if (target.iType == W8_TARGET_KIND_CHARACTER) {
            AppendToLastTextLine(
                fmt::format("{} {}", CurrentGame().party.characters[character_index].name, notice),
                -1);
        } else if (target.iType == W8_TARGET_KIND_MONSTER) {
            monster_info = FindMonster(target.iMonsterID);
            AppendToLastTextLine(fmt::format("{} {}", GetMonsterName(monster_info, 0, 0), notice),
                                 -1);
        } else {
            AppendToLastTextLine(fmt::format("{:d} {}", affected, notice), -1);
        }
        CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
    }
    effect->reported = true;
    effect->applied = true;
}

/* Install the spell's being-effect slot on the party and on every listed
   monster. The slot index is the spell's place in the being table; a slot
   that was not already running this spell drops the old visual first. */
void ApplyBeingEffectSlot(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8EffectSlot* slot;
    unsigned int duration;
    const int* table;
    int slot_index;
    int index;

    duration = RollEffectDuration(&effect->definition);
    slot_index = 0;
    table = g_being_effect_slot_spells;
    while (effect->kind != *table) {
        ++table;
        ++slot_index;
        assert(g_combat_effect_slot_spells > table);
    }
    if (!effect->target_indices.empty()) {
        slot = &CurrentGame().status.effect_slots[slot_index];
        slot->Activate(effect->kind, effect->definition.duration_scale, duration);
        RebuildPartyEffectBlock();
        CurrentGame().events.push(PartyEffectsChanged{});
        CurrentGame().events.push(CombatEffectsChanged{});
        effect->applied = true;
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x30d, 0x30e);
        slot = &monster_info->effect_slots[slot_index];
        if (!slot->active || slot->effect_id != effect->kind) {
            SetMonsterSpellIcon(monster_info->p3D, g_effect_visual_table[effect->kind].monster_icon,
                                true);
        }
        slot->Activate(effect->kind, effect->definition.duration_scale, duration);
        RebuildMonsterDerivedStats(monster_info->location_id);
        effect->applied = true;
    }
}

/* Roll for each listed target: the attack condition goes out against a
   chance, and a target that shakes it off still takes the second condition
   the effect carries. */
void AttackThenInflictCondition(W8SpellEffectEntry* effect, W8Condition attack_condition,
                                W8Condition condition, int chance, int attack_argument,
                                int condition_argument)
{
    W8CombatSlot target;
    const int source_character =
        TargetSourceIsCharacter(&effect->Source, 0) ? effect->Source.iChar : -1;
    const std::size_t character_count = effect->target_indices.size();
    const std::size_t total = character_count + effect->monster_ids.size();

    for (std::size_t index = 0; index < total; ++index) {
        if (CurrentGame().rng.below(100) >= static_cast<unsigned int>(chance)) continue;

        const unsigned int duration = RollEffectDuration(&effect->definition);
        if (index < character_count) {
            const int character_index = effect->target_indices[index];
            assert(character_index != -1);
            target.iType = W8_TARGET_KIND_CHARACTER;
            target.iChar = character_index;
        } else {
            W8MonsterInfo* monster =
                GetEffectMonsterTarget(effect->monster_ids[index - character_count], 0x4d8, 0x4d9);
            target.iType = W8_TARGET_KIND_MONSTER;
            target.iMonsterID = monster->location_id;
        }
        if (ResolveAttackOnTarget(&effect->Source, &target, attack_condition,
                                  Data().spells[effect->kind].realm, effect->definition.power_level,
                                  attack_argument, duration, false, true, 0)) {
            InflictConditionOnTarget(&target, condition, Data().spells[effect->kind].realm,
                                     effect->definition.power_level, condition_argument, duration,
                                     source_character, 0, true);
        }
    }
}

/* Fill the spell's slot in the first combat-effect block on the party and on
   every listed monster. A monster not yet in combat has its group pulled in
   first; one that already fights takes the slot straight away and, when the
   source is a character, hates them a little more for it. */
void ApplyCombatEffectSlot(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8EffectSlot* slot;
    unsigned int duration;
    const int* table;
    int source_character = -1;
    int spell_id;
    int slot_index;
    int index;

    spell_id = effect->kind;
    assert(CurrentGame().combat.has_value());
    duration = RollEffectDuration(&effect->definition);
    if (TargetSourceIsCharacter(&effect->Source, 1)) {
        source_character = effect->Source.iChar;
    }
    slot_index = 0;
    table = g_combat_effect_slot_spells;
    while (spell_id != *table) {
        ++table;
        ++slot_index;
        if (table >= g_combat_effect_slot_spells_and_cast_success) {
            break;
        }
    }
    if (spell_id != *table) {
        assert(false && "spell has no combat effect slot");
    } else {
        if (!effect->target_indices.empty()) {
            slot = &CurrentGame().combat->effect_slots[slot_index];
            slot->Activate(spell_id, effect->definition.duration_scale, duration);
            RebuildPartyEffectBlock();
            CurrentGame().events.push(PartyEffectsChanged{});
            CurrentGame().events.push(CombatEffectsChanged{});
            effect->applied = true;
            effect->result.count += CountActiveCharacters();
        }
        for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
            monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x342, 0x343);
            if (!monster_info->pCombat) {
                MonsterGroupEnterCombat(FindMonsterGroup(monster_info->monster_group_id));
                if (!monster_info->pCombat) {
                    continue;
                }
            }
            slot = &monster_info->pCombat->combat_effects[slot_index];
            if (!slot->active || slot->effect_id != spell_id) {
                SetMonsterSpellIcon(monster_info->p3D, g_effect_visual_table[spell_id].monster_icon,
                                    true);
            }
            slot->Activate(spell_id, effect->definition.duration_scale, duration);
            RebuildMonsterDerivedStats(monster_info->location_id);
            if (source_character != -1) {
                const W8MonsterRecord* monster = GetMonsterDataForInfo(monster_info);
                monster_info->pCombat->character_hate[source_character] +=
                    monster->effective_level * effect->definition.duration_scale;
            }
            effect->applied = true;
            ++effect->result.count;
        }
    }
    if (effect->result.count != 0) {
        if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
            AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        AppendToLastTextLine(fmt::sprintf(Data().strings[0x1a4], effect->result.count, -1), -1);
        effect->reported = true;
    }
}

/* The second combat-effect block: the six defensive slots past the offensive
   nine. Same dance as ApplyCombatEffectSlot, minus the hate and the report. */
void ApplyDefenseEffectSlot(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8EffectSlot* slot;
    unsigned int duration;
    const int* table;
    int spell_id;
    int slot_index;
    int index;

    spell_id = effect->kind;
    assert(CurrentGame().combat.has_value());
    duration = RollEffectDuration(&effect->definition);
    slot_index = 0;
    table = g_combat_effect_slot_spells_and_cast_success;
    while (spell_id != *table) {
        ++table;
        ++slot_index;
        assert(table < g_combat_effect_slot_spells_and_cast_success + 9);
    }
    if (!effect->target_indices.empty()) {
        slot = &CurrentGame().combat->effect_slots0[slot_index];
        slot->Activate(spell_id, effect->definition.duration_scale, duration);
        RebuildPartyEffectBlock();
        CurrentGame().events.push(PartyEffectsChanged{});
        CurrentGame().events.push(CombatEffectsChanged{});
        effect->applied = true;
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x3d8, 0x3d9);
        if (!monster_info->pCombat) {
            MonsterGroupEnterCombat(FindMonsterGroup(monster_info->monster_group_id));
            if (!monster_info->pCombat) {
                continue;
            }
        }
        slot = &monster_info->pCombat->combat_effects_2[slot_index];
        if (!slot->active || slot->effect_id != spell_id) {
            SetMonsterSpellIcon(monster_info->p3D, g_effect_visual_table[spell_id].monster_icon,
                                true);
        }
        slot->Activate(spell_id, effect->definition.duration_scale, duration);
        RebuildMonsterDerivedStats(monster_info->location_id);
        effect->applied = true;
    }
}

static void ResolveAfflictionCondition(W8SpellEffectEntry* effect, W8CombatSlot* target,
                                       W8SpellRealm realm, int power_level, unsigned int duration,
                                       bool verbose, unsigned int roll)
{
    for (int weight_index = 0; weight_index < W8_CONDITION_COUNT; ++weight_index) {
        int weight = g_affliction_condition_weights[weight_index];
        if (weight != 0) {
            if (roll < static_cast<unsigned int>(weight)) {
                bool landed = ResolveAttackOnTarget(&effect->Source, target,
                                                    static_cast<W8Condition>(weight_index), realm,
                                                    power_level, 0, duration, verbose, verbose, 0);
                if (!landed) {
                    ++effect->result.condition_counts[weight_index];
                }
                break;
            }
            roll -= weight;
        }
    }
}

/* Apply a single affliction roll after the caller has identified its target. */
static void ResolveAfflictionForTarget(W8SpellEffectEntry* effect, W8CombatSlot* target,
                                       W8SpellRealm realm, int power_level, unsigned int duration,
                                       bool verbose, W8MonsterInfo* monster_info)
{
    const unsigned int roll = CurrentGame().rng.below(100);
    if (roll < 0x32) {
        ResolveAfflictionCondition(effect, target, realm, power_level, duration, verbose, roll);
        return;
    }

    unsigned int magnitude = RollEffectMagnitude(&effect->definition);
    ReduceMagnitudeByResistance(&magnitude, target, realm, power_level);
    if (magnitude == 0) {
        if (verbose) AnnounceEffectResisted(target);
        return;
    }
    W8SpellEffectResult* result = verbose ? nullptr : &effect->result;
    if (target->iType == W8_TARGET_KIND_CHARACTER)
        ApplyDamageToCharacter(target->iChar, magnitude, false, verbose, false, result, false);
    else
        ApplyDamageToMonster(monster_info, magnitude, &effect->Source, false, verbose, 0, result,
                             false);
}

/* The affliction spells: each listed target takes either a weighted-random
   condition attack or a straight damage roll, about a coin flip each. With
   verbose reporting off the accumulated result is flushed at the end. */
void ResolveAfflictionAgainstTargets(W8SpellEffectEntry* effect)
{
    W8SpellRealm realm;
    int power_level;
    bool verbose;
    W8CombatSlot target;
    W8MonsterInfo* monster_info;
    int index;
    int party_slot;

    realm = Data().spells[effect->kind].realm;
    power_level = effect->definition.power_level;
    verbose = g_settings.verbose_combat_messages != 0;
    TargetSourceIsCharacter(&effect->Source, 1);
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        unsigned int duration = RollEffectDuration(&effect->definition);
        party_slot = effect->target_indices[index];
        assert(party_slot != -1);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = party_slot;
        ResolveAfflictionForTarget(effect, &target, realm, power_level, duration, verbose, nullptr);
    }
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        unsigned int duration = RollEffectDuration(&effect->definition);
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x907, 0x908);
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = monster_info->location_id;
        ResolveAfflictionForTarget(effect, &target, realm, power_level, duration, verbose,
                                   monster_info);
    }
    if (!verbose) {
        ReportSpellEffectResult(effect);
    }
}

/* The mass-damage spells: every listed character takes the raw dice roll,
   every listed monster the percentage-adjusted one, each shrunk by realm
   resistance. The monster pass reuses the character target slot, so a
   resisted monster reports under the last character's name. With verbose
   reporting off the totals go out as one line and the report queue drains. */
void DamageTargetsAndReport(W8SpellEffectEntry* effect)
{
    W8SpellEffectResult* result;
    W8MonsterInfo* monster_info;
    bool verbose;
    unsigned int magnitude;
    unsigned int total;
    unsigned int hits;
    int party_slot;
    int index;
    W8CombatSlot target;

    result = &effect->result;
    verbose = g_settings.verbose_combat_messages != 0;
    total = 0;
    hits = 0;
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        party_slot = effect->target_indices[index];
        assert(party_slot != -1);
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = party_slot;
        magnitude = RollDice(&effect->definition.magnitude);
        ApplyEffectAndAnnounce(&magnitude, &target, Data().spells[effect->kind].realm,
                               effect->definition.power_level);
        if (magnitude != 0) {
            DamageCharacter(party_slot, magnitude, verbose);
            total += magnitude;
            ++hits;
        }
    }
    TargetSourceIsCharacter(&effect->Source, 1);
    for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
        monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0xb39, 0xb3a);
        magnitude = RollEffectMagnitude(&effect->definition);
        ApplyEffectAndAnnounce(&magnitude, &target, Data().spells[effect->kind].realm,
                               effect->definition.power_level);
        if (magnitude != 0) {
            ApplyDamageToMonster(monster_info, magnitude, &effect->Source, false, verbose, 0,
                                 verbose ? 0 : result, false);
            total += magnitude;
            ++hits;
        }
    }
    if (!verbose) {
        if (hits != 0) {
            if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
                AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
                CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            }
            if (static_cast<int>(effect->monster_ids.size()) +
                    static_cast<int>(effect->target_indices.size()) ==
                1) {
                AppendToLastTextLine(fmt::sprintf(Data().strings[0x1a6], total, -1), -1);
            } else {
                AppendToLastTextLine(fmt::sprintf(Data().strings[0x1a7], hits, total / hits, -1),
                                     -1);
            }
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            effect->reported = true;
            effect->applied = true;
        }
        ReportSpellDeaths(effect);
    }
}

static void DestroyItemConsumables(W8ItemLocation location, unsigned int chance, int* totals)
{
    const W8ItemInstance* item = CurrentGame().party.itemAt(location);
    unsigned int destroyed;
    unsigned int unit;
    if (item->iItemNo != -1 &&
        (Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_POTION ||
         (Data().items[item->iItemNo].equip_class > W8_ITEM_EQUIP_CLASS_POWDER &&
          Data().items[item->iItemNo].equip_class < W8_ITEM_EQUIP_CLASS_FOOD))) {
        destroyed = 0;
        for (unit = 0; unit < item->stack_count; ++unit) {
            if (CurrentGame().rng.below(100) < chance) {
                ++destroyed;
            }
        }
        const auto equip_class = Data().items[item->iItemNo].equip_class;
        if (equip_class == W8_ITEM_EQUIP_CLASS_POTION) {
            totals[0] += destroyed;
        } else if (equip_class == W8_ITEM_EQUIP_CLASS_SPELLBOOK) {
            totals[2] += destroyed;
        } else if (equip_class == W8_ITEM_EQUIP_CLASS_SCROLL) {
            totals[1] += destroyed;
        }
        RemoveInventoryQuantity(CurrentGame(), location, destroyed);
    }
}

/* Destroy potion, scroll and spellbook stacks held by
   the listed characters or sitting in the party pool. Roll `argument * 10`
   percent per unit and sheds what fails. The three class totals each get
   their own summary line. */
void DestroyConsumablesOnTargets(W8SpellEffectEntry* effect)
{
    bool verbose;
    unsigned int chance;
    int totals[3];
    int index;
    int slot;

    verbose = g_settings.verbose_combat_messages != 0;
    chance = effect->definition.duration_scale * 10;
    totals[2] = 0;
    totals[1] = 0;
    totals[0] = 0;
    for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
        int party_slot = effect->target_indices[index];
        assert(party_slot != -1);
        const auto member = static_cast<unsigned int>(party_slot);
        for (slot = 0; slot < 12; ++slot)
            DestroyItemConsumables({W8ItemStorage::Equipment, member, static_cast<unsigned>(slot)},
                                   chance, totals);
        for (slot = 0; slot < 8; ++slot)
            DestroyItemConsumables({W8ItemStorage::Backpack, member, static_cast<unsigned>(slot)},
                                   chance, totals);
    }
    if (!effect->target_indices.empty()) {
        for (index = 0; static_cast<unsigned int>(index) < CurrentGame().party.item_count;
             ++index) {
            DestroyItemConsumables({W8ItemStorage::Pool, 0, static_cast<unsigned>(index)}, chance,
                                   totals);
        }
    }
    if (totals[2] + totals[1] + totals[0] != 0) {
        for (index = 0; index < 3; ++index) {
            if (totals[index] != 0) {
                if (!verbose) {
                    AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
                }
                CurrentGame().events.push(TextBoxModeChange{
                    verbose ? W8_TEXT_BOX_MODE_NOTICE : W8_TEXT_BOX_MODE_APPEND, -1});
                if (index == 0) {
                    AppendToLastTextLine(fmt::sprintf(Data().strings[0x1fa], totals[0], -1), -1);
                } else if (index == 1) {
                    AppendToLastTextLine(fmt::sprintf(Data().strings[0x1fb], totals[1], -1), -1);
                } else if (index == 2) {
                    AppendToLastTextLine(fmt::sprintf(Data().strings[0x1fc], totals[2], -1), -1);
                }
                if (!verbose) {
                    CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
                }
                effect->reported = true;
            }
        }
        effect->applied = true;
    }
}

/* The lure effect, spell 0x26: every hostile monster in the world-far-clip
   box around the target point that the effect can see and that is not already
   spoken for gets a resistance roll. A monster that fails is switched to the
   controlled state - its whole group when the party is out of combat, just
   itself in combat - and one that resists is switched the other way. The
   0x400-byte query buffer is not freed, matching retail. */
void ApplyMonsterControlToNearbyMonsters(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8CombatSlot target;
    glm::vec3 center;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 eye;
    std::vector<std::uint32_t> location_ids;
    std::size_t count;
    unsigned int index;
    float far_clip;

    far_clip = static_cast<float>(WorldGetFarClip(GetWorld()));
    center = effect->target.point;
    center.y += g_default_world_height * 0.5f;
    lower = glm::vec3(center.x - far_clip, center.y - far_clip, center.z - far_clip);
    upper = glm::vec3(center.x + far_clip, center.y + far_clip, center.z + far_clip);

    location_ids = GetWorldSpatialIndex()->locationsInBox({lower, upper}, 0);
    count = location_ids.size();
    for (index = 0; index < count; ++index) {
        monster_info = FindMonster(location_ids[index]);
        if (monster_info->p3D->IsDying()) {
            continue;
        }
        if ((!CurrentGame().combat.has_value() && monster_info->p3D->nav.linked_navigator != 0) ||
            monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
            continue;
        }
        eye = monster_info->p3D->nav.movement.position;
        eye.y += monster_info->p3D->nav.movement.height_offset;
        if (!GetWorldSpatialIndex()->hasLineOfSight(eye, center)) {
            continue;
        }
        if (monster_info->control_state < W8_MONSTER_CONTROL_NONE ||
            monster_info->control_state >= W8_MONSTER_CONTROL_RESISTED) {
            continue;
        }
        if (monster_info->uiCondition[W8_CONDITION_BLIND] != 0 ||
            monster_info->highest_condition >= W8_CONDITION_ASLEEP) {
            SetMonsterControlState(monster_info, W8_MONSTER_CONTROL_NONE);
            continue;
        }
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = monster_info->location_id;
        if (!MonsterResistsSpellEffect(&target, effect->definition.power_level)) {
            if (CurrentGame().combat.has_value()) {
                SetMonsterControlState(monster_info, W8_MONSTER_CONTROL_LURED);
            } else {
                SetMonsterGroupControlState(FindMonsterGroup(monster_info->monster_group_id),
                                            W8_MONSTER_CONTROL_LURED);
            }
            effect->applied = true;
        } else {
            if (CurrentGame().combat.has_value()) {
                SetMonsterControlState(monster_info, W8_MONSTER_CONTROL_RESISTED);
            } else {
                SetMonsterGroupControlState(FindMonsterGroup(monster_info->monster_group_id),
                                            W8_MONSTER_CONTROL_RESISTED);
            }
        }
    }
}

/* The reveal-item effect on a character target: an ordinary condition-nine
   cure pass first, then the binding reveal. Only a character target produces
   bindings to report; a result of one or two appends the matching line and,
   on the partial answer, clears the applied flag. */
signed char RevealItemBindingsToTarget(W8SpellEffectEntry* effect)
{
    bool verbose;
    signed char applied; // bool-byte-ok: forwards the unnormalized byte-valued cure result
    int result;

    verbose = g_settings.verbose_combat_messages != 0;
    applied = TryCureConditionOnTargets(effect, W8_CONDITION_HEXED, false);
    if (effect->target.iType == W8_TARGET_KIND_CHARACTER) {
        result = RevealCharacterItemBindings(
            effect->target.iChar, effect->definition.duration_scale, effect->definition.percent);
        if (result == 0) {
            return 0;
        }
        if (result > 0 && result < 3) {
            if (!verbose) {
                effect->applied = true;
                AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
                CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            }
            if (result == 2) {
                AppendToLastTextLine(
                    fmt::sprintf(Data().strings[0x1bc],
                                 CurrentGame().party.characters[effect->target.iChar].name, -1),
                    -1);
                effect->reported = true;
                return applied;
            }
            AppendToLastTextLine(
                fmt::sprintf(Data().strings[0x1bd],
                             CurrentGame().party.characters[effect->target.iChar].name, -1),
                -1);
            applied = 0;
            effect->reported = true;
        }
    }
    return applied;
}

/* Charm the target monster: when the condition save fails, the rolled amount
   beats the stored effect strength and raises it. A charm that lands also
   gets its "<name> <effect>" line into the NPC quote bubble. A resisting or
   backfiring monster answers with a refusal quote instead. */
void ApplyCharmToMonsterTarget(W8SpellEffectEntry* effect)
{
    W8CombatSlot* target;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    unsigned int magnitude;
    std::string notice;
    std::string name;
    std::string formatted;

    target = &effect->target;
    assert(target->iType == W8_TARGET_KIND_MONSTER);
    assert(target->iMonsterID != -1);
    monster_info = FindMonster(target->iMonsterID);
    if (effect->Source.fBackfire) {
        monster = GetMonsterDataForInfo(monster_info);
        if (CurrentGame().rng.below(100) <
            (monster->attribute_values[W8_MONSTER_ATTRIBUTE_SENSES] +
             monster->attribute_values[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) /
                2) {
            CurrentGame().npc_dialogue.QueueQuote(0x16, false, false, false);
        }
        return;
    }
    if (TargetResistsCondition(target, W8_SPELL_REALM_MENTAL, effect->definition.power_level,
                               W8_CONDITION_NONE)) {
        AnnounceEffectResisted(target);
        return;
    }
    magnitude = effect->definition.duration_scale * 7;
    AdjustIntegerByPercent(&magnitude, effect->definition.percent);
    ReduceMagnitudeByResistance(&magnitude, target, W8_SPELL_REALM_MENTAL,
                                effect->definition.power_level);
    if (static_cast<signed char>(magnitude) <=
        static_cast<signed char>(monster_info->charm_strength)) {
        return;
    }
    if (monster_info->charm_strength == 0) {
        SetMonsterSpellIcon(monster_info->p3D, SPELL_ICON_CHARMED, true);
    }
    monster_info->charm_strength = static_cast<signed char>(magnitude);
    if (g_settings.verbose_combat_messages != 0) {
        PostMonsterNotice(monster_info, Data().strings[0x1ac]);
    } else {
        effect->applied = true;
    }
    notice = Data().strings[0x1ac];
    name = GetMonsterName(monster_info, 0, 0);
    formatted = fmt::format("{} {}", name, notice);
    DisplayNpcQuote(formatted, false);
}

/* The tame/refuse spell: a monster target that fails its resistance check
   answers with a refusal quote, and a backfired cast asks for one outright.
   Otherwise the dialogue NPC's staged refusal runs. */
void ResolveCharmRefusal(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster;
    W8SpellRealm realm;

    realm = Data().spells[effect->kind].realm;
    assert(TargetSourceIsCharacter(&effect->Source, 0));
    assert(effect->target.iType == W8_TARGET_KIND_MONSTER);
    monster_info = FindMonster(effect->target.iMonsterID);
    if (!effect->Source.fBackfire) {
        if (TargetResistsCondition(&effect->target, realm, effect->definition.power_level,
                                   W8_CONDITION_NONE)) {
            CurrentGame().npc_dialogue.QueueQuote(0x69, false, false, false);
            return;
        }
        QueueDialogueNpcRefusal();
    } else {
        monster = GetMonsterDataForInfo(monster_info);
        if (CurrentGame().rng.below(100) <
            (monster->attribute_values[W8_MONSTER_ATTRIBUTE_SENSES] +
             monster->attribute_values[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) /
                2) {
            CurrentGame().npc_dialogue.QueueQuote(0x16, false, false, false);
            return;
        }
    }
}

/* Weaken the running combat effects: each of the dispel-able spells in the
   first combat-effect block loses `argument` ticks, jittered and scaled the
   same way a fresh application would be, plus the level gap between the
   reference spell and the spell in the slot. A slot that runs out is
   dropped and rebuilt. */
void ReduceCombatEffectDurations(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info;
    W8EffectSlot* slot;
    unsigned int duration;
    int reduce;
    const int* table;
    int slot_index;
    int index;

    assert(CurrentGame().combat.has_value());
    duration = effect->definition.duration_scale;
    if (duration != W8_CONDITION_INDEFINITE) {
        ++duration;
        switch (CurrentGame().rng.below(4)) {
        case 0:
            if (duration > 1) {
                ++duration;
            }
            break;
        case 1:
            if (duration < 3) {
                ++duration;
            }
            break;
        }
        AdjustIntegerByPercent(&duration, effect->definition.percent);
    }
    slot_index = 0;
    for (table = g_combat_effect_slot_spells; table < g_combat_effect_slot_spells_and_cast_success;
         ++table, ++slot_index) {
        switch (*table) {
        case 0x30:
        case 0x4c:
        case 0x50:
        case 0x51:
        case 0x5d:
            reduce = Data().spells[W8_SPELL_PURIFY_AIR].spell_level -
                     Data().spells[*table].spell_level + duration;
            if (reduce > 0) {
                slot = &CurrentGame().combat->effect_slots[slot_index];
                if (!effect->target_indices.empty() && slot->active) {
                    if (static_cast<unsigned int>(reduce) < slot->duration) {
                        slot->duration -= reduce;
                    } else {
                        ResetPartyEffectBlock(slot);
                    }
                    effect->applied = true;
                }
                for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
                    monster_info = GetEffectMonsterTarget(effect->monster_ids[index], 0x3a7, 0x3a8);
                    slot = &monster_info->pCombat->combat_effects[slot_index];
                    if (slot->active) {
                        if (static_cast<unsigned int>(reduce) < slot->duration) {
                            slot->duration -= reduce;
                        } else {
                            ClearEffectSlot(monster_info, slot);
                        }
                        effect->applied = true;
                    }
                }
            }
        }
    }
}

/* Reflection handling at the end of a cast: when the spell can be aimed and
   the source was not already a bounce, the strongest reflection enchantment
   among the targets soaks the cast - the effect re-targets at a clamped
   power, the reflector is charged one use, and the notice goes out. */
void FinishSpellEffectTargets(W8SpellEffectEntry* effect)
{
    W8Character* character;
    W8MonsterInfo* monster_info;
    W8MonsterInfo* info;
    unsigned int best;
    unsigned int amount;
    W8TargetSource source_copy;
    W8CombatSlot target_copy;
    int index;

    character = 0;
    monster_info = 0;
    best = 0;
    if (MonsterCanAimSpell(effect->kind) && !effect->Source.aim_resolved &&
        !effect->Source.fBackfire && !effect->Source.fReflection) {
        for (index = 0; index < static_cast<int>(effect->target_indices.size()); ++index) {
            W8Character* member = &CurrentGame().party.characters[effect->target_indices[index]];
            if (member->highest_condition < W8_CONDITION_DEAD &&
                member->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].turns != 0 &&
                best < member->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power) {
                best = member->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power;
                character = member;
            }
        }
        for (index = 0; index < static_cast<int>(effect->monster_ids.size()); ++index) {
            info = FindMonster(effect->monster_ids[index], false);
            if (info == nullptr) {
                effect->monster_ids.erase(effect->monster_ids.begin() + index);
                --index;
                continue;
            }
            assert(info != 0);
            if (info->highest_condition < W8_CONDITION_DEAD &&
                info->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].turns != 0 &&
                best < info->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power) {
                character = 0;
                best = info->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power;
                monster_info = info;
            }
        }
        if (best != 0) {
            amount = effect->definition.duration_scale;
            if (best <= static_cast<unsigned int>(effect->definition.duration_scale)) {
                amount = best;
            }
            source_copy = effect->Source;
            effect->definition.duration_scale = amount;
            target_copy = effect->target;
            PrepareSpellTarget(effect->kind, &source_copy, &target_copy);
            /* The bounced cast carries the reflection flag so it cannot
               reflect a second time. */
            source_copy.fReflection = true;
            CastSpellFromSource(effect->kind, &source_copy, &target_copy,
                                effect->definition.duration_scale, effect->definition.percent, 0,
                                false, 0, W8SpellCastKind::Standard, 0, 0);
            if (character == 0) {
                if (monster_info != 0) {
                    PostMonsterNotice(monster_info, Data().strings[0x198]);
                    if (--monster_info->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power == 0) {
                        ClearMonsterEnchantmentSlot(monster_info->location_id,
                                                    W8_ENCHANTMENT_EYE_FOR_AN_EYE);
                    }
                }
            } else {
                CurrentGame().events.push(CharacterNotice{
                    static_cast<unsigned int>(CharacterPointerToPartySlot(character)),
                    Data().strings[0x198]});
                if (--character->enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].power == 0) {
                    Disenchant(CurrentGame(), CharacterPointerToPartySlot(character),
                               W8_ENCHANTMENT_EYE_FOR_AN_EYE);
                }
            }
        }
    }
}

/* Per-tick handler for the active 0x3e party combat effect: each live slot
   detonates a radius blast around the party - every monster in range takes a
   resistance-checked dice roll scaled by the slot's stored magnitude.  In
   verbose mode each damage line goes straight out; otherwise the results
   accumulate and print as one summary plus per-target condition reports. */
void TickRadiusBlastEffectSlots(std::span<const W8EffectSlot> effect_slots)
{
    std::vector<int> found;
    W8SpellEffectResult local_result;
    W8TargetSource source;
    W8CombatSlot target;
    W8Dice dice;
    W8MonsterInfo* monster_info;
    unsigned int amount;
    bool verbose;
    int index;

    verbose = g_settings.verbose_combat_messages != 0;
    ResetTargetSource(&source);
    local_result = {};
    target = {};
    for (const auto& slot : effect_slots) {
        if (slot.active && slot.effect_id == 0x3e) {
            CurrentGame().events.push(
                TextNotice{W8_FONT_PALETTE_WHITE,
                           fmt::sprintf(Data().strings[0x1ae],
                                        Data().spells[W8_SPELL_RING_OF_FIRE].display_name)});
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            {
                float reach = CalcRangeDistance(W8_RANGE_SHORT);
                float radius = PartyNavigator()->radius;
                glm::vec3 centre = PartyNavigator()->GetPosition();
                CollectMonstersWithinRadius(&centre, &centre, found, radius + reach, 1,
                                            W8AreaSightRule::RequireLineOfSight);
            }
            for (index = 0; index < static_cast<int>(found.size()); ++index) {
                monster_info = FindMonster(found[index]);
                assert(monster_info != 0);
                dice = Data().spells[W8_SPELL_RING_OF_FIRE].effect_dice;
                dice.count = static_cast<signed char>(slot.amount) * dice.count;
                amount = RollDice(&dice);
                target.iType = W8_TARGET_KIND_MONSTER;
                target.iMonsterID = monster_info->location_id;
                ApplyEffectAndAnnounce(&amount, &target, Data().spells[W8_SPELL_RING_OF_FIRE].realm,
                                       slot.amount * 3);
                if (amount != 0) {
                    ApplyDamageToMonster(monster_info, amount, &source, false, verbose, 0,
                                         !verbose ? &local_result : 0, false);
                }
            }
            if (!verbose) {
                if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
                    AppendToLastTextLine(" -- ", -1);
                    CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
                }
                if (local_result.count != 0) {
                    std::string line;
                    if (static_cast<int>(found.size()) == 1) {
                        line = fmt::sprintf(Data().strings[0x19a], local_result.amount, -1);
                    } else {
                        line = fmt::sprintf(Data().strings[0x199], local_result.count,
                                            local_result.amount / local_result.count, -1);
                    }
                    AppendToLastTextLine(line, -1);
                } else {
                    AppendToLastTextLine(Data().strings[0x1a5], -1);
                }
                while (!local_result.reports.empty()) {
                    auto report = std::move(local_result.reports.front());
                    local_result.reports.erase(local_result.reports.begin());
                    if (report != 0) {
                        if (report->kind == W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH) {
                            CurrentGame().events.push(CharacterNotice{
                                static_cast<unsigned int>(report->value),
                                fmt::format(
                                    "{}!",
                                    Data()
                                        .strings[kConditionNotices[W8_CONDITION_DEAD].singular])});
                        } else if (report->kind == W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH) {
                            CurrentGame().events.push(TextNotice{
                                W8_FONT_PALETTE_RUST,
                                fmt::format(
                                    "{} {}!", report->text,
                                    Data()
                                        .strings[kConditionNotices[W8_CONDITION_DEAD].singular])});
                        }
                    }
                }
            }
        }
    }
}

/* Per-tick upkeep for the first party combat-effect block (the nine slots at
   Combat+0x7c1).  Each live slot re-applies its spell to the block's
   target: the damage spells roll fresh dice every tick, the 0x5d sleep
   effect retries the condition instead, and 0x4c additionally rolls the
   shared 4/7/0x11 condition bundle on whatever survived the first checks. */
void TickCombatEffectSlots(W8EffectSlot* effect_slots, W8CombatSlot* target)
{
    W8TargetSource source;
    W8CombatSlot char_target;
    W8Dice dice;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* monster_record;
    unsigned int difficulty;
    unsigned int amount;
    unsigned int duration;
    unsigned int condition_turns;
    unsigned int resisted_total;
    unsigned int hit_count;
    int power;
    int percent;
    int duration_base;
    int duration_scale;
    int spell_id;
    int party_slot;
    int realm;
    int index;
    bool rolls_damage;
    bool sleep_type;
    bool verbose;

    verbose = g_settings.verbose_combat_messages != 0;
    ResetTargetSource(&source);
    index = 0;
    do {
        if (effect_slots->active) {
            spell_id = effect_slots->effect_id;
            power = effect_slots->amount;
            percent = effect_slots->percent;
            duration_base = Data().spells[spell_id].duration_per_level;
            duration_scale = Data().spells[spell_id].duration;
            party_slot = 0;
            difficulty = GetSpellDifficulty(0, spell_id, power);
            AdjustIntegerByPercent(&difficulty, static_cast<unsigned int>(percent) >> 1);
            switch (spell_id) {
            case W8_SPELL_ACID_BOMB:
            case W8_SPELL_TOXIC_CLOUD:
            case W8_SPELL_DRAINING_CLOUD:
            case W8_SPELL_FIRESTORM:
                dice = Data().spells[spell_id].effect_dice;
                dice.count = static_cast<signed char>(power) * dice.count;
                rolls_damage = true;
                sleep_type = false;
                break;
            case W8_SPELL_DEATH_CLOUD:
                rolls_damage = false;
                sleep_type = true;
                break;
            default:
                goto next_slot;
            }
            if (target->iType == W8_TARGET_KIND_PARTY) {
                hit_count = 0;
                resisted_total = 0;
                if (verbose && !sleep_type) {
                    CurrentGame().events.push(TextNotice{
                        W8_FONT_PALETTE_BEIGE,
                        fmt::sprintf(Data().strings[0x1ae], Data().spells[spell_id].display_name)});
                }
                do {
                    if (CurrentGame().party.members[party_slot].has_value() &&
                        CurrentGame().party.characters[party_slot].highest_condition <
                            W8_CONDITION_DEAD) {
                        char_target.iType = W8_TARGET_KIND_CHARACTER;
                        char_target.iChar = party_slot;
                        if (rolls_damage) {
                            amount = RollDice(&dice);
                            AdjustIntegerByPercent(&amount, percent);
                            ApplyEffectAndAnnounce(&amount, &char_target,
                                                   Data().spells[spell_id].realm, difficulty);
                            if (amount != 0) {
                                ApplyDamageToCharacter(party_slot, amount, false, true, false, 0,
                                                       false);
                                resisted_total += amount;
                                ++hit_count;
                                if (spell_id == W8_SPELL_DRAINING_CLOUD) {
                                    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
                                        DrainCharacterRealmSpellPoints(
                                            party_slot, static_cast<W8SpellRealm>(realm), amount,
                                            true);
                                    }
                                    FatigueCharacter(party_slot, amount, false, 0);
                                }
                            }
                        }
                        if (sleep_type &&
                            !TargetResistsCondition(&char_target, Data().spells[spell_id].realm,
                                                    difficulty, W8_CONDITION_DEAD)) {
                            InflictCondition(CurrentGame(), party_slot,
                                             {.condition = W8_CONDITION_DEAD,
                                              .duration = W8_CONDITION_INDEFINITE});
                        }
                        if (spell_id == W8_SPELL_TOXIC_CLOUD) {
                            duration =
                                RollEffectDuration(duration_scale, power, duration_base, percent);
                            condition_turns = duration;
                            ReduceMagnitudeByResistance(&condition_turns, target,
                                                        Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                                        difficulty);
                            if (condition_turns != 0) {
                                if (condition_turns == 1) {
                                    condition_turns = 2;
                                }
                                if (!TargetResistsCondition(
                                        &char_target, Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                        difficulty, W8_CONDITION_NAUSEATED)) {
                                    InflictCondition(
                                        CurrentGame(), party_slot,
                                        {.condition = W8_CONDITION_NAUSEATED,
                                         .duration = static_cast<int>(condition_turns)});
                                }
                                if (!TargetResistsCondition(
                                        &char_target, Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                        difficulty, W8_CONDITION_POISONED)) {
                                    InflictCondition(
                                        CurrentGame(), party_slot,
                                        {.condition = W8_CONDITION_POISONED,
                                         .duration = static_cast<int>(condition_turns),
                                         .poison_strength = static_cast<int>(condition_turns)});
                                }
                                if (!TargetResistsCondition(
                                        &char_target, Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                        difficulty, W8_CONDITION_UNCONSCIOUS)) {
                                    InflictCondition(
                                        CurrentGame(), party_slot,
                                        {.condition = W8_CONDITION_UNCONSCIOUS,
                                         .duration = static_cast<int>(condition_turns)});
                                }
                            }
                        }
                    }
                    ++party_slot;
                } while (party_slot < 8);
                if (!verbose && hit_count != 0) {
                    CurrentGame().events.push(TextNotice{
                        W8_FONT_PALETTE_BEIGE,
                        fmt::sprintf(Data().strings[0x1af], Data().spells[spell_id].display_name,
                                     hit_count, resisted_total / hit_count)});
                }
            } else if (target->iType == W8_TARGET_KIND_MONSTER) {
                monster_info = FindMonster(target->iMonsterID);
                if (rolls_damage) {
                    amount = RollDice(&dice);
                    AdjustIntegerByPercent(&amount, percent);
                    ApplyEffectAndAnnounce(&amount, target, Data().spells[spell_id].realm,
                                           difficulty);
                    if (amount != 0) {
                        ApplyDamageToMonster(monster_info, amount, &source, false, 1, 0, 0, false);
                        if (spell_id == W8_SPELL_DRAINING_CLOUD) {
                            FatigueMonster(monster_info, amount, 0);
                            monster_info->spell_points = monster_info->spell_points - (amount >> 1);
                        }
                    }
                }
                monster_record = GetMonsterDataForInfo(monster_info);
                if (sleep_type && monster_record->instant_death_immune == 0 &&
                    !TargetResistsCondition(target, Data().spells[W8_SPELL_DEATH_CLOUD].realm,
                                            difficulty, W8_CONDITION_DEAD)) {
                    SetMonsterCondition(target->iMonsterID, W8_CONDITION_DEAD,
                                        W8_CONDITION_INDEFINITE, 0, &source, true);
                }
                if (monster_info->hp_current == 0) {
                    return;
                }
                if (spell_id == W8_SPELL_TOXIC_CLOUD) {
                    duration = RollEffectDuration(duration_scale, power, duration_base, percent);
                    condition_turns = duration;
                    ReduceMagnitudeByResistance(&condition_turns, target,
                                                Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                                difficulty);
                    if (condition_turns != 0) {
                        if (condition_turns == 1) {
                            condition_turns = 2;
                        }
                        if (!TargetResistsCondition(target,
                                                    Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                                    difficulty, W8_CONDITION_NAUSEATED)) {
                            SetMonsterCondition(target->iMonsterID, W8_CONDITION_NAUSEATED,
                                                condition_turns, 0, &source, true);
                        }
                        if (!TargetResistsCondition(target,
                                                    Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                                    difficulty, W8_CONDITION_POISONED)) {
                            SetMonsterCondition(target->iMonsterID, W8_CONDITION_POISONED,
                                                condition_turns, condition_turns, &source, true);
                        }
                        if (!TargetResistsCondition(target,
                                                    Data().spells[W8_SPELL_TOXIC_CLOUD].realm,
                                                    difficulty, W8_CONDITION_UNCONSCIOUS)) {
                            SetMonsterCondition(target->iMonsterID, W8_CONDITION_UNCONSCIOUS,
                                                condition_turns, 0, &source, true);
                        }
                    }
                }
            }
        }
    next_slot:
        ++index;
        ++effect_slots;
        if (8 < index) {
            return;
        }
    } while (true);
}

/* The per-frame spell effect dispatcher. A queued effect that needed aiming
   and was never armed is dropped up front; everything else routes on the
   spell id. Backfired effects invert the helpful spells - cures and buffs
   become attacks on the caster's side. After the dispatch the unreported
   effect gets its summary line, the caster is charged (repeatedly, while
   stamina and realm spell points hold out), and the reflection pass runs. */
void ProcessSpellEffectTargets(W8SpellEffectEntry* effect)
{
    CameraPose* camera_state;
    World* world;
    glm::vec3 point;
    std::string sound_name;
    std::string path;
    unsigned char
        applied; // bool-byte-ok: bitwise intersection of byte-valued cure results; normalization changes values
    unsigned int level;
    unsigned int amount;
    int count;
    unsigned int percent_roll;
    int queued_spell_id;
    int spell_id;
    int condition;
    W8SpellTargetType target_type;
    int index;
    int cost;
    W8Character* character;
    W8ItemInstance* item;
    W8EffectSlot* slot;

    spell_id = effect->kind;
    applied = 1;
    queued_spell_id = spell_id;
    if (Data().spells[spell_id].needs_aim != 0 && !IsCombatEffectSlotSpell(spell_id) &&
        (target_type = GetSpellTargetType(spell_id, false)) != W8_TARGET_TYPE_RADIUS &&
        target_type != W8_TARGET_TYPE_PARTY) {
        return;
    }
    if (effect->Source.auto_cast && spell_id != W8_SPELL_MAKE_WOUNDS) {
        effect->reported = true;
    }
    switch (spell_id) {
    case W8_SPELL_ACID_SPLASH:
    case W8_SPELL_ENERGY_BLAST:
    case W8_SPELL_MIND_STAB:
    case W8_SPELL_PARALYZE:
    case W8_SPELL_MAGIC_MISSILES:
    case W8_SPELL_BOILING_BLOOD:
        return;
    case W8_SPELL_BLESS:
    case W8_SPELL_ELEMENT_SHIELD:
    case W8_SPELL_SOUL_SHIELD:
    case W8_SPELL_RING_OF_FIRE:
        ApplyDefenseEffectSlot(effect);
        break;
    case W8_SPELL_CHARM:
        ApplyCharmToMonsterTarget(effect);
        break;
    case W8_SPELL_HEAL_WOUNDS:
    case W8_SPELL_HEAL_ALL:
        if (!effect->Source.fBackfire) {
            applied &= HealTargets(effect);
        } else {
            ApplyDamageToTargets(effect);
        }
        break;
    case W8_SPELL_ITCHING_SKIN:
        InflictConditionAttack(effect, W8_CONDITION_IRRITATED, 100, 0);
        break;
    case W8_SPELL_LIGHT:
    case W8_SPELL_DETECT_SECRETS:
    case W8_SPELL_ENCHANTED_BLADE:
    case W8_SPELL_MISSILE_SHIELD:
    case W8_SPELL_ARMORPLATE:
    case W8_SPELL_CHAMELEON:
    case W8_SPELL_MAGIC_SCREEN:
    case W8_SPELL_SHADOW_HOUND:
    case W8_SPELL_X_RAY:
        ApplyBeingEffectSlot(effect);
        break;
    case W8_SPELL_SLEEP:
        InflictConditionAttack(effect, W8_CONDITION_ASLEEP, 100, 0);
        break;
    case W8_SPELL_STAMINA:
    case W8_SPELL_REST_ALL:
        if (!effect->Source.fBackfire) {
            applied &= RestoreTargetsStamina(effect);
        } else {
            FatigueTargets(effect);
        }
        break;
    case W8_SPELL_TERROR:
    case W8_SPELL_ROUT:
        InflictConditionAttack(effect, W8_CONDITION_AFRAID, 100, 0);
        break;
    case W8_SPELL_BLINDING_FLASH:
        InflictConditionAttack(effect, W8_CONDITION_BLIND, 100, 0);
        break;
    case W8_SPELL_CURE_LESSER_COND:
        if (!effect->Source.fBackfire) {
            /* Retail tries the cures in this order; each one rolls and
               reports, so the sequence is spelled out. */
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_IRRITATED, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_NAUSEATED, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_AFRAID, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_ASLEEP, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_BLIND, false);
        } else {
            switch (CurrentGame().rng.below(5)) {
            case W8_SPELL_NONE:
                InflictConditionAttack(effect, W8_CONDITION_IRRITATED, 100, 0);
                break;
            case W8_SPELL_ACID_SPLASH:
                InflictConditionAttack(effect, W8_CONDITION_NAUSEATED, 100, 0);
                break;
            case W8_SPELL_BLESS:
                InflictConditionAttack(effect, W8_CONDITION_AFRAID, 100, 0);
                break;
            case W8_SPELL_CHARM:
                InflictConditionAttack(effect, W8_CONDITION_ASLEEP, 100, 0);
                break;
            default:
                InflictConditionAttack(effect, W8_CONDITION_BLIND, 100, 0);
                break;
            }
        }
        break;
    case W8_SPELL_DIVINE_TRAP:
        level = effect->definition.duration_scale;
        AdjustIntegerByPercent(&level, effect->definition.percent);
        if (6 < level) {
            level = 7;
        }
        SetKnockKnockTarget(level, effect->Source.name_known, effect->Source.fBackfire);
        effect->reported = true;
        break;
    case W8_SPELL_DRACON_BREATH:
    case W8_SPELL_GUARDIAN_ANGEL:
    case W8_SPELL_RAZOR_CLOAK:
    case W8_SPELL_EYE_FOR_AN_EYE:
    case W8_SPELL_HASTE:
    case W8_SPELL_SUPERMAN:
    case W8_SPELL_BODY_OF_STONE:
        if (!effect->Source.fBackfire) {
            W8EnchantmentSlot enchantment;
            switch (spell_id) {
            case W8_SPELL_DRACON_BREATH:
                enchantment = W8_ENCHANTMENT_DRACON_BREATH;
                break;
            default:
                enchantment = W8_ENCHANTMENT_NONE;
                break;
            case W8_SPELL_GUARDIAN_ANGEL:
                enchantment = W8_ENCHANTMENT_GUARDIAN_ANGEL;
                break;
            case W8_SPELL_RAZOR_CLOAK:
                enchantment = W8_ENCHANTMENT_RAZOR_CLOAK;
                break;
            case W8_SPELL_EYE_FOR_AN_EYE:
                enchantment = W8_ENCHANTMENT_EYE_FOR_AN_EYE;
                break;
            case W8_SPELL_HASTE:
                enchantment = W8_ENCHANTMENT_HASTE;
                break;
            case W8_SPELL_SUPERMAN:
                enchantment = W8_ENCHANTMENT_SUPERMAN;
                break;
            case W8_SPELL_BODY_OF_STONE:
                enchantment = W8_ENCHANTMENT_BODY_OF_STONE;
                break;
            }
            ApplyConditionToTargets(effect, enchantment);
            effect->applied = true;
        } else {
            if (effect->kind == W8_SPELL_DRACON_BREATH) {
                InflictConditionAttack(effect, W8_CONDITION_NAUSEATED, 100, 0);
            }
            if (effect->kind == W8_SPELL_RAZOR_CLOAK) {
                ApplyDamageToTargets(effect);
            }
            if (effect->kind == W8_SPELL_HASTE) {
                InflictConditionAttack(effect, W8_CONDITION_SLOWED, 100, 0);
            }
        }
        break;
    case W8_SPELL_IDENTIFY_ITEM:
        assert(TargetSourceIsCharacter(&effect->Source, 0));
        ApplyIdentifyAttempt(CurrentGame().party.itemAt(effect->target.item),
                             effect->definition.duration_scale, effect->definition.percent);
        item = CurrentGame().party.itemAt(effect->target.item);
        if (item->identified && item->bound) {
            effect->applied = true;
        }
        break;
    case W8_SPELL_INSANITY:
        InflictConditionAttack(effect, W8_CONDITION_INSANE, 100, 0);
        break;
    case W8_SPELL_SLOW:
        InflictConditionAttack(effect, W8_CONDITION_SLOWED, 100, 0);
        break;
    case W8_SPELL_SONIC_BOOM:
    case W8_SPELL_ALARM:
        InflictConditionAttack(effect, W8_CONDITION_AFRAID, 100, 0);
        AlertMonsterGroupsToNoise(&effect->target.point,
                                  effect->definition.duration_scale * 15000 + 25000, 0);
        if (!CurrentGame().combat.has_value()) {
            effect->applied = true;
        }
        break;
    case W8_SPELL_WEB:
        InflictConditionAttack(effect, W8_CONDITION_WEBBED, 100, 0);
        break;
    case W8_SPELL_CURE_PARALYSIS:
        if (!effect->Source.fBackfire) {
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_PARALYZED, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_WEBBED, false);
        } else {
            InflictConditionAttack(effect, W8_CONDITION_PARALYZED, 100, 0);
        }
        break;
    case W8_SPELL_CURE_POISON:
        if (!effect->Source.fBackfire) {
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_POISONED, false);
        } else {
            int turns = GetTargetConditionTurns(effect, W8_CONDITION_POISONED, &condition);
            unsigned int roll = CurrentGame().rng.below(effect->definition.duration_scale);
            effect->definition.duration_base = roll + 1 + turns;
            roll = CurrentGame().rng.below(effect->definition.duration_scale / 3);
            InflictConditionAttack(effect, W8_CONDITION_POISONED, 100, roll + 1 + condition);
        }
        break;
    case W8_SPELL_FREEZE_FLESH:
    case W8_SPELL_FREEZE_ALL:
        InflictConditionAttack(effect, W8_CONDITION_PARALYZED, 100, 0);
        break;
    case W8_SPELL_HYPNOTIC_LURE:
        ApplyMonsterControlToNearbyMonsters(effect);
        break;
    case W8_SPELL_KNOCK_KNOCK:
        level = effect->definition.duration_scale;
        AdjustIntegerByPercent(&level, effect->definition.percent);
        if (6 < level) {
            level = 7;
        }
        CastSpellAtLockInteraction(level, effect->Source.name_known, effect->Source.fBackfire);
        effect->reported = true;
        break;
    case W8_SPELL_MINDREAD:
        ResolveCharmRefusal(effect);
        effect->reported = true;
        break;
    case W8_SPELL_NOXIOUS_FUMES:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_NAUSEATED, 0x5a, 0);
        InflictConditionAttack(effect, W8_CONDITION_UNCONSCIOUS, 0x19, 0);
        break;
    case W8_SPELL_SILENCE:
        InflictConditionAttack(effect, W8_CONDITION_SILENCED, 100, 0);
        break;
    case W8_SPELL_ACID_BOMB:
    case W8_SPELL_ARMORMELT:
    case W8_SPELL_TOXIC_CLOUD:
    case W8_SPELL_DRAINING_CLOUD:
    case W8_SPELL_FIRESTORM:
    case W8_SPELL_DEATH_CLOUD:
        ApplyCombatEffectSlot(effect);
        break;
    case W8_SPELL_CURE_DISEASE:
        applied &= TryCureConditionOnTargets(effect, W8_CONDITION_DISEASED, false);
        break;
    case W8_SPELL_REMOVE_CURSE:
        if (effect->Source.fBackfire) {
            InflictConditionAttack(effect, W8_CONDITION_HEXED, 100, 0);
        } else {
            RevealItemBindingsToTarget(effect);
        }
        break;
    case W8_SPELL_SUMMON_ELEMENTAL:
        ApplySummonElementalEffect(effect);
        break;
    case W8_SPELL_HEX:
        InflictConditionAttack(effect, W8_CONDITION_HEXED, 100, 0);
        break;
    case W8_SPELL_INSTANT_DEATH:
    case W8_SPELL_QUICKSAND:
    case W8_SPELL_ASPHYXIATION:
    case W8_SPELL_DEATH_WISH:
        InflictConditionAttack(effect, W8_CONDITION_DEAD, 100, 0);
        break;
    case W8_SPELL_PSIONIC_BLAST:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_INSANE, 0x19, 0);
        break;
    case W8_SPELL_PURIFY_AIR:
        ReduceCombatEffectDurations(effect);
        effect->applied = true;
        break;
    case W8_SPELL_RETURN_TO_PORTAL:
        RecallCasterToSavedLocation(effect);
        effect->applied = true;
        break;
    case W8_SPELL_SANE_MIND:
        if (!effect->Source.fBackfire) {
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_INSANE, false);
            applied &= TryCureConditionOnTargets(effect, W8_CONDITION_TURNCOAT, false);
        } else {
            InflictConditionAttack(effect, W8_CONDITION_INSANE, 100, 0);
        }
        break;
    case W8_SPELL_SET_PORTAL:
        assert(TargetSourceIsCharacter(&effect->Source, 0));
        camera_state = &CurrentGame().party.characters[effect->Source.iChar].saved_location;
        world = GetWorld();
        *camera_state = cameraState(*world);
        CurrentGame().party.characters[effect->Source.iChar].saved_level = GetLoadedLevelID();
        CurrentGame().party.characters[effect->Source.iChar].has_saved_location = true;
        effect->applied = true;
        break;
    case W8_SPELL_BLIZZARD:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_BLIND, 0x19, 0);
        break;
    case W8_SPELL_LIFESTEAL:
    case W8_SPELL_MIGHT_TO_MAGIC:
        DrainTargetsLife(effect);
        break;
    case W8_SPELL_PANDEMONIUM:
        AttackThenInflictCondition(effect, W8_CONDITION_INSANE, W8_CONDITION_AFRAID, 100, 0, 0);
        break;
    case W8_SPELL_PRISMIC_RAY:
    case W8_SPELL_PRISMIC_CHAOS:
        ResolveAfflictionAgainstTargets(effect);
        effect->reported = true;
        break;
    case W8_SPELL_RESURRECTION:
        applied &= TryCureConditionOnTargets(effect, W8_CONDITION_DEAD, false);
        break;
    case W8_SPELL_TURNCOAT:
        InflictConditionAttack(effect, W8_CONDITION_TURNCOAT, 100, 0);
        break;
    case W8_SPELL_CEREBRAL_HEMORRHAGE:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_INSANE, 0x4b, 0);
        break;
    case W8_SPELL_CONCUSSION:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_UNCONSCIOUS, 0x32, 0);
        break;
    case W8_SPELL_EARTHQUAKE:
        point = effect->Source.point;
        level = effect->definition.duration_scale;
        {
            ShakeDefinition shake(level * g_navigator_vertical_phase_step + 0.5f, true,
                                  level * g_navigator_snap_angle + kCameraShakeIntensityBase,
                                  50000.0f, &point);
            shake.quadratic_falloff = false;
            shake.fade_out = false;
            CameraShakes().Start(shake);
        }
        sound_name = Data().spells[spell_id].sound_name;
        if (sound_name[0] != 0) {
            path = fmt::format("Data\\Spells\\Sounds\\{}.wav", sound_name);
            wiz8::sound::play(path);
        }
        [[fallthrough]];
    case W8_SPELL_FROST:
    case W8_SPELL_MAKE_WOUNDS:
    case W8_SPELL_HOLY_WATER:
    case W8_SPELL_SHRILL_SOUND:
    case W8_SPELL_FIREBALL:
    case W8_SPELL_PSIONIC_FIRE:
    case W8_SPELL_WHIPPING_ROCKS:
    case W8_SPELL_CRUSH:
    case W8_SPELL_EGO_WHIP:
    case W8_SPELL_FIRE_BOMB:
    case W8_SPELL_ICEBALL:
    case W8_SPELL_WHIRLWIND:
    case W8_SPELL_DEHYDRATE:
    case W8_SPELL_BANISH:
    case W8_SPELL_LIGHTNING:
    case W8_SPELL_FALLING_STARS:
    case W8_SPELL_MIND_FLAY:
    case W8_SPELL_NUCLEAR_BLAST:
    case W8_SPELL_TSUNAMI:
    case W8_SPELL_BOILING_BLOOD_EXPLOSION:
        ApplyDamageToTargets(effect);
        break;
    case W8_SPELL_RESTORATION:
        applied &= HealTargets(effect);
        applied &= RestoreTargetsStamina(effect);
        for (condition = 2; condition < 0x12; ++condition) {
            if (condition != W8_CONDITION_INFATUATED) {
                applied &=
                    TryCureConditionOnTargets(effect, static_cast<W8Condition>(condition), true);
            }
        }
        break;
    case W8_SPELL_RESTORE_HEALTH:
        applied &= HealTargets(effect);
        effect->definition.magnitude.count <<= 1;
        applied &= RestoreTargetsStamina(effect);
        effect->reported = true;
        break;
    case W8_SPELL_RESTORE_MAGIC:
        count = static_cast<int>(effect->target_indices.size());
        if (0 < count) {
            for (index = 0; index < count; ++index) {
                int iChar = effect->target_indices[index];
                assert(iChar != -1);
                amount = RollEffectMagnitude(&effect->definition);
                RestoreCharacterSpellPointsEvenly(iChar, amount);
            }
        }
        effect->applied = true;
        break;
    case W8_SPELL_SMELLING_SALTS:
        applied &= TryCureConditionOnTargets(effect, W8_CONDITION_UNCONSCIOUS, false);
        applied &= TryCureConditionOnTargets(effect, W8_CONDITION_ASLEEP, false);
        break;
    case W8_SPELL_RENEWAL:
        TryCureConditionOnTargets(effect, W8_CONDITION_DRAINED, true);
        break;
    case W8_SPELL_PHILOSOPHERS_BANE:
        if (0 < static_cast<int>(effect->target_indices.size())) {
            percent_roll = RollDice(&effect->definition.magnitude);
            if (percent_roll < 100) {
                percent_roll = RollDice(&effect->definition.magnitude);
            } else {
                percent_roll = 100;
            }
            CurrentGame().party.gold =
                CurrentGame().party.gold - (percent_roll * CurrentGame().party.gold) / 100;
            if (g_settings.verbose_combat_messages == 0 &&
                GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
                AppendToLastTextLine(" -- ", -1);
                CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
            }
            if (percent_roll < 100) {
                AppendToLastTextLine(Data().strings[0x1b5], -1);
                effect->reported = true;
            } else {
                AppendToLastTextLine(Data().strings[0x1b6], -1);
                effect->reported = true;
            }
        }
        break;
    case W8_SPELL_VAMPIRIC_VORTEX:
        DamageTargetsAndReport(effect);
        break;
    case W8_SPELL_HEATWAVE:
        DestroyConsumablesOnTargets(effect);
        break;
    case W8_SPELL_ANTI_MAGIC:
        amount = effect->definition.duration_scale * 0x32;
        AdjustIntegerByPercent(&amount, effect->definition.percent);
        if (0 < static_cast<int>(effect->target_indices.size())) {
            DrainPartySpellPoints(amount, false);
            slot = CurrentGame().status.effect_slots.data();
            do {
                if (slot->active) {
                    ResetPartyEffectBlock(slot);
                }
                ++slot;
            } while (slot < CurrentGame().status.effect_slots.data() + 12);
        }
        effect->reported = true;
        break;
    case W8_SPELL_DISPEL_UNDEAD:
        ApplyDamageToTargets(effect);
        InflictConditionAttack(effect, W8_CONDITION_PARALYZED, 0x32, 0);
        break;
    case W8_SPELL_BOOZE:
        ApplyRandomAfflictionToTarget(effect);
        break;
    case W8_SPELL_ROCKET_BLAST:
        GetFact(W8_FACT_UMISSION_TRAIN_FIRE_ASSIGN);
        ApplyDamageToTargets(effect);
        break;
    default:
        CurrentGame().events.push(
            TextNotice{W8_FONT_PALETTE_BROWN, fmt::format("{} - spell effect not implemented",
                                                          Data().spells[spell_id].display_name)});
        break;
    }
    if (g_settings.verbose_combat_messages == 0 && !effect->reported && !effect->Source.fBackfire) {
        if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
            AppendToLastTextLine(" -- ", -1);
            CurrentGame().events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        AppendToLastTextLine(effect->applied ? Data().strings[0x1b0] : Data().strings[0x1a5], -1);
        effect->reported = true;
    }
    if (effect->recast && applied == 0) {
        if (effect->Source.auto_cast) {
            CastSpellFromSource(spell_id, &effect->Source, &effect->target,
                                effect->definition.duration_scale, 0, 0, true, &cost,
                                W8SpellCastKind::Standard, 0, 0);
        } else if (TargetSourceIsCharacter(&effect->Source, 0)) {
            character = &CurrentGame().party.characters[effect->Source.iChar];
            cost = SpellCastFatigueCost(spell_id, effect->definition.duration_scale);
            FatigueCharacter(effect->Source.iChar, cost, true, 0);
            while (SpellCastFatigueCost(spell_id, 1) <= character->stamina) {
                if (character->iSPLeft[Data().spells[spell_id].realm] <
                        Data().spells[spell_id].spell_point_cost ||
                    ExecuteCharacterSpellCast(effect->Source.iChar, spell_id, 8, &cost, true) !=
                        2) {
                    break;
                }
                FatigueCharacter(effect->Source.iChar, cost, true, 0);
            }
        }
    }
    FinishSpellEffectTargets(effect);
    if (queued_spell_id != W8_SPELL_IDENTIFY_ITEM && CurrentScreenState().id == W8_SCREEN_CAMP) {
        RefreshTextBoxMode(0xffff);
    }
}

static void QueueCombatDamageReports(W8SpellEffectResult* result)
{
    auto& queued = CurrentGame().combat->attack_report.reports;
    queued.insert(queued.end(), std::make_move_iterator(result->reports.begin()),
                  std::make_move_iterator(result->reports.end()));
    result->reports.clear();
}

/* Damage from the target-side enchantment: the enchantment's power scales the
   spell record's dice, the reduced roll is applied to the character, the
   result's amount feeds the running combat total at +0xa1a, and the reports
   queue on the combat state for the message pass. */
void ApplyDiceDamageToCharacter(int party_slot, W8TargetSource* source, W8Enchantment* enchantment)
{
    bool verbose = g_settings.verbose_combat_messages != 0;
    W8SpellEffectResult result;
    W8Dice dice;
    unsigned int amount;

    if (enchantment->power == 0) {
        return;
    }
    result = {};
    dice = Data().spells[W8_SPELL_RAZOR_CLOAK].effect_dice;
    dice.count = static_cast<unsigned char>(enchantment->power) * dice.count;
    amount =
        ApplyCharacterDamageReduction(&CurrentGame().party.characters[party_slot], RollDice(&dice));
    if (amount > 0) {
        ApplyDamageToCharacter(party_slot, amount, false, verbose, verbose, &result, false);
        CurrentGame().combat->attack_report.notice_values[3] += result.amount;
        QueueCombatDamageReports(&result);
    }
}

/* The monster-side counterpart: the same enchantment-scaled dice roll feeds
   ApplyDamageToMonster and the running total, with no report queueing. */
void ApplyDiceDamageToMonster(W8MonsterInfo* monster_info, W8TargetSource* source,
                              W8Enchantment* enchantment)
{
    bool verbose = g_settings.verbose_combat_messages != 0;
    const W8MonsterRecord* record;
    W8Dice dice;
    int damage;
    unsigned int amount;

    if (enchantment->power == 0) {
        return;
    }
    dice = Data().spells[W8_SPELL_RAZOR_CLOAK].effect_dice;
    dice.count = static_cast<unsigned char>(enchantment->power) * dice.count;
    damage = RollDice(&dice);
    record = GetMonsterDataForInfo(monster_info);
    amount = ApplyDamageReduction(monster_info, record, damage);
    if (amount > 0) {
        ApplyDamageToMonster(monster_info, amount, source, false, verbose, verbose, 0, false);
        CurrentGame().combat->attack_report.notice_values[3] += amount;
    }
}

/* Flat-amount damage to a character: when the combat log is quiet the applied
   amount feeds the running total at +0xa1e and the reports queue up; when it
   is verbose the damage is applied with the announced flags instead. */
void ApplyDirectDamageToCharacter(int party_slot, W8TargetSource* source, int damage)
{
    bool verbose = g_settings.verbose_combat_messages != 0;
    W8SpellEffectResult result;
    unsigned int amount;

    amount = ApplyCharacterDamageReduction(&CurrentGame().party.characters[party_slot], damage);
    if (amount > 0) {
        if (verbose) {
            ApplyDamageToCharacter(party_slot, amount, false, true, true, 0, true);
        } else {
            amount =
                ApplyDamageToCharacter(party_slot, amount, false, false, false, &result, false);
            CurrentGame().combat->attack_report.notice_values[4] += amount;
            QueueCombatDamageReports(&result);
        }
    }
}

/* The monster-side counterpart: the flat amount reduced by the monster's own
   reduction is applied, feeding the running total and report queue in quiet
   mode or the announced apply in verbose mode. */
void ApplyDirectDamageToMonster(W8MonsterInfo* monster_info, W8TargetSource* source, int damage)
{
    bool verbose = g_settings.verbose_combat_messages != 0;
    W8SpellEffectResult result;
    const W8MonsterRecord* record;
    unsigned int amount;

    record = GetMonsterDataForInfo(monster_info);
    amount = ApplyDamageReduction(monster_info, record, damage);
    if (amount > 0) {
        if (verbose) {
            ApplyDamageToMonster(monster_info, amount, source, false, 1, 1, 0, true);
        } else {
            amount =
                ApplyDamageToMonster(monster_info, amount, source, false, 0, 0, &result, false);
            CurrentGame().combat->attack_report.notice_values[4] += amount;
            QueueCombatDamageReports(&result);
        }
    }
}

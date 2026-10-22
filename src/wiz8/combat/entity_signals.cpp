#include "wiz8/combat/entity_signals.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <string>

#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_actions.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"

namespace {
// The missile table row of a stored-spell missile, which casts its spell where it lands.
constexpr int kStoredSpellMissile = 0x23;

/* Cast Rocket Blast where a stored-spell missile landed, on behalf of the character
   that fired it. */
void DetonateStoredSpell(Missile& missile)
{
    W8TargetSource source;
    W8CombatSlot target;

    assert(missile.m_Source.iType == W8_TARGET_SOURCE_CHARACTER);
    const glm::vec3 position = missile.nav.GetPosition();
    ResetTargetSource(&source);
    source.iChar = missile.m_Source.iChar;
    source.iType = W8_TARGET_SOURCE_CHARACTER;
    source.point = position;
    source.aim_resolved = true;
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    target.point = position;
    CastSpellFromSource(W8_SPELL_ROCKET_BLAST, &source, &target, 1, 0, 0, false, 0,
                        W8SpellCastKind::Standard, 0, 0);
}

} // namespace

/* Decide what the missile struck - the party or a live monster - record it in its
   combat slot, roll the target's missile deflection and either resolve the hit or
   leave it for the combat engine. */
bool CombatSignals::missileReached(Missile& missile, Navigator& struck)
{
    const World& world = *missile.owner_world;
    W8MissileHitResult hit_result;
    unsigned char deflect_chance;

    if (PartyNavigator() == &struck) {
        if (TargetSourceIsCharacter(&missile.m_Source, 0)) {
            return false;
        }
        if (missile.combat_slot.iType != W8_TARGET_KIND_PARTY &&
            missile.combat_slot.iType != W8_TARGET_KIND_CHARACTER) {
            if (game_.data.missiles[missile.missile_table_index].spell_missile) {
                return false;
            }
            if (missile.combat_slot.iType != W8_TARGET_KIND_NONE) {
                missile.retargeted = true;
            }
            missile.combat_slot.iType = W8_TARGET_KIND_CHARACTER;
            missile.combat_slot.iChar = GetRandomCharacter(1, 1, -1, -1);
            missile.combat_slot.iMonsterID = -1;
            announceCollisionTarget(world, missile);
        }
    } else {
        if (struck.movement.location_id <= 0) {
            return false;
        }
        Monster* monster = struck.monster_owner;
        if (!monster) return false;
        int location_id = monster->location_id;
        W8MonsterInfo* monster_info = world.population->findMonster(location_id);
        if (TargetSourceIsMonster(&missile.m_Source, 0) &&
            missile.m_Source.iMonsterID == location_id) {
            return false;
        }
        if (monster_info->hp_current == 0) {
            return false;
        }
        if (missile.combat_slot.iType != W8_TARGET_KIND_MONSTER ||
            missile.combat_slot.iMonsterID != location_id) {
            if (game_.data.missiles[missile.missile_table_index].spell_missile) {
                return false;
            }
            if (missile.combat_slot.iType != W8_TARGET_KIND_NONE) {
                missile.retargeted = true;
            }
            missile.combat_slot.iType = W8_TARGET_KIND_MONSTER;
            missile.combat_slot.iChar = -1;
            missile.combat_slot.iMonsterID = location_id;
            announceCollisionTarget(world, missile);
        }
    }

    hit_result = W8_MISSILE_HIT;
    if (!game_.data.missiles[missile.missile_table_index].spell_missile) {
        if (missile.combat_slot.iType == W8_TARGET_KIND_CHARACTER) {
            deflect_chance =
                game_.party.characters[missile.combat_slot.iChar].bonus.missile_deflect_chance;
        } else {
            W8MonsterInfo* monster_info =
                world.population->findMonster(missile.combat_slot.iMonsterID);
            deflect_chance = monster_info->modifiers.missile_deflect_chance;
        }
        if (deflect_chance > 0 && game_.rng.below(100) + 1 <= deflect_chance) {
            hit_result = W8_MISSILE_HIT_DEFLECTED;
        }
    }

    missile.deflected = hit_result == W8_MISSILE_HIT_DEFLECTED;
    if (missile.missile_table_index == kStoredSpellMissile) {
        if (game_.combat && game_.combat->engaged_missile == &missile) {
            game_.combat->missile_hit_result = hit_result;
        }
    } else if (game_.combat.has_value() && game_.combat->engaged_missile != 0) {
        game_.combat->missile_hit_result = hit_result;
        game_.combat->TargetHit = missile.combat_slot;
    } else if (game_.data.missiles[missile.missile_table_index].spell_missile) {
        resolveSpellMissileHit(world, missile);
    } else {
        resolveMissileHit(world, missile, hit_result == W8_MISSILE_HIT_DEFLECTED);
    }
    return true;
}

/* A stored-spell missile releases its spell unless its collision was deflected,
   and a spell missile hands what it did to the spell that cast it. */
void CombatSignals::missileLanded(Missile& missile)
{
    if (missile.missile_table_index == kStoredSpellMissile && !missile.deflected) {
        DetonateStoredSpell(missile);
    }
    if (game_.data.missiles[missile.missile_table_index].spell_missile != 0) {
        absorbMissileDamage(missile);
    }
}

/* Launch the missile at the frame shared by attack cycles 7, 13 and 17. In
   combat an already-selected attack is reused; otherwise the monster picks a
   live character and the first database attack that permits a missile mode. */
void CombatSignals::missileReleased(Monster& monster)
{
    const World& world = *monster.owner_world;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;
    W8TargetSource source;
    W8SpellEffectDefinition attack_block;
    const W8MonsterAttack* attack;
    unsigned int attack_index;
    W8RangeCategory range_category;
    int missile_type;
    int accuracy;
    bool selected_attack;
    unsigned char monster_value;

    selected_attack = false;
    monster_info = world.population->findMonster(monster.location_id);
    record = GetMonsterDataForInfo(monster_info);
    SetTargetSourceToMonster(monster_info, &source);

    if (game_.combat.has_value() &&
        game_.combat->resolvingTurnOf(MonsterActor{monster_info->location_id})) {
        attack_index = monster_info->pCombat->attack_index;
        selected_attack = true;
        goto prepare_attack;
    }

    monster_info->Target.iType = W8_TARGET_KIND_CHARACTER;
    monster_info->Target.iChar = GetRandomCharacter(1, 1, -1, -1);
    monster_info->Target.iMonsterID = -1;
    if (monster_info->Target.iChar == -1) {
        return;
    }

    for (attack_index = 0; attack_index < W8_MAX_MONSTER_ATTACKS; ++attack_index) {
        attack = &record->attacks[attack_index];
        if (attack->fHasAttack != 0 && (attack->attack_modes & 0x110) != 0) {
            monster_info->action_detail = ChooseAttackMode(attack->attack_modes);
            goto prepare_attack;
        }
    }

    missile_type = 0;
    range_category = W8_RANGE_EXTREME;
    attack_block = {};
    accuracy = 50;
    goto fire_missile;

prepare_attack:
    assert(attack_index < W8_MAX_MONSTER_ATTACKS);
    attack = &record->attacks[attack_index];
    missile_type = attack->missile_type;
    if (static_cast<unsigned int>(missile_type) >= game_.data.missiles.size()) {
        missile_type = 0;
    }

    range_category = MonsterAttackRange(*monster_info, *record, attack_index);
    attack_block = {};
    attack_block.magnitude = attack->damage_dice;
    std::ranges::copy(attack->missile_values, attack_block.condition_chances);
    monster_value = record->effective_level;
    attack_block.power_level = monster_value + (monster_value < 15 ? monster_value : 15);
    attack_block.magnitude_base = attack->missile_magnitude;

    if (selected_attack) {
        accuracy = GetMonsterAttackScore(monster_info, attack,
                                         static_cast<W8AttackMode>(monster_info->action_detail), 0);
    } else {
        accuracy = 50;
    }

fire_missile:
    monster_info->fMissileReleased = true;
    FireMissileSourceToTarget(missile_type, &source, &monster_info->Target, &attack_block,
                              !selected_attack, range_category, accuracy);
}

/* Cast the monster's pending spell action as its casting animation releases it. The
   cast's return is the stamina charge passed directly to FatigueMonster. */
void CombatSignals::spellReleased(Monster& monster)
{
    W8MonsterInfo* monster_info = monster.owner_world->population->findMonster(monster.location_id);
    const W8MonsterActionKind action_kind = monster_info->action_kind;
    const int action_detail = monster_info->action_detail;
    const unsigned int power_level = monster_info->spell_power_level;
    if (action_kind == W8_MONSTER_ACTION_SPELL && action_detail != 0 && power_level != 0) {
        const unsigned int fatigue = MonsterCastsSpell(monster_info, action_detail, power_level);
        FatigueMonster(monster_info, fatigue, 0);
        monster_info->fSpellReleased = true;
    }
}

/* Post "<source> hits <target>" to the notice box and colour the target's name with the
   target side's colour when it differs from the source's. */
void CombatSignals::announceCollisionTarget(const World& world, const Missile& missile)
{
    if (!game_.combat.has_value()) {
        return;
    }
    std::string text = fmt::format("{} ", game_.data.strings[0x1bf]);
    const auto target_start = static_cast<unsigned char>(text.size());
    if (missile.combat_slot.iType == W8_TARGET_KIND_MONSTER) {
        text += GetMonsterName(world.population->findMonster(missile.combat_slot.iMonsterID), 0, 0);
    } else {
        text += game_.party.characters[missile.combat_slot.iChar].name;
    }
    const auto target_stop = static_cast<unsigned char>(text.size());
    const auto source_color = static_cast<unsigned char>(GetSourceNoticeColor(&missile.m_Source));
    const auto target_color =
        static_cast<unsigned char>(GetTargetNoticeColor(&missile.m_Source, &missile.combat_slot));
    text += " ";
    text += game_.data.strings[0x1c0];
    ShowNotice(source_color, text);
    if (target_color != source_color) {
        HighlightTextBoxRange(target_color, target_start, target_stop, -1);
    }
}

/* The owning effect is the one whose missile list still names the missile; nothing
   happens if it has already been detached. */
void CombatSignals::absorbMissileDamage(Missile& missile)
{
    for (const auto& effect : game_.spell_effects) {
        if (!std::ranges::contains(effect->missiles, &missile)) continue;
        effect->result.count += missile.result.count;
        effect->result.amount += missile.result.amount;
        for (int condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
            effect->result.condition_counts[condition] +=
                missile.result.condition_counts[condition];
        }
        auto& reports = missile.result.reports;
        effect->result.reports.insert(effect->result.reports.end(),
                                      std::make_move_iterator(reports.begin()),
                                      std::make_move_iterator(reports.end()));
        reports.clear();
        return;
    }
}

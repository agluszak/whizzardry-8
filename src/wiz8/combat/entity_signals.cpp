#include "wiz8/combat/entity_signals.h"

#include <algorithm>

#include <fmt/format.h>

#include "wiz8/data/attack_modes.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/monster_actions.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/simulation_sink.h"

namespace {
class CombatSignals final : public SimulationSink {
public:
    bool missileReached(Missile& missile, Navigator& struck) override;
    void missileReleased(Monster& monster) override;
    void spellReleased(Monster& monster) override;
};

/* Decide what the missile struck - the party or a live monster - record it in its
   combat slot, roll the target's missile deflection and either resolve the hit or
   leave it for the combat engine. */
bool CombatSignals::missileReached(Missile& missile, Navigator& struck)
{
    W8MissileHitResult hit_result;
    unsigned char deflect_chance;

    if (PartyNavigator() == &struck) {
        if (TargetSourceIsCharacter(&missile.m_Source, 0)) {
            return false;
        }
        if (missile.combat_slot.iType != W8_TARGET_KIND_PARTY &&
            missile.combat_slot.iType != W8_TARGET_KIND_CHARACTER) {
            if (Data().missiles[missile.missile_table_index].spell_missile) {
                return false;
            }
            if (missile.combat_slot.iType != W8_TARGET_KIND_NONE) {
                missile.retargeted = true;
            }
            missile.combat_slot.iType = W8_TARGET_KIND_CHARACTER;
            missile.combat_slot.iChar = GetRandomCharacter(1, 1, -1, -1);
            missile.combat_slot.iMonsterID = -1;
            if (CurrentGame().combat.active()) {
                missile.AnnounceCollisionTarget();
            }
        }
    } else {
        if (struck.movement.location_id <= 0) {
            return false;
        }
        Monster* monster = struck.monster_owner;
        if (!monster) return false;
        int location_id = monster->location_id;
        W8MonsterInfo* monster_info = FindMonster(location_id);
        if (TargetSourceIsMonster(&missile.m_Source, 0) &&
            missile.m_Source.iMonsterID == location_id) {
            return false;
        }
        if (monster_info->hp_current == 0) {
            return false;
        }
        if (missile.combat_slot.iType != W8_TARGET_KIND_MONSTER ||
            missile.combat_slot.iMonsterID != location_id) {
            if (Data().missiles[missile.missile_table_index].spell_missile) {
                return false;
            }
            if (missile.combat_slot.iType != W8_TARGET_KIND_NONE) {
                missile.retargeted = true;
            }
            missile.combat_slot.iType = W8_TARGET_KIND_MONSTER;
            missile.combat_slot.iChar = -1;
            missile.combat_slot.iMonsterID = location_id;
            if (CurrentGame().combat.active()) {
                missile.AnnounceCollisionTarget();
            }
        }
    }

    hit_result = W8_MISSILE_HIT;
    if (!Data().missiles[missile.missile_table_index].spell_missile) {
        if (missile.combat_slot.iType == W8_TARGET_KIND_CHARACTER) {
            deflect_chance = CurrentGame()
                                 .party.characters[missile.combat_slot.iChar]
                                 .bonus.missile_deflect_chance;
        } else {
            W8MonsterInfo* monster_info = FindMonster(missile.combat_slot.iMonsterID);
            deflect_chance = monster_info->modifiers.missile_deflect_chance;
        }
        if (deflect_chance > 0 && CurrentGame().rng.below(100) + 1 <= deflect_chance) {
            hit_result = W8_MISSILE_HIT_DEFLECTED;
        }
    }

    if (missile.missile_table_index == 0x23) {
        CurrentGame().combat.state()->missile_hit_result = hit_result;
    } else if (CurrentGame().combat.state() != 0 &&
               CurrentGame().combat.state()->engaged_missile != 0) {
        CurrentGame().combat.state()->missile_hit_result = hit_result;
        CurrentGame().combat.state()->TargetHit = missile.combat_slot;
    } else if (Data().missiles[missile.missile_table_index].spell_missile) {
        ResolveSpellMissileHit(&missile);
    } else {
        ResolveMissileHit(&missile, hit_result == W8_MISSILE_HIT_DEFLECTED);
    }
    return true;
}

/* Launch the missile at the frame shared by attack cycles 7, 13 and 17. In
   combat an already-selected attack is reused; otherwise the monster picks a
   live character and the first database attack that permits a missile mode. */
void CombatSignals::missileReleased(Monster& monster)
{
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
    monster_info = FindMonster(monster.location_id);
    record = GetMonsterDataForInfo(monster_info);
    SetTargetSourceToMonster(monster_info, &source);

    if (CurrentGame().combat.active() &&
        CurrentGame().combat.state()->resolvingTurnOf(MonsterActor{monster_info->location_id})) {
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
    if (attack_index >= W8_MAX_MONSTER_ATTACKS) {
        Fail("uiAttack < MAX_MONSTER_ATTACKS");
    }
    attack = &record->attacks[attack_index];
    missile_type = attack->missile_type;
    if (static_cast<unsigned int>(missile_type) >= Data().missiles.size()) {
        DebugMessage(0, fmt::format("WARNING: {} has invalid missile type {:d} for attack {:d}",
                                    record->name0, missile_type, attack_index));
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
        CombatLog(fmt::format("TO HIT (MISSILE ACCURACY): Chance {:d}", accuracy));
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
    W8MonsterInfo* monster_info = FindMonster(monster.location_id);
    const W8MonsterActionKind action_kind = monster_info->action_kind;
    const int action_detail = monster_info->action_detail;
    const unsigned int power_level = monster_info->spell_power_level;
    if (action_kind == W8_MONSTER_ACTION_SPELL && action_detail != 0 && power_level != 0) {
        const unsigned int fatigue = MonsterCastsSpell(monster_info, action_detail, power_level);
        FatigueMonster(monster_info, fatigue, 0);
        monster_info->fSpellReleased = true;
    }
}
} // namespace

SimulationSink& CombatEntitySignals()
{
    static CombatSignals signals;
    return signals;
}

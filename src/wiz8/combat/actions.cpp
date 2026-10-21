#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/group_attacks.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"

/* The actions a fight's turn runs: each actor's committed action started, stepped
   while it resolves, and its next phase settled. */

namespace {

constexpr int kBreathNoticeId = 0x65b;

/* Aim the monster's mode-three particle axis at its combat target: the target
   monster's center, a visible group member, the aimed point, or the camera.
   Self targets and empty slots leave the axis unchanged. */
void AimMonsterBreathAtTarget(W8MonsterInfo* monster_info)
{
    if (!monster_info || !monster_info->entity) return;
    glm::vec3 position;
    int location_id;
    Monster* target_monster;
    bool no_target;

    switch (monster_info->Target.iType) {
    case W8_TARGET_KIND_MONSTER:
        location_id = monster_info->Target.iMonsterID;
        no_target = monster_info->location_id == location_id;
        break;
    case W8_TARGET_KIND_PLACE:
        monster_info->entity->visual.m_axis = monster_info->Target.point;
        monster_info->entity->visual.aim_set = true;
        return;
    case W8_TARGET_KIND_GROUP:
        location_id = FindNearestVisibleGroupMonster(monster_info, monster_info->Target.iGroupID,
                                                     W8VisibilityCondition::ThirdLineOfSightFlag);
        no_target = location_id == -1;
        break;
    case W8_TARGET_KIND_NONE:
    case W8_TARGET_KIND_ALL_ENEMIES:
        return;
    default:
        position = cameraLocation();
        monster_info->entity->visual.m_axis = position;
        monster_info->entity->visual.aim_set = true;
        return;
    }
    if (!no_target) {
        target_monster = GetMonsterByLocationID(location_id);
        if (!target_monster) return;
        monster_info->entity->visual.m_axis.x = target_monster->nav.movement.position.x;
        monster_info->entity->visual.m_axis.y =
            target_monster->nav.movement.position.y + target_monster->nav.movement.height_offset;
        monster_info->entity->visual.m_axis.z = target_monster->nav.movement.position.z;
        monster_info->entity->visual.aim_set = true;
    }
}

/* Start the monster's special attack once CanMonsterFlee and AimFleeingMonster allow
   it: face the target unless the special-attack mode table says otherwise, play the
   special-attack cycle and report the attempt. */
bool StartMonsterSpecialAttack(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    if (!CanMonsterFlee(monster_info, record, false)) {
        return false;
    }
    if (!AimFleeingMonster(monster_info, record)) {
        return false;
    }
    if (g_special_attack_table[record->special_attack_kind][0] != 6) {
        OrientMonsterTowardTarget(monster_info, false);
        AimMonsterBreathAtTarget(monster_info);
    }
    monster_info->pCombat->special_ready = false;
    StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL, W8_ANIMATION_PLAY_ONCE);
    if (g_settings.verbose_combat_messages != 0) {
        CurrentGame().events.push(TextNotice{
            9,
            fmt::format(
                "{} {}!", GetMonsterName(monster_info, nullptr, 0),
                Data().strings[g_monster_special_attack_name_ids[record->special_attack_kind]])});
        return true;
    }
    CurrentGame().events.push(TextNotice{
        W8_FONT_PALETTE_RUST,
        fmt::format(
            "{} {}", GetMonsterName(monster_info, nullptr, 0),
            Data().strings[g_monster_special_attack_name_ids[record->special_attack_kind]])});
    return true;
}

/* Run the monster's committed special attack: for a non-summoning kind, point
   the camera at the target, rebuild the spell's character and monster marker
   lists, fold in every monster hostile to the source, then resolve the group
   attack. Either way the record's cooldown lands in its combat state and the
   attack costs a tenth of its stamina. */
ActionStep ReleaseMonsterSpecialAttack(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    W8TargetSource source;
    std::vector<int> char_targets;
    std::vector<int> monster_targets;

    if (!monster_info->pCombat->special_ready) {
        return ActionStep::resolving;
    }
    SetTargetSourceToMonster(monster_info, &source);
    if (g_special_attack_table[record->special_attack_kind][0] != 6) {
        monster_info->entity->visual.aim_set = false;
        PointCameraAtCombatTarget(&source, &monster_info->Target);
        PopulateSpellTargetMarkers(W8_SPELL_SPECIAL_ATTACK_CONE, 0, &source, &monster_info->Target,
                                   monster_targets, char_targets,
                                   W8AreaSightRule::RequireLineOfSight);
        ProvokeListedMonsterGroups(&source, monster_targets);
    }
    ResolveMonsterGroupAttack(record->special_attack_kind, &source, &monster_info->Target,
                              char_targets, monster_targets);
    monster_info->pCombat->special_cooldown = record->special_attack_cooldown;
    FatigueMonster(monster_info, static_cast<unsigned int>(monster_info->stamina_max) / 10,
                   nullptr);
    return ActionStep::finishing;
}

} // namespace

/* In a fight, the in-combat action the chooser stored becomes the live pending action;
   the tail flags engagement and charges the fatigue. */
void ExecuteCharacterAction(Game& game, int party_slot)
{
    W8Character* character = &game.party.characters[party_slot];
    W8PartySlotRow* slot = &game.party.rows[party_slot];
    W8TargetSource source;
    W8TargetSource enemy_source;
    int interrupt = -1;
    // What the action cost in stamina; without one, its kind's ordinary cost is charged.
    std::optional<int> fatigue_cost;
    unsigned char result = 1;
    bool saved_dead = false; /* only combat tracks whether the slot already acted */
    W8ActionKind action;
    int detail;

    FaceCameraToSelection(party_slot);
    if (game.combat.has_value()) {
        saved_dead = game.combat->characters[party_slot].dead;
        if (!saved_dead) {
            slot->executing.kind = slot->committed.kind;
            slot->executing.attack_mode[0] = slot->committed.detail;
            slot->executing.attack_mode[1] = -1;
            slot->executing.detail_block = slot->committed.detail_block;
            slot->executing.target = slot->committed.target;
            game.combat->characters[party_slot].action_changed = false;
            if ((slot->executing.kind == W8_ACTION_ATTACK ||
                 slot->executing.kind == W8_ACTION_BERSERK) &&
                CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, false,
                                     false)) {
                PrepareCharacterAttacks(party_slot);
            }
        } else if ((slot->executing.kind == W8_ACTION_ATTACK ||
                    slot->executing.kind == W8_ACTION_BERSERK) &&
                   game.combat->characters[party_slot].alternate_hand) {
            W8CombatCharacterRow* row = &game.combat->characters[party_slot];
            row->hand_attack_values[0] = 0;
            row->hand_attack_values[1] = 0;
            row->uiSwingsRemaining = 0;
            slot->executing.kind = W8_ACTION_NONE;
        }
    }
    action = slot->executing.kind;
    detail = slot->executing.attack_mode[0];
    if (game.combat.has_value()) {
        if (action != W8_ACTION_DEFEND && action != W8_ACTION_PROTECT) {
            game.combat->characters[party_slot].dead = true;
            CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
            CurrentGame().events.push(CombatProgressChanged{});
        }
        if (!CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, false, false)) {
            slot->executing.kind = W8_ACTION_NONE;
            action = W8_ACTION_NONE;
        }
        SetTargetSourceToCharacter(party_slot, &source);
        interrupt = GetConditionInterrupt(&source);
        if (interrupt != -1) {
            W8Disposition relationship = W8_DISPOSITION_NEUTRAL;
            game.events.push(
                CharacterNotice{static_cast<unsigned int>(party_slot),
                                Data().strings[g_condition_interrupt_notices[interrupt]]});
            switch (interrupt) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 10:
                slot->executing.kind = W8_ACTION_NONE;
                action = W8_ACTION_NONE;
                break;
            case 7:
                slot->executing.kind = W8_ACTION_NONE;
                action = W8_ACTION_NONE;
                LiftCondition(CurrentGame(), party_slot, W8_CONDITION_WEBBED, true);
                break;
            case 8:
                game.combat->characters[party_slot].berserk = true;
                relationship = W8_DISPOSITION_FRIENDLY;
                break;
            case 9:
                relationship = W8_DISPOSITION_HOSTILE;
                break;
            }
            /* Berserk and confused characters turn on a reachable character:
               berserk ones with their ranged attack when it reaches far enough. */
            if (interrupt == 8 || interrupt == 9) {
                int target;

                if (interrupt == 8 && GetBestHandRangeCategory(character) < W8_RANGE_LONG) {
                    action = W8_ACTION_BERSERK;
                    slot->executing.kind = W8_ACTION_BERSERK;
                } else {
                    action = W8_ACTION_ATTACK;
                    slot->executing.kind = W8_ACTION_ATTACK;
                }
                target = PickReachableSlotByDisposition(party_slot, relationship);
                if (target == -1) {
                    DebugMessage(1,
                                 fmt::format("ERROR: {} is attacking friends with nobody in range",
                                             character->name));
                    slot->executing.kind = W8_ACTION_NONE;
                    action = W8_ACTION_NONE;
                } else {
                    if (!saved_dead) {
                        PrepareCharacterAttacks(party_slot);
                    }
                    AimAtCharacter(party_slot, target, W8_TARGETING_CONTEXT_OUT_OF_COMBAT);
                }
            }
        }
        if (CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, false, false)) {
            FaceCharacterTowardCombatTarget(party_slot, &slot->executing.target);
        }
    }
    if (CharacterActionTargetsEnemies(game.party, character, action, detail,
                                      &slot->executing.detail_block) &&
        (!game.combat.has_value() ||
         (interrupt == -1 && !game.combat->characters[party_slot].berserk))) {
        SetTargetSourceToCharacter(party_slot, &enemy_source);
        MakeTargetGroupHostile(&enemy_source, &slot->executing.target);
    }
    switch (action) {
    case W8_ACTION_CAST_SPELL: {
        unsigned int power = slot->executing.detail_block.spell.power_level;
        int step_cost;
        int cast;
        int spent = 0;
        do {
            cast = ExecuteCharacterSpellCast(party_slot, detail, power, &step_cost, false);
            if (cast == 0) {
                result = 0;
            }
            spent += step_cost;
        } while (cast == 2 && power == 8 &&
                 SpellCastFatigueCost(detail, 1) + spent <= character->stamina &&
                 Data().spells[detail].spell_point_cost <=
                     character->iSPLeft[Data().spells[detail].realm]);
        fatigue_cost = spent;
        break;
    }
    case W8_ACTION_BREATHE:
        result = game.combat->startCharacterBreath(game, party_slot);
        break;
    case W8_ACTION_PRAY:
        fatigue_cost = CharacterPrayAction(party_slot);
        if (*fatigue_cost == 0) {
            result = 0;
        }
        break;
    case W8_ACTION_TURN_UNDEAD:
        if (CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 &&
            TurnUndead(party_slot, true) > 0) {
            fatigue_cost = CharacterActionFatigueCost(party_slot, W8_ACTION_TURN_UNDEAD);
        } else {
            result = 0;
        }
        break;
    case W8_ACTION_ATTACK:
        result = StartCharacterAttack(party_slot, W8_ATTACK_MODE_NONE);
        break;
    case W8_ACTION_NONE:
        result = 0;
        break;
    case W8_ACTION_BERSERK:
        result = StartCharacterAttack(party_slot, W8_ATTACK_MODE_BERSERK);
        break;
    case W8_ACTION_DEFEND:
        if (game.combat->characters[party_slot].defend_switched &&
            g_settings.verbose_combat_messages != 0) {
            game.events.push(
                CharacterNotice{static_cast<unsigned int>(party_slot), Data().strings[0x234]});
        }
        break;
    case W8_ACTION_PROTECT:
        result = CanCharacterAttackItsTarget(party_slot);
        break;
    case W8_ACTION_USE_ITEM: {
        const auto use = UseItem(game, party_slot, slot->executing.detail_block.item_use);
        fatigue_cost = use.fatigue_cost;
        if (use.effect == 0) {
            result = 0;
        }
        break;
    }
    case W8_ACTION_EQUIP:
        break;
    default:
        DebugMessage(1, fmt::format("ERROR: Char {:d} executed {} as a character action",
                                    party_slot, Data().strings[g_action_kind_message_ids[action]]));
        break;
    }
    /* A failed action falls back to defending, unless a condition interrupted
       it or the slot had already acted. */
    if (result == 0) {
        action = W8_ACTION_NONE;
        if (interrupt == -1 && !saved_dead) {
            action = W8_ACTION_DEFEND;
            slot->executing.kind = W8_ACTION_DEFEND;
            if (g_settings.verbose_combat_messages != 0) {
                game.events.push(
                    CharacterNotice{static_cast<unsigned int>(party_slot), Data().strings[0x234]});
            }
        } else {
            slot->executing.kind = W8_ACTION_NONE;
        }
    }
    if (game.combat.has_value()) {
        if (action != W8_ACTION_DEFEND && action != W8_ACTION_PROTECT) {
            game.combat->passive_round = 0;
        }
        if (CharacterActionTargetsEnemies(game.party, character, action, detail,
                                          &slot->executing.detail_block) &&
            slot->executing.target.iType >= W8_TARGET_KIND_MONSTER &&
            slot->executing.target.iType <= W8_TARGET_KIND_PLACE) {
            game.combat->combat_update_count = 0;
        }
    }
    if (action != W8_ACTION_ATTACK && action != W8_ACTION_BERSERK) {
        if (!fatigue_cost) fatigue_cost = CharacterActionFatigueCost(party_slot, action);
        FatigueCharacter(party_slot, *fatigue_cost, true, nullptr);
    }
    if (game.combat.has_value()) {
        game.combat->setStepOf(PartyActor{party_slot}, ActionStep::resolving);
    }
}

/* Derive the row's next action phase and settle which hand the slot swings
   with. Only an attack pending action that can still reach a live target
   earns a phase; anything else leaves the row idle for the round. When both
   hands can attack the off hand wins unless it is strictly worse on both the
   reach value and the damage bonus, and a switched hand charges the phase
   the difference in the hands' damage bonuses. */
void Combat::settleCharacterPhase(Game& game, int party_slot)
{
    W8CombatCharacterRow* row = &characters[party_slot];
    W8ActionKind action = game.party.rows[party_slot].executing.kind;
    if ((action == W8_ACTION_ATTACK || action == W8_ACTION_BERSERK) &&
        CanAnyHandReachTarget(party_slot) &&
        TargetIsInPlay(party_slot, 2,
                       SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
        W8Character* character = &game.party.characters[party_slot];
        int current = row->current_hand;
        int other = current == 0;
        unsigned int current_value = row->hand_attack_values[current];
        unsigned int other_value = row->hand_attack_values[other];
        int chosen = current;
        if (other_value != 0) {
            chosen = other;
            if (other_value < current_value &&
                character->Hand[other].damage_bonus < character->Hand[current].damage_bonus) {
                chosen = current;
            }
        }
        unsigned int attack_value = row->hand_attack_values[chosen];
        if (attack_value != 0) {
            unsigned int phase;
            if (chosen == current || other_value <= current_value) {
                phase = row->phase +
                        (row->phase_clock_stamp - round_counter + 100) / (attack_value + 1);
            } else {
                phase = round_counter;
            }
            row->phase = phase;
            if (phase < 0x65) {
                row->current_hand = chosen;
                if (chosen != current) {
                    row->phase = character->Hand[current].damage_bonus -
                                 character->Hand[chosen].damage_bonus + phase;
                }
                row->phase =
                    row->phase > 100u ? 100u : std::max<unsigned>(row->phase, round_counter);
                RoundPhaseToStep(&row->phase, round_counter);
                return;
            }
        }
    }
    row->phase = 0;
}

/* Run one monster's committed combat action - the monster-side counterpart
   of ExecuteCharacterAction. A condition interrupt can rewrite the
   action first (berserk forces a fresh random choice); the action-kind
   switch executes it and a failed action loops back through the AI for a
   repick until something commits or the monster holds. */
void Combat::executeMonsterAction(Game& game, W8MonsterInfo& monster)
{
    W8MonsterInfo* monster_info = &monster;
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    bool tried[11] = {};
    unsigned char result = 0;
    unsigned char move_flag = 0;
    bool berserked = false;
    int interrupt;
    int sight;
    W8RangeCategory range;
    float approach_distance;
    float engage_distance;
    glm::vec3 position;
    W8MonsterInfo* target_info;
    W8TargetSource source;
    W8TargetSource monster_source;

    if (uiCurrentPartyActionStatus == W8_PARTY_ACTION_IN_PROGRESS) {
        EndPartyMovementPhase();
    }
    PointCameraAtMonster(monster_info, false, true);
    SetTargetSourceToMonster(monster_info, &source);
    interrupt = GetConditionInterrupt(&source);
    if (interrupt != -1) {
        game.events.push(
            TextNotice{W8_FONT_PALETTE_RUST,
                       fmt::format("{} {}", GetMonsterName(monster_info, nullptr, 0),
                                   Data().strings[g_condition_interrupt_notices[interrupt]])});
        switch (interrupt) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 10:
            monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            break;
        case 7:
            monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            ClearMonsterCondition(monster_info->location_id, W8_CONDITION_WEBBED);
            break;
        case 8:
            if (!monster_info->pCombat->berserk) {
                monster_info->pCombat->berserk = true;
                berserked = true;
            }
            break;
        case 0xb:
            monster_info->action_kind = W8_MONSTER_ACTION_CONTROLLED_MOVE;
            break;
        case 0xc:
            monster_info->action_kind = W8_MONSTER_ACTION_ADVANCE;
            break;
        }
        if (monster_info->pCombat->berserk) {
            monster_info->action_kind = W8_MONSTER_ACTION_ATTACK;
            if (!ChooseRandomMonsterAction(monster_info, true, false, berserked)) {
                if (interrupt == 8) {
                    DebugMessage(
                        1,
                        fmt::format("ERROR: {} (ID {:d}) is attacking friends with nobody in range",
                                    record->name0, monster_info->location_id));
                }
                monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            }
        }
    }
    while (true) {
        if (monster_info->action_kind != W8_MONSTER_ACTION_NONE) {
            tried[monster_info->action_kind] = 1;
        }
        if (MonsterActionTargetsEnemies(monster_info->action_kind, monster_info->action_detail,
                                        &monster_info->spell_power_level) &&
            interrupt == -1 && !monster_info->pCombat->berserk) {
            SetTargetSourceToMonster(monster_info, &monster_source);
            MakeTargetGroupHostile(&monster_source, &monster_info->Target);
        }
        switch (monster_info->action_kind) {
        case W8_MONSTER_ACTION_APPROACH:
        case W8_MONSTER_ACTION_BACK_OFF:
            if (monster_info->uiCondition[W8_CONDITION_BLIND] == 0 || record->kind == '\f') {
                if (record->prefer_ranged_actions == 0) {
                    range = GetBestMonsterAttackRange(record, true);
                    sight = 0;
                } else {
                    range = GetMonsterBestRangeCategory(monster_info, false, &sight);
                }
                if (range != W8_RANGE_NONE) {
                    unsigned short move_result;
                    if (range < W8_RANGE_LONG) {
                        approach_distance = CalcRangeDistance(W8_RANGE_TOUCH) * 0.75f;
                    } else {
                        approach_distance =
                            CalcRangeDistance(W8_RANGE_SHORT) * g_prepath_link_height;
                    }
                    GetMonsterAttackSourceOffset(monster_info->entity.find(), sight, &position);
                    engage_distance = CalcRangeDistance(range);
                    MonsterChooseTarget(monster_info, &monster_info->Target,
                                        W8VisibilityCondition::SightState);
                    SetUpMonsterTurn(monster_info);
                    if (monster_info->Target.iType == W8_TARGET_KIND_MONSTER) {
                        target_info = FindMonster(monster_info->Target.iMonsterID);
                        move_result = monster_info->entity->ConfigureMovementToMonster(
                            target_info->entity.find(), approach_distance, engage_distance,
                            position, sight, &move_flag);
                    } else {
                        if (monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
                            break;
                        }
                        move_result = monster_info->entity->ConfigureMovementToPlayer(
                            approach_distance, engage_distance, position, sight, &move_flag);
                    }
                    if (move_result == 1 || (move_result == 3 && move_flag != 0)) {
                        if (monster_info->action_kind == W8_MONSTER_ACTION_APPROACH) {
                            if (g_settings.verbose_combat_messages != 0) {
                                if (monster_info->Target.iType == W8_TARGET_KIND_MONSTER) {
                                    PostMonsterNotice(
                                        monster_info,
                                        fmt::sprintf(Data().strings[0x238],
                                                     GetMonsterName(target_info, nullptr, 0)));
                                } else {
                                    PostMonsterNotice(monster_info, Data().strings[0x236]);
                                }
                            }
                        } else if (monster_info->Target.iType == W8_TARGET_KIND_MONSTER) {
                            PostMonsterNotice(
                                monster_info,
                                fmt::sprintf(Data().strings[0x239],
                                             GetMonsterName(target_info, nullptr, 0)));
                        } else {
                            PostMonsterNotice(monster_info, Data().strings[0x237]);
                        }
                        result = 1;
                        break;
                    }
                    if (monster_info->pCombat->reconsider_action) {
                        monster_info->pCombat->reconsider_action = false;
                        continue;
                    }
                    if (move_flag != 0) {
                        break;
                    }
                }
                monster_info->action_kind = W8_MONSTER_ACTION_RETURN_TO_START;
                continue;
            }
            break;
        case W8_MONSTER_ACTION_RETURN_TO_START:
            if (monster_info->summoned != W8_MONSTER_SUMMON_NONE ||
                (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY &&
                 CurrentLevelPopulation().monster_roster.hostileCombatCount() == 0)) {
                game.events.push(TextNotice{
                    W8_FONT_PALETTE_RUST,
                    fmt::sprintf(Data().strings[0x23c], GetMonsterName(monster_info, nullptr, 0))});
                monster_info->action_kind = W8_MONSTER_ACTION_WAIT;
                continue;
            }
            SetUpMonsterTurn(monster_info);
            result = monster_info->entity->LinkToStartupNavigator() != 0;
            if (result != 0) {
                game.events.push(TextNotice{
                    W8_FONT_PALETTE_RUST,
                    fmt::sprintf(Data().strings[0x23a], GetMonsterName(monster_info, nullptr, 0))});
            }
            break;
        case W8_MONSTER_ACTION_ADVANCE:
            SetUpMonsterTurn(monster_info);
            result = monster_info->entity->LinkToStartupNavigator() != 0;
            if (result != 0) {
                monster_info->pCombat->advancing = true;
                if (interrupt != 0xc) {
                    game.events.push(TextNotice{
                        9, fmt::sprintf(
                               Data().strings[0x23b], GetMonsterName(monster_info, nullptr, 0),
                               Data().strings[g_gender_name_message_rows[record->name_group][2]])});
                }
            }
            break;
        case W8_MONSTER_ACTION_CONTROLLED_MOVE:
            SetUpMonsterTurn(monster_info);
            result = IsMonsterControlPointInRange(monster_info) != 0;
            break;
        case W8_MONSTER_ACTION_ATTACK:
            result = StartMonsterAttack(monster_info, record);
            break;
        case W8_MONSTER_ACTION_SPELL: {
            int spell_id = monster_info->action_detail;
            unsigned int power = ChooseMonsterSpellPowerLevel(monster_info, record, spell_id);
            if (!CanMonsterAimSpell(monster_info, spell_id) ||
                !MonsterOKToCastSpell(monster_info, spell_id, power)) {
                result = 0;
                break;
            }
            monster_info->spell_power_level = power;
            OrientMonsterTowardTarget(monster_info, false);
            monster_info->fSpellReleased = false;
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPELL, W8_ANIMATION_PLAY_ONCE);
            result = 1;
            break;
        }
        case W8_MONSTER_ACTION_SPECIAL_ATTACK:
            result = StartMonsterSpecialAttack(monster_info, record);
            break;
        case W8_MONSTER_ACTION_PROTECT:
            result = CanMonsterProtectTarget(monster_info);
            break;
        case W8_MONSTER_ACTION_WAIT:
            result = 1;
            break;
        default:
            break;
        }
        if (result != 0) {
            if (monster_info->action_kind != W8_MONSTER_ACTION_WAIT &&
                monster_info->action_kind != W8_MONSTER_ACTION_PROTECT) {
                monster_info->entity->nav.SetHalted(false);
            }
            break;
        }
        if (interrupt != -1 || monster_info->pCombat->active ||
            monster_info->action_kind == W8_MONSTER_ACTION_CONTROLLED_MOVE) {
            monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            break;
        }
        UpdateMonsterAI(monster_info);
        if (monster_info->action_kind == W8_MONSTER_ACTION_NONE ||
            tried[monster_info->action_kind] != '\0') {
            monster_info->action_kind = W8_MONSTER_ACTION_WAIT;
        }
        if (monster_info->action_kind == W8_MONSTER_ACTION_WAIT) {
            game.events.push(TextNotice{
                W8_FONT_PALETTE_RUST,
                fmt::sprintf(Data().strings[0x23c], GetMonsterName(monster_info, nullptr, 0))});
            break;
        }
    }
    if (monster_info->action_kind != W8_MONSTER_ACTION_WAIT &&
        monster_info->action_kind != W8_MONSTER_ACTION_PROTECT) {
        passive_round = 0;
    }
    if (IsMonsterActionUsable(monster_info)) {
        monster_info->pCombat->settle_ticks = 0;
    }
    monster_info->pCombat->active = true;
    CurrentGame().events.push(CombatProgressChanged{});
    setStepOf(MonsterActor{monster_info->location_id}, ActionStep::resolving);
}

/* The character's breath action: refuse while the previous breath visual is
   still playing, hand it to the world updater, rebuild the spell marker lists
   from the committed target, fold in every hostile monster, resolve the group
   attack as kind 0x1e and tax a fifth of the stamina ceiling. */
ActionStep Combat::releaseCharacterBreath(Game& game, int party_slot)
{
    W8TargetSource source;
    std::vector<int> char_targets;
    std::vector<int> monster_targets;

    if (!breath_visual->finished) {
        return ActionStep::resolving;
    }
    breath_visual->auto_release = true;
    SetTargetSourceToCharacter(party_slot, &source);
    PopulateSpellTargetMarkers(W8_SPELL_SPECIAL_ATTACK_CONE, 0, &source,
                               &game.party.rows[party_slot].executing.target, monster_targets,
                               char_targets, W8AreaSightRule::RequireLineOfSight);
    ProvokeListedMonsterGroups(&source, monster_targets);
    ResolveMonsterGroupAttack(0x1e, &source, &game.party.rows[party_slot].executing.target,
                              char_targets, monster_targets);
    FatigueCharacter(
        party_slot,
        static_cast<int>(static_cast<unsigned int>(game.party.characters[party_slot].uiStaminaMax) /
                         5),
        false, nullptr);
    return ActionStep::finishing;
}

/* The character's breathe step: face the committed target, rebuild the spell's
   marker lists from it, then build the breath visual aimed down the
   camera-to-target line. Fails when no monster marker survives, when the
   visual cannot be created, and otherwise reports the breath name and leaves
   the visual held for the attack. */
bool Combat::startCharacterBreath(Game& game, int party_slot)
{
    W8TargetSource source;
    glm::mat3 rotation;
    glm::vec3 camera;
    glm::vec3 direction;
    std::vector<int> char_targets;
    std::vector<int> monster_targets;
    glm::vec3 aim;
    W8CombatSlot* target;
    double yaw;
    double pitch;

    SetTargetSourceToCharacter(party_slot, &source);
    target = &game.party.rows[party_slot].executing.target;
    PopulateSpellTargetMarkers(W8_SPELL_SPECIAL_ATTACK_CONE, 0, &source, target, monster_targets,
                               char_targets, W8AreaSightRule::RequireLineOfSight);
    if (monster_targets.empty()) {
        game.events.push(
            CharacterNotice{static_cast<unsigned int>(party_slot),
                            fmt::sprintf(Data().strings[0x1b7],
                                         Data().spells[W8_SPELL_DRACON_BREATH].display_name)});
        return false;
    }
    FaceCharacterTowardCombatTarget(party_slot, target);
    camera = cameraLocation();
    aim = target->point - camera;
    direction = aim;
    aim = glm::dot(aim, aim) == 0 ? aim : glm::normalize(aim) * static_cast<float>(1.0);
    rotation = glm::mat3(1);
    yaw = atan2(aim.x, aim.z);
    rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
    aim = rotation * direction;
    pitch = -atan2(aim.y, aim.z);
    rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
    breath_visual = CreateAimedSpellEffect(
        Data().spells[W8_SPELL_SPECIAL_ATTACK_CONE].resource_name, 1, &camera, &rotation, 0, 0);
    if (breath_visual == 0) {
        return false;
    }
    if (g_settings.verbose_combat_messages != 0) {
        game.events.push(CharacterNotice{static_cast<unsigned int>(party_slot),
                                         fmt::format("{}!", Data().strings[kBreathNoticeId])});
    } else {
        game.events.push(CharacterNotice{static_cast<unsigned int>(party_slot),
                                         fmt::format("{} -- ", Data().strings[kBreathNoticeId])});
    }
    breath_visual->auto_release = false;
    PointCameraAtCombatTarget(&source, target);
    return true;
}

/* Step the acting monster's committed action: attack swings, the spell wait,
   the special attack, or the movement kinds - which may retarget, re-decide
   through the AI and bank the next phase. The outcome code lands in
   the turn while this monster still holds it. */
void Combat::stepMonsterAction(Game& game, W8MonsterInfo& monster)
{
    W8MonsterInfo* monster_info = &monster;
    ActionStep outcome;
    /* MonsterChooseTarget fills the target kind and the chosen monster's id. */
    W8CombatSlot chosen;
    int progress_pct;
    const W8MonsterRecord* record;
    W8MonsterCombatState* combat;
    Monster* entity;
    int action_kind;

    switch (monster_info->action_kind) {
    case W8_MONSTER_ACTION_ADVANCE:
    case W8_MONSTER_ACTION_APPROACH:
    case W8_MONSTER_ACTION_RETURN_TO_START:
    case W8_MONSTER_ACTION_BACK_OFF:
    case W8_MONSTER_ACTION_CONTROLLED_MOVE:
        PointCameraAtMonster(monster_info, false, true);
        if (!monster_info->entity->nav.movement_complete) {
            outcome = ActionStep::resolving;
            break;
        }
        if ((monster_info->action_kind == W8_MONSTER_ACTION_APPROACH ||
             monster_info->action_kind == W8_MONSTER_ACTION_BACK_OFF) &&
            monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_WEBBED &&
            monster_info->uiCondition[W8_CONDITION_BLIND] == 0) {
            MonsterChooseTarget(monster_info, &chosen, W8VisibilityCondition::ThirdLineOfSightFlag);
            if (chosen.iType == W8_TARGET_KIND_PARTY) {
                monster_info->entity->ForwardReferencePosition(0);
            } else if (chosen.iType == W8_TARGET_KIND_MONSTER) {
                monster_info->entity->AimAtMonster(GetMonsterByLocationID(chosen.iMonsterID),
                                                   false);
            }
        }
        RefreshAllSight();
        entity = monster_info->entity.find();
        combat = monster_info->pCombat.get();
        if (static_cast<int>((entity->nav.movement.callback_threshold <= 0.0f
                                  ? entity->nav.movement.callback_progress /
                                        entity->nav.movement.callback_threshold
                                  : 0.0f) *
                             g_octree_cell_scale) < 100) {
            progress_pct = static_cast<int>((entity->nav.movement.callback_threshold <= 0.0f
                                                 ? entity->nav.movement.callback_progress /
                                                       entity->nav.movement.callback_threshold
                                                 : 0.0f) *
                                            g_octree_cell_scale);
        } else {
            progress_pct = 100;
        }
        if (static_cast<int>(game.rng.below(100)) < 100 - progress_pct) {
            if (monster_info->action_kind != W8_MONSTER_ACTION_CONTROLLED_MOVE) {
                UpdateMonsterAI(monster_info);
            }
            action_kind = monster_info->action_kind;
            if (action_kind < 4 || (action_kind > 7 && action_kind != 9)) {
                combat->active = false;
                combat->phase += round_counter;
                if (combat->phase > 100) {
                    combat->phase = 100;
                }
                RoundPhaseToStep(&combat->phase, round_counter);
            }
        }
        outcome = ActionStep::finishing;
        break;
    case W8_MONSTER_ACTION_ATTACK:
        record = GetMonsterDataForInfo(monster_info);
        outcome = ContinueMonsterAttack(monster_info, record);
        break;
    case W8_MONSTER_ACTION_SPELL:
        assert(monster_info->fSpellReleased);
        outcome = ActionStep::resolving;
        break;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        record = GetMonsterDataForInfo(monster_info);
        outcome = ReleaseMonsterSpecialAttack(monster_info, record);
        break;
    default:
        outcome = ActionStep::finishing;
        break;
    }
    setStepOf(MonsterActor{monster_info->location_id}, outcome);
}

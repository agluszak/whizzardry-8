#include <cassert>
#include <chrono>
#include <cmath>
#include <variant>

#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/party_camera.h"

/* The fight's turn: what it waits on, the scheduler that picks who acts next, and the
   step that moves the turn through its phases. */

namespace {
/* Whether combat must keep waiting on a live monster's pending cycle, running
   animation or unfinished turn before the action state machine may advance. */
bool AnyCombatMonsterBusy(const Combat& fight)
{
    auto monster_count = CurrentLevelPopulation().monster_roster.live().size();
    for (unsigned int monster_index = 0; monster_index < monster_count; ++monster_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
        Monster* monster = monster_info->p3D;
        W8MonsterCycle cycle = static_cast<W8MonsterCycle>(monster->Query(W8_MONSTER_QUERY_CYCLE));
        if (cycle == W8_MONSTER_CYCLE_DIE &&
            GetMonsterDataForInfo(monster_info)->record_id == 0x234) {
            return true;
        }
        if (monster_info->pCombat) {
            if (monster->HasPendingCycle() || monster->IsScalingY()) {
                return true;
            }
            if (monster->IsAnimating() != 0 && cycle != W8_MONSTER_CYCLE_IDLE &&
                cycle != W8_MONSTER_CYCLE_SPICE && cycle != W8_MONSTER_CYCLE_WALK &&
                cycle != W8_MONSTER_CYCLE_DIE) {
                if (!fight.resolvingTurnOf(MonsterActor{monster_info->location_id})) {
                    return true;
                }
                if (NormalizeMonsterCycle(cycle) != W8_MONSTER_CYCLE_ATTACK_CLOSE) {
                    if (NormalizeMonsterCycle(cycle) != W8_MONSTER_CYCLE_ATTACK_RANGED) {
                        return true;
                    }
                    return !monster_info->fMissileReleased;
                }
                if (monster->Query(W8_MONSTER_QUERY_FRAME) <
                    monster->Query(W8_MONSTER_QUERY_FRAME_COUNT) / 2) {
                    return true;
                }
            }
            if (fabsf(monster->nav.movement.target_yaw - monster->nav.movement.yaw) >=
                    g_camera_transition_epsilon &&
                monster_info->highest_condition < W8_CONDITION_WEBBED) {
                return true;
            }
        }
        monster_count = CurrentLevelPopulation().monster_roster.live().size();
    }
    return false;
}
} // namespace

CombatWait Combat::waitingOn(const Game& game) const
{
    if (partyCamera().m_transition_active) {
        return CombatWait::camera;
    }
    if (AnyCombatMonsterBusy(*this)) {
        return CombatWait::monster;
    }
    if (hit_sound) {
        return CombatWait::hit_sound;
    }
    if (!AllSpellEffectsStillRunning()) {
        return CombatWait::spell_effect;
    }
    for (Missile* missile = NextMissile(true); missile != 0; missile = NextMissile(false)) {
        if ((missile == engaged_missile ||
             Data().missiles[missile->missile_table_index].spell_missile != 0) &&
            missile->BlocksEndingCombat()) {
            return CombatWait::missile;
        }
    }
    for (unsigned int slot = 0; slot < 2; ++slot) {
        if (game.party.members[slot].has_value() && game.party.characters[slot].hp_current != 0 &&
            game.party.characters[slot].highest_condition < W8_CONDITION_ASLEEP &&
            npc_combat_script_pending[slot]) {
            return CombatWait::npc_script;
        }
    }
    return CombatWait::none;
}

/* Start the scheduled actor's action once the action clock has run out. */
void Combat::beginScheduledAction(Game& game, const CombatActor& actor)
{
    if (const auto* party = std::get_if<PartyActor>(&actor)) {
        ExecuteCharacterAction(game, party->slot);
        return;
    }
    executeMonsterAction(game, *FindMonster(std::get<MonsterActor>(actor).location_id));
}

/* Step the resolving actor's action and move its turn to what the step reports. */
void Combat::stepResolvingAction(Game& game, const CombatActor& actor)
{
    const auto* party = std::get_if<PartyActor>(&actor);
    if (!party) {
        stepMonsterAction(game, *FindMonster(std::get<MonsterActor>(actor).location_id));
        return;
    }
    const int slot = party->slot;
    W8ActionKind pending_action = game.party.rows[slot].executing.kind;
    ActionStep result;
    if (pending_action >= W8_ACTION_ATTACK && pending_action < W8_ACTION_BREATHE) {
        result = ResolveCharacterAttack(slot);
    } else if (pending_action == W8_ACTION_BREATHE) {
        result = releaseCharacterBreath(game, slot);
    } else {
        result = ActionStep::finishing;
    }
    setStepOf(actor, result);
}

/* Settle a finished action: a party slot falls back to its remembered combat
   choice and takes its next phase; a monster pays the fatigue and may queue
   another attack this round. The turn becomes idle. */
void Combat::finishAction(Game& game, const CombatActor& actor)
{
    const auto* party = std::get_if<PartyActor>(&actor);
    if (!party) {
        EndMonsterAttack(FindMonster(std::get<MonsterActor>(actor).location_id));
        return;
    }
    const int slot = party->slot;
    if (!characters[slot].action_changed) {
        W8PartySlotRow* row = &game.party.rows[slot];
        switch (row->committed.kind) {
        case W8_ACTION_ATTACK:
        case W8_ACTION_BERSERK:
        case W8_ACTION_DEFEND:
        case W8_ACTION_PROTECT:
            break;
        default:
            ResetCombatSlot(&row->committed.target);
            CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(slot)});
            if (slot == game.status.selected_character) {
                CurrentGame().events.push(MonstersChanged{});
            }
            SetCharacterCombatAction(slot, row->remembered.basic(), 0);
        }
    }
    settleCharacterPhase(game, slot);
    endTurnOf(actor);
}

void Combat::step(Game& game)
{
    if (game.advancement.held) {
        return;
    }
    if (game.surprise_possible) {
        return;
    }
    if (combat_evaluated == 0 &&
        CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0) {
        EvaluateCombatDifficulty();
        combat_evaluated = 1;
    }
    if (engaged_missile != 0 && !engaged_missile->BlocksEndingCombat()) {
        engaged_missile->block_released = 1;
        engaged_missile = 0;
    }
    if (uiCurrentPartyActionStatus == W8_PARTY_ACTION_IN_PROGRESS) {
        if (uiCurrentPartyAction == W8_PARTY_ACTION_WALK ||
            uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
            UpdateActivePartyMovement();
        }
    } else {
        AutoAdvanceSelectedCharacter();
        if (waitingOn(game) != CombatWait::none) {
            return;
        }
    }
    for (unsigned int monster_index = 0;
         monster_index < CurrentLevelPopulation().monster_roster.live().size(); ++monster_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
            monster_info->pCombat->sight_refresh_pending) {
            RefreshMonsterSight(monster_info);
            monster_info->pCombat->sight_refresh_pending = false;
        }
    }
    if (execution_active == 0) {
        if (QueueNpcCombatScript()) {
            return;
        }
        if (CheckCombatEnd(execution_active == 0 && enemies_engaged) != 0) {
            return;
        }
        if ((g_settings.continuous_combat == 0 ||
             continuous_start_delay > std::chrono::milliseconds::zero() ||
             !CombatMayAdvanceContinuously()) &&
            !party_surprised) {
            return;
        }
        BeginCombatExecution();
        return;
    }
    /* The turn is copied: each phase's handler replaces it. */
    const CombatTurn current = turn();
    if (std::holds_alternative<TurnIdle>(current)) {
        if (!QueueNpcCombatScript()) {
            scheduleNextActor(game);
        }
    } else if (const auto* scheduled = std::get_if<TurnScheduled>(&current)) {
        if (action_delay == std::chrono::milliseconds::zero()) {
            if (g_settings.autoscroll_combat_messages != 0) {
                notice_scroll_pending = true;
            }
            beginScheduledAction(game, scheduled->actor);
        }
    } else if (const auto* resolving = std::get_if<TurnResolving>(&current)) {
        stepResolvingAction(game, resolving->actor);
    } else {
        finishAction(game, std::get<TurnFinishing>(current).actor);
    }
}

void Combat::scheduleNextActor(Game& game)
{
    assert(!actor());
    bool free_turn = false;
    for (;;) {
        if (uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
            uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) {
            for (int slot = 0; slot < 8; ++slot) {
                if (!game.party.members[slot].has_value()) {
                    continue;
                }
                W8CombatCharacterRow* combat_row = &characters[slot];
                if (combat_row->phase != round_counter) {
                    continue;
                }
                W8Character* character = &game.party.characters[slot];
                if (character->hp_current != 0 &&
                    character->highest_condition < W8_CONDITION_ASLEEP) {
                    schedule(PartyActor{slot});
                    break;
                }
                row->executing.kind = W8_ACTION_NONE;
                combat_row->phase = 0;
                if (!combat_row->dead) {
                    combat_row->dead = true;
                    CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(slot)});
                    CurrentGame().events.push(CombatProgressChanged{});
                }
            }
        }
        if (!actor()) {
            for (unsigned int monster_index = 0;
                 monster_index < CurrentLevelPopulation().monster_roster.live().size();
                 ++monster_index) {
                W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
                if (!monster_info->pCombat) {
                    continue;
                }
                W8MonsterCombatState* combat = monster_info->pCombat.get();
                if (combat->phase != round_counter) {
                    continue;
                }
                if (monster_info->hp_current != 0 &&
                    monster_info->highest_condition < W8_CONDITION_ASLEEP &&
                    !monster_info->fMotionless) {
                    schedule(MonsterActor{monster_info->location_id});
                    break;
                }
                monster_info->action_kind = W8_MONSTER_ACTION_NONE;
                combat->phase = 0;
                if (!combat->active) {
                    combat->active = true;
                    CurrentGame().events.push(CombatProgressChanged{});
                }
            }
        }
        if (!actor() && uiCurrentPartyAction != W8_PARTY_ACTION_NONE &&
            uiCurrentPartyActionStatus == W8_PARTY_ACTION_NOT_STARTED &&
            uiPartyActionPhase == round_counter) {
            int slot = 0;
            for (; slot < 8; ++slot) {
                if (IsPartySlotEligible(slot)) {
                    break;
                }
            }
            if (slot < 8) {
                BeginPartyMovementPhase();
            } else {
                game.events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x22a]});
                BeginFreeTurnPhase();
            }
            free_turn = true;
        }
        if (actor() || free_turn) {
            bool interruptible = false;
            bool apply_delay = false;
            if (!actor()) {
                apply_delay = true;
            } else if (const auto slot = actingSlot()) {
                W8TargetingContext context;
                W8ActionKind action;
                if (!characters[*slot].dead) {
                    action = game.party.rows[*slot].committed.kind;
                    context = W8_TARGETING_CONTEXT_IN_COMBAT;
                } else {
                    action = game.party.rows[*slot].executing.kind;
                    context = W8_TARGETING_CONTEXT_OUT_OF_COMBAT;
                }
                if ((action == W8_ACTION_DEFEND || action == W8_ACTION_PROTECT ||
                     action == W8_ACTION_NONE) &&
                    (g_settings.continuous_combat == 0 ||
                     CharacterCanSwitchTo(*slot, context, false, false))) {
                    interruptible = true;
                }
                if ((g_settings.continuous_combat != 0 ||
                     CharacterCanSwitchTo(*slot, context, false, false)) &&
                    !interruptible) {
                    apply_delay = true;
                }
            } else {
                W8MonsterActionKind action_kind = FindMonster(*actingMonster())->action_kind;
                if (action_kind == W8_MONSTER_ACTION_WAIT ||
                    action_kind == W8_MONSTER_ACTION_PROTECT ||
                    action_kind == W8_MONSTER_ACTION_NONE) {
                    interruptible = true;
                }
                if (!pacing_latch && !interruptible) {
                    apply_delay = true;
                }
            }
            if (apply_delay && CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 &&
                IsLevelMovementStopped()) {
                unsigned int delay = g_settings.combat_delay_ms;
                if (pacing_latch && (g_settings.continuous_combat == 0 || !free_turn) &&
                    delay > 0x31f) {
                    delay = 0x320;
                }
                action_delay = std::chrono::milliseconds{delay};
            }
            pacing_latch = false;
        } else {
            if (uiCurrentPartyActionStatus == W8_PARTY_ACTION_PHASE_ENDED) {
                int slot = 0;
                for (; slot < 8; ++slot) {
                    if (IsPartySlotEligible(slot)) {
                        break;
                    }
                }
                if (slot < 8) {
                    FinishPartyMovementAction();
                } else {
                    game.events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x22a]});
                    BeginFreeTurnPhase();
                }
            }
            if (!game.party_movement_mode || PartyMovementReachedPhaseLimit()) {
                round_counter += GetPhaseStep();
            }
        }
        if (actor() || game.party_movement_mode || free_turn || round_counter > 0x64) {
            if (round_counter > 0x64) {
                equip_phase = W8CombatEquipPhase::action_selection;
                equip_pending = 0;
                for (unsigned int slot = 0; slot < 8; ++slot) {
                    if (game.party.members[slot].has_value() &&
                        game.party.characters[slot].hp_current != 0 &&
                        game.party.characters[slot].highest_condition < W8_CONDITION_TURNCOAT &&
                        game.party.rows[slot].executing.kind == W8_ACTION_EQUIP) {
                        ++equip_pending;
                        if (equip_phase == W8CombatEquipPhase::action_selection) {
                            equip_phase = W8CombatEquipPhase::resolving;
                            OpenCharacterScreenForPartySlot(slot, false);
                        }
                    }
                }
                if (equip_phase == W8CombatEquipPhase::action_selection) {
                    AdvanceCombatRound();
                    if (CheckCombatEnd(1) == 0 && g_settings.continuous_combat != 0) {
                        BeginCombatExecution();
                    }
                }
            }
            return;
        }
    }
}

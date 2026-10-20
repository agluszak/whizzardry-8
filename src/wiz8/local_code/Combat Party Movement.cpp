#include <algorithm>
#include <optional>
#include <vector>

#include "layouts/game_status.h"
#include "local_code/Targeting.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_screens/MGSPartyMovement.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/load_category.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"

/* PHASES_PER_ROUND, named by the assertion that bounds the phase. */
enum { W8_PHASES_PER_ROUND = 100 };

constexpr float kMovementFatigueTickScale = 0.0004f;

namespace {
/* The movement gauge: how far the party may move this action, and how far
   it has moved. The update converts them into the panel's percentage. */
float party_move_limit = 0.0f;
float party_move_distance = 0.0f;

void ArmPartyMoveGauge(bool running)
{
    party_move_limit = GetPartyMovementSpeed();
    ResetLevelMovement(party_move_limit, false, running);
}
} // namespace

void ResetRoundPartyMovement()
{
    const auto action = CurrentGame().combat->uiCurrentPartyAction;
    if (action == W8_PARTY_ACTION_WALK || action == W8_PARTY_ACTION_RUN) {
        ArmPartyMoveGauge(action == W8_PARTY_ACTION_RUN);
    } else {
        party_move_limit = 0.0f;
        ResetLevelMovement(party_move_limit, true, false);
    }
}

/* Note that the party has started moving. */
void BeginPartyMovement(void)
{
    CurrentGame().party_moving = true;
    MoveTimer(W8_MOVE_TIMER_SUSPEND);
}

/* Round one combatant's phase to the ten it belongs in and clamp it into the
   round. The assertion names the bound as PHASES_PER_ROUND. */
void RoundPhaseToStep(unsigned int* phase, unsigned int base)
{
    if (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE) {
        *phase = (*phase + 5) - (*phase + 5) % 10;
        if (base % 10 != 0) {
            base += 10 - base % 10;
        }
        *phase =
            *phase > W8_PHASES_PER_ROUND ? W8_PHASES_PER_ROUND : std::max<unsigned>(*phase, base);
    }
    if (*phase == 0 || *phase > W8_PHASES_PER_ROUND) {
        Fail("( *puiPhase > 0 ) && ( *puiPhase <= PHASES_PER_ROUND )");
    }
}

/* Movement fatigue: each occupied party slot with stamina and a bearable load
   accumulates distance scaled by load, doubled in combat and raised again while
   diseased, until the accumulator crosses 2500 and converts into real fatigue.
   Returns the elapsed times the level's motion consumed, or nothing when the
   party did not move. */
std::optional<PartyMotionElapsed> AccumulateMovementFatigue(Game& game)
{
    PartyMotionElapsed elapsed;
    if (ConsumeLevelElapsedTime(&elapsed.real, &elapsed.frame) == 0) return std::nullopt;
    for (unsigned int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow& row = game.party.rows[party_slot];
        const W8Character& character = game.party.characters[party_slot];
        if (!row.fOccupied || character.stamina <= 0 ||
            character.highest_condition >= W8_CONDITION_ASLEEP) {
            continue;
        }
        float amount = elapsed.frame;
        if (game.status.search_mode || game.combat.has_value())
            amount = elapsed.real * 0.5f + amount;
        float multiplier = 1.0f;
        switch (character.load_category) {
        case W8_LOAD_NONE:
            multiplier = 1.0f;
            break;
        case W8_LOAD_LIGHT:
            multiplier = 1.25f;
            break;
        case W8_LOAD_MEDIUM:
            multiplier = 1.5f;
            break;
        case W8_LOAD_HEAVY:
            multiplier = 2.0f;
            break;
        case W8_LOAD_EXTREME:
            multiplier = 3.0f;
            break;
        default:
            Fail("FALSE", "HandlePartyMovement: ERROR - Invalid load category");
        }
        if (game.combat.has_value()) multiplier += multiplier;
        if (character.uiCondition[W8_CONDITION_DISEASED] != 0) multiplier *= 1.5f;
        row.movement_fatigue = multiplier * amount + row.movement_fatigue;
        if (row.movement_fatigue > g_position_height_epsilon) {
            const auto ticks =
                static_cast<unsigned int>(row.movement_fatigue * kMovementFatigueTickScale);
            FatigueCharacter(static_cast<int>(party_slot), static_cast<int>(ticks), false, 0);
            row.movement_fatigue = row.movement_fatigue - (ticks * 0x9c4);
        }
    }
    return elapsed;
}

/* Advance the movement gauge from real world motion. Continuous-combat mode
   supplies a bounded synthetic step while the world is stationary. Emptying
   the gauge completes the action and synchronizes every combatant/UI owner. */
void UpdateActivePartyMovement(void)
{
    auto& combat = *CurrentGame().combat;
    bool moved = true;
    if (const auto elapsed = AccumulateMovementFatigue(CurrentGame())) {
        party_move_distance += elapsed->real + elapsed->frame;
    } else if (g_settings.continuous_combat != 0 &&
               TimeUntilDeadline(combat.party_movement_clock) == 0) {
        unsigned int step_count = g_settings.combat_delay_ms / 200 + 10;
        step_count = std::clamp(step_count, 10u, 60u);
        party_move_distance += party_move_limit / step_count;
    } else {
        moved = false;
    }

    if (moved) {
        if (party_move_limit <= 0.0f) {
            Fail("party_move_limit > 0.0f");
        }
        const int percent =
            static_cast<int>(100.0f - party_move_distance * 100.0f / party_move_limit + 0.5);
        combat.party_move_percent = std::clamp(percent, 0, 100);
        ShowPartyMovementGauge(false);
        if (g_settings.continuous_combat != 0) {
            combat.party_movement_clock = MonotonicMilliseconds() + (1000);
        }
    }

    if (combat.party_move_percent < 1) {
        BeginFreeTurnPhase();
    }
}

/* One or ten, depending on whether the party is moving under combat rules -
   the step the phase counter advances by. */
int GetPhaseStep(void)
{
    return CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE ? 10 : 1;
}

/* Note what kind of move is pending. Only one value is singled out; everything
   else counts as the other kind. */
void SetPendingMoveKind(W8ActionKind kind)
{
    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    CurrentGame().combat->uiNextPartyAction =
        kind == W8_ACTION_WALK ? W8_PARTY_ACTION_WALK : W8_PARTY_ACTION_RUN;
}

/* Drop a queued party move and rebuild every other switchable character's
   combat target. The excluded slot is the character currently being edited;
   -1 refreshes the whole party. */
void ClearPendingPartyMovement()
{
    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    CurrentGame().combat->uiNextPartyAction = W8_PARTY_ACTION_NONE;
    UpdatePartyMovementControl();
}

/* Hand movement back to the party, or take it away and fill both budgets. The
   party has its own movement only out of combat mode, or in the free phase,
   and only with nothing pending. */
void UpdatePartyMovementControl(void)
{
    if ((CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
         CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) &&
        CurrentGame().combat->uiNextPartyAction == W8_PARTY_ACTION_NONE) {
        ReleasePartyMovement();
        return;
    }
    CurrentGame().combat->party_move_percent = 100;
    ShowPartyMovementGauge(true);
}

/* Whether the party may move at all right now. Out of combat mode, or in the
   free phase, it comes down to whether a move is already pending; in the
   opening phase it is always allowed. */
bool CanPartyMove(void)
{
    W8PartyActionStatus status;

    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    status = CurrentGame().combat->uiCurrentPartyActionStatus;
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
        status == W8_PARTY_ACTION_FINISHED) {
        if (CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_NONE) {
            return true;
        }
        return false;
    }
    if (status == W8_PARTY_ACTION_NOT_STARTED) {
        return true;
    }
    return false;
}

/* Average the haste bonus over the party: every occupied slot must either be
   free of the sixth condition or carry the sixth enchantment, else there is no
   bonus at all. Each contributing slot adds ten steps per point of power,
   adjusted by the enchantment's percentage. */
std::optional<unsigned int> GetPartyHasteSteps()
{
    unsigned int total = 0;
    unsigned int count = 0;
    unsigned int party_slot;
    unsigned int steps;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        W8Character* character;

        if (!CurrentGame().party.rows[party_slot].fOccupied) {
            continue;
        }
        character = &CurrentGame().party.characters[party_slot];
        if (character->uiCondition[W8_CONDITION_MISSING] != 0) {
            continue;
        }
        if (character->enchantments[W8_ENCHANTMENT_HASTE].turns == 0) {
            return std::nullopt;
        }
        steps =
            static_cast<unsigned char>(character->enchantments[W8_ENCHANTMENT_HASTE].power * 10);
        AdjustIntegerByPercent(&steps, character->enchantments[W8_ENCHANTMENT_HASTE].percent);
        total += steps;
        ++count;
    }
    return count == 0 ? std::nullopt : std::optional<unsigned int>(total / count);
}

/* How fast the party moves. The second combat mode is half again as fast, and
   whatever the step count adds is scaled by the same world constant. */
float GetPartyMovementSpeed(void)
{
    float speed = 1.0f;
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
        speed = 1.5f;
    }
    if (const auto steps = GetPartyHasteSteps()) {
        return (*steps * kMovementSpeedStep + speed) * 1e+04f;
    }
    return speed * 1e+04f;
}

/* Reconcile character turns after the party finishes moving. Characters that
   pass the remaining-movement roll catch up to the combat clock; the rest are
   marked finished for this round and have their queued portrait event reset. */
void CompletePartyMovementTurns(void)
{
    const int percent = CurrentGame().combat->party_move_percent;
    if (percent < 0) {
        Fail("gpMGSV->iPartyMovementPercent >= 0");
    }
    if (percent > 100) {
        Fail("gpMGSV->iPartyMovementPercent <= 100");
    }

    unsigned int remaining = 100 - percent;
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
        remaining = static_cast<unsigned int>(remaining * 1.5f);
    }
    remaining = remaining < 100 ? 100 - remaining : 0;

    for (int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* party_row = &CurrentGame().party.rows[party_slot];
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (!party_row->fOccupied || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_ASLEEP) {
            continue;
        }
        W8CombatCharacterRow* combat_row = &CurrentGame().combat->characters[party_slot];
        if (!combat_row->dead && CurrentGame().rng.below(100) < remaining) {
            CatchUpCombatActor(combat_row);
            continue;
        }
        combat_row->dead = true;
        combat_row->phase = 0;
        party_row->pending_event_type = static_cast<unsigned int>(-1);
    }
    CurrentGame().events.push(CombatProgressChanged{.whole_party = true});
}

/* Choose the party's phase from the best living character initiative, with
   the retail four-point random tie break. Party movement uses ten-step phase
   boundaries. */
void InitializePartyMovementPhase(void)
{
    int minimum_initiative = 90;

    for (int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (row->fOccupied && character->hp_current != 0) {
            int initiative = character->initiative + CurrentGame().rng.below(4);
            if (initiative < minimum_initiative) {
                minimum_initiative = initiative;
            }
        }
    }
    minimum_initiative = std::clamp(minimum_initiative, -10, 89);
    CurrentGame().combat->uiPartyActionPhase = 90 - minimum_initiative;
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_WALK ||
        CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
        RoundPhaseToStep(&CurrentGame().combat->uiPartyActionPhase, 10);
    }
    if (CurrentGame().combat->uiPartyActionPhase == 0 ||
        CurrentGame().combat->uiPartyActionPhase > W8_PHASES_PER_ROUND) {
        Fail("( gpCombat->uiPartyActionPhase > 0 ) && "
             "( gpCombat->uiPartyActionPhase <= PHASES_PER_ROUND )");
    }
}

/* Bring every not-yet-active combatant onto the ten-step schedule used by a
   party movement action. */
void AlignCombatantsToPartyMovementPhase(void)
{
    RoundPhaseToStep(&CurrentGame().combat->uiPartyActionPhase,
                     CurrentGame().combat->round_counter);
    for (auto& character : CurrentGame().combat->characters) {
        W8CombatCharacterRow* row = &character;
        if (row->phase >= CurrentGame().combat->round_counter && !row->dead) {
            RoundPhaseToStep(&row->phase, CurrentGame().combat->round_counter);
        }
    }
    auto monster_count = CurrentLevelPopulation().monster_roster.live().size();
    for (unsigned int monster_index = 0; monster_index < monster_count; ++monster_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
        if (monster_info->pCombat &&
            monster_info->pCombat->phase >= CurrentGame().combat->round_counter &&
            !monster_info->pCombat->active) {
            RoundPhaseToStep(&monster_info->pCombat->phase, CurrentGame().combat->round_counter);
        }
        monster_count = CurrentLevelPopulation().monster_roster.live().size();
    }
}

/* Start one of the two party movement actions and synchronize the world,
   party and monster combat clocks around its movement limit. */
void StartPartyMovementAction(W8PartyAction move_kind)
{
    if (move_kind == W8_PARTY_ACTION_NONE) {
        Fail("uiPartyAction != PARTY_ACTION_NONE");
    }
    if (CurrentGame().combat->uiCurrentPartyAction == move_kind) {
        return;
    }

    CurrentGame().combat->uiCurrentPartyAction = move_kind;
    CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_NOT_STARTED;
    ArmPartyMoveGauge(move_kind == W8_PARTY_ACTION_RUN);
    InitializePartyMovementPhase();
    CurrentGame().combat->uiPartyActionPhase += CurrentGame().combat->round_counter;
    if (CurrentGame().combat->uiPartyActionPhase > W8_PHASES_PER_ROUND) {
        CurrentGame().combat->uiPartyActionPhase = W8_PHASES_PER_ROUND;
    }
    AlignCombatantsToPartyMovementPhase();
    RoundPhaseToStep(&CurrentGame().combat->round_counter, CurrentGame().combat->round_counter);
    CurrentGame().events.push(CombatProgressChanged{});
}

/* End the party's movement phase. Outside the two combat modes there is
   nothing to unwind; inside them the notice is posted unless the level says
   otherwise, and everything that watched the party move is told. */
void EndPartyMovementPhase(void)
{
    if (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_WALK &&
        CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_RUN) {
        CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_PHASE_ENDED;
        return;
    }
    if (!IsLevelMovementStopped()) {
        ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x21c]);
    }
    ResetLevelDataVectors();
    DisableFreeTurnButton();
    InvalidatePartyMovementPanel();
    RefreshOutwardSightForAllMonsters();
    CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_PHASE_ENDED;
}

/* Enter the free phase: everything that was waiting on the party is released,
   the monsters are told, and movement control is settled. */
void BeginFreeTurnPhase(void)
{
    ResetLevelDataVectors();
    CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_FINISHED;
    CurrentGame().party_movement_mode = false;
    CheckMonsterGroupsEnterCombat();
    CompletePartyMovementTurns();
    NotifyNearbyMonsters(0);
    UpdatePartyMovementControl();
}

/* Cancel the pending movement action. An action that is already entering its
   first phase is handed to the phase-settling path; otherwise the queued
   action is cleared and every eligible party target highlight is rebuilt. */
void CancelPartyMovement(void)
{
    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    if (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE &&
        CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_FINISHED) {
        if (CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_NOT_STARTED) {
            InterruptActivePartyMovement();
        }
        return;
    }
    if (CurrentGame().combat->uiNextPartyAction == W8_PARTY_ACTION_NONE) {
        return;
    }
    ClearPendingPartyMovement();
}

/* Interrupt a move that has entered its first phase. Bring every unfinished
   party member forward to the combat clock, then retire any phase that moved
   beyond this round before handing movement control back to the UI. */
void InterruptActivePartyMovement(void)
{
    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    CurrentGame().combat->uiCurrentPartyAction = W8_PARTY_ACTION_NONE;
    for (int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* party_row = &CurrentGame().party.rows[party_slot];
        if (!party_row->fOccupied) {
            continue;
        }
        W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
        if (row->phase < CurrentGame().combat->round_counter && !row->dead) {
            row->phase = CurrentGame().combat->round_counter;
        }
        row->phase_clock_stamp = CurrentGame().combat->round_counter;
        if (row->phase > W8_PHASES_PER_ROUND) {
            row->dead = true;
            row->phase = 0;
            party_row->pending_event_type = static_cast<unsigned int>(-1);
            CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
            CurrentGame().events.push(CombatProgressChanged{});
        }
    }
    UpdatePartyMovementControl();
}

void FinishPartyMovementAction(void)
{
    if (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_WALK &&
        CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_RUN) {
        CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_IN_PROGRESS;
        return;
    }
    if (IsLevelMovementStopped() &&
        CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_PHASE_ENDED) {
        ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x21d]);
    }
    wiz8::sound::play("Data\\Sound\\Misc\\Movement_Bar_Pop_Up.wav");
    ClearLevelMovementStopped();
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
        SetLevelFastMovement();
    }
    EnableFreeTurnButton();
    InvalidatePartyMovementPanel();
    if (g_settings.continuous_combat != 0) {
        CurrentGame().combat->party_movement_clock = MonotonicMilliseconds() + (1000);
    }
    TurnPartyTo(CurrentGame().status.party_heading);
    CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_IN_PROGRESS;
}

void BeginPartyMovementPhase(void)
{
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_WALK ||
        CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_RUN) {
        CurrentGame().party_movement_mode = true;
        ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x21b]);
        party_move_distance = 0.0f;
    }
    FinishPartyMovementAction();
    CurrentGame().combat->passive_round = 0;
}

bool PartyMovementReachedPhaseLimit(void)
{
    if (CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_IN_PROGRESS) {
        Fail("gpCombat->uiCurrentPartyActionStatus == ACTION_STATUS_IN_PROGRESS");
    }
    if (!CurrentGame().party_movement_mode) {
        Fail("CurrentGame().party_movement_mode");
    }
    if (party_move_limit <= 0.0f) {
        Fail("party_move_limit > 0");
    }
    float moved_fraction = party_move_distance / party_move_limit;
    int remaining_phases = 101 - CurrentGame().combat->uiPartyActionPhase;
    if (remaining_phases == 0) {
        Fail("uiTotalMovementPhases > 0");
    }
    unsigned int elapsed_phases =
        CurrentGame().combat->round_counter - CurrentGame().combat->uiPartyActionPhase +
        (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE ? 9 : 0) + 2;
    return static_cast<float>(elapsed_phases) / remaining_phases <= moved_fraction;
}

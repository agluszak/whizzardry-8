#pragma once

#include <optional>

#include "wiz8/rules/targeting.h"

class Game;

// The real and frame time the level's party motion consumed in one step.
struct PartyMotionElapsed {
    float real = 0.0f;
    float frame = 0.0f;
};

void SetPendingMoveKind(W8ActionKind kind);
std::optional<unsigned int> GetPartyHasteSteps();
void CompletePartyMovementTurns(void);
void BeginFreeTurnPhase(void);
void BeginPartyMovement(void);
void EndPartyMovementPhase(void);
void CancelPartyMovement(void);
void InterruptActivePartyMovement(void);
bool CanPartyMove(void);
void ClearPendingPartyMovement();
void StartPartyMovementAction(W8PartyAction move_kind);
/* A new round: arm the movement gauge for a pending walk or run, otherwise
   stop level movement. */
void ResetRoundPartyMovement(void);
/* Convert the party's motion since the last step into movement fatigue; nothing when
   the party did not move. */
std::optional<PartyMotionElapsed> AccumulateMovementFatigue(Game& game);
void UpdateActivePartyMovement(void);
void UpdatePartyMovementControl(void);

int GetPhaseStep(void);
float GetPartyMovementSpeed(void);
void BeginPartyMovementPhase(void);
void FinishPartyMovementAction(void);
bool PartyMovementReachedPhaseLimit(void);
/* Combat.cpp calls it when a party movement action is pending
   while phases are assigned, so it is not file-local. */
void InitializePartyMovementPhase(void);

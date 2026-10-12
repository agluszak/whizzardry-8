#pragma once

#include <cstdint>
#include <optional>

#include "wiz8/layouts/targeting.h"

void SetPendingMoveKind(W8ActionKind kind);
std::optional<std::uint32_t> GetPartyHasteSteps();
void CompletePartyMovementTurns(void);
void BeginFreeTurnPhase(void);
void BeginPartyMovement(void);
void EndPartyMovementPhase(void);
void CancelPartyMovement(void);
void InterruptActivePartyMovement(void);
bool CanPartyMove(void);
void ClearPendingPartyMovement(std::int32_t excluded_party_slot);
void StartPartyMovementAction(W8PartyAction move_kind);
void UpdateActivePartyMovement(void);
void UpdatePartyMovementControl(void);
void RoundPhaseToStep(std::uint32_t* phase, std::uint32_t base);
std::int32_t GetPhaseStep(void);
float GetPartyMovementSpeed(void);
void BeginPartyMovementPhase(void);
void FinishPartyMovementAction(void);
bool PartyMovementReachedPhaseLimit(void);
/* Combat.cpp calls it when a party movement action is pending
   while phases are assigned, so it is not file-local. */
void InitializePartyMovementPhase(void);

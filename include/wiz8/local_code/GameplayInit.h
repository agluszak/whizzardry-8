#pragma once

#include "wiz8/data/records.h"
#include "wiz8/layouts/game_status.h"

void ResetPartySlotRow(int slot);
void ResetTargetingState(void);
void ResetForNewGame(void);
void ResetGameplaySlot(unsigned int slot);
void ResetGameplaySettings(void);

void RunNewGameOpeningSequence(bool notify, std::string_view target);

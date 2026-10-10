#pragma once

#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"

void ResetGameStatus(bool release);
void InitializeGameplayRuntimeObjects(void);
unsigned char InitializeFactDatabase(void);
unsigned char InitializeLevelDatabase(void);
unsigned char InitializeNpcDatabase(void);
void DestroyFactDatabase(void);
void DestroyLevelDatabase(void);
void DestroyNpcDatabase(void);
void ResetPartySlotRow(int slot);
void ResetGameplayStatusBlock(void);
void ResetTargetingState(void);
void DestroyGameplayObjects(void);
void ResetForNewGame(void);
void ResetGameplaySlot(unsigned int slot);
void ResetGameplaySettings(void);

void RunNewGameOpeningSequence(bool notify, std::string_view target);

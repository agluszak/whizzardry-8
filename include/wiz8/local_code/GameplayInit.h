#pragma once

#include <cstdint>
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"

void ResetGameStatus(bool release);
void InitializeGameplayRuntimeObjects(void);
std::uint8_t InitializeFactDatabase(void);
std::uint8_t InitializeLevelDatabase(void);
std::uint8_t InitializeNpcDatabase(void);
void DestroyFactDatabase(void);
void DestroyLevelDatabase(void);
void DestroyNpcDatabase(void);
void ResetPartySlotRow(std::int32_t slot);
void ResetGameplayStatusBlock(void);
void ResetTargetingState(void);
void DestroyGameplayObjects(void);
void ResetForNewGame(void);
void ResetGameplaySlot(std::uint32_t slot);
void ResetGameplaySettings(void);

void RunNewGameOpeningSequence(bool notify, std::string_view target);

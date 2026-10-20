#pragma once

#include "wiz8/data/records.h"
#include "wiz8/layouts/game_status.h"

class Game;
class GameSession;

void ResetPartySlotRow(int slot);
void ResetTargetingState(void);
/* Begin a new game in party creation, replacing any in progress: the party starts with
   the starting items and gold and an empty marching order. */
Game& BeginNewGame(GameSession& games);
void ResetGameplaySlot(unsigned int slot);
void ResetGameplaySettings(void);

void RunNewGameOpeningSequence(bool notify, std::string_view target);

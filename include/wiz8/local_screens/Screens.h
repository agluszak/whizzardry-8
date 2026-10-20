#pragma once

#include <memory>
#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/rules/events.h"
#include "wiz8/screen.h"

class Game;

void RefreshPartySlotDisplay(unsigned int party_slot);
void InitializeMainGameLevelBlock(void);
void SetTargetCursor(int cursor);
void ApplyCurrentCursor(void);
void RequestPartySlotRedraw(int bit);
void RequestExitScreen(void);
std::unique_ptr<Screen> CreateExitScreen();
/* The game's screens by id, for the router. */
std::unique_ptr<Screen> CreateScreen(W8ScreenId id);
/* Dispatch one already-built line to the active screen. */
void ShowNoticeLine(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel);
/* Show one game event on the active screen: the composition root's observer of every
   game's events. */
void PresentGameEvent(const Event& event);
/* Whether a skill increase is shown at once: only over the idle exploring main game.
   Otherwise it waits, and the main game shows the waiting ones once it is idle again. */
bool SkillIncreasesShownNow(const Game& game);

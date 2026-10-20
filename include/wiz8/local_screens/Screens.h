#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/rules/events.h"
#include "wiz8/screen.h"

class Game;
struct InventoryRefusal;

void RefreshPartySlotDisplay(unsigned int party_slot);
struct W8LevelRuntimeBlock;
// Default the main game's HUD as the main game is entered.
void InitializeMainGameLevelBlock(W8LevelRuntimeBlock& hud);
/* What a screen asked the pointer to show. A request lapses when the hand changes; without
   one, the main game and the camp show the held item and other screens the arrow. */
struct PointerRequest {
    enum class Kind { None, Slot, HeldItem, Custom };
    Kind kind = Kind::None;
    // The cursor-table slot, or the catalog overlay drawn over the held item.
    int value = -1;
    // The held item's id when the request was made.
    int hand = -1;
};
/* Ask for a main-game cursor-table slot; W8_CURSOR_NONE drops the request. An unchanged
   slot keeps its animation. */
void SetTargetCursor(int cursor);
// Drop whatever cursor a screen asked for.
void ResetPointerCursor();
// Show the held item under a catalog overlay (the camp's move and swap cursors).
void ShowHeldItemCursor(int overlay);
// A screen drew its own image (a formation drag, an automap tool) into the pointer.
void ShowCustomCursor();
/* The main-game cursor-table slot the pointer shows: W8_CURSOR_NONE for the default
   arrow, W8_CURSOR_INVALID_TARGET for the held item or a custom image. */
int ShownCursor();
// The current request, to restore once a dialog closes.
PointerRequest CurrentPointerRequest();
void RestorePointerRequest(const PointerRequest& request);
// Bring the pointer up to date with the hand and the request; runs every frame.
void PresentPointer();
// Step a multi-frame cursor's animation when its frame is due.
void AnimateShownCursor();
void RequestPartySlotRedraw(int bit);
void RequestExitScreen(void);
std::unique_ptr<Screen> CreateExitScreen();
/* The game's screens by id, for the router. */
std::unique_ptr<Screen> CreateScreen(W8ScreenId id);
/* Dispatch one already-built line to the active screen. */
void ShowNoticeLine(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel);
/* Tell the player why the active screen's inventory command was turned down. */
void ShowRefusal(const InventoryRefusal& refusal);
/* Show one game event on the active screen: the composition root's observer of every
   game's events. */
void PresentGameEvent(const Event& event);
/* Whether a skill increase is shown at once: only over the idle exploring main game.
   Otherwise it waits, and the main game shows the waiting ones once it is idle again. */
bool SkillIncreasesShownNow(const Game& game);

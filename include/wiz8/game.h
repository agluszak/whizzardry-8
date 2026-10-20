#pragma once

#include <functional>

#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"

/* One play-through, from choosing a new game or loading one until the player quits to
   the menu. Starting or loading a game replaces the whole object, so nothing is reset
   field by field. Its members keep their retail-era types for now; it moves into the
   `game` layer as they move into the lower layers (ROADMAP.md, "Migration"). */
class Game {
public:
    Game() = default;
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    W8Party party{};
    W8GlobalStatus status{};
    W8CombatLifecycle combat;
    // Voice barks and quotes waiting for, or holding, a speaker.
    W8CharacterEventQueue speech;
    W8GameTimer gameplay_timer{300.0f, false};
    W8MonsterManagerEntry monster_manager_entries[8]{};
    // A camp or rest may be interrupted by an ambush.
    bool surprise_possible = false;
    // The combat movement phase: the party walks to a new position before acting.
    bool party_movement_mode = false;
    bool party_moving = false;
    /* One-shot latch set when the combat and party panels refresh; the next
       UpdateMonsterSight pass consumes it. */
    bool sight_refresh_pending = false;
    /* A container offered more than one item, so the item-choice dialog is pending;
       blocks the magic-effects tick until it opens. */
    bool item_pick_pending = false;
    int item_manager_pending = 0;
    // What happened, in order, for the UI to present.
    Events events;
};

/* The game in progress. Transitional: code that does not receive the game yet reaches
   it here; `rules` and `game` may not, and this goes with its last caller. Throws
   std::logic_error when no game is in progress. */
Game& CurrentGame();
bool GameInProgress();
// A game is in progress and the party has left party creation.
bool GameStarted();
/* Begin a new game, ending any in progress. The new game's events go to the observer
   the UI installed. */
Game& BeginGame();
void EndGame();
/* The composition root's presenter for every game's events; a game begun before one is
   installed keeps its events pending. */
void SetGameEventObserver(std::function<void(const Event&)> observer);

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/message_log.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/story.h"

struct World;

/* The worlds the party is in: the loaded level and the sky drawn behind it. Each slot
   owns its world; queries borrow it. The level's entities report to combat. Replacing a slot hands back the previous owner,
   so a world is never reachable from its slot while it is destroyed. */
struct Level {
    Level();
    ~Level();
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;

    std::unique_ptr<World> replacePrimary(std::unique_ptr<World> world);
    std::unique_ptr<World> replaceSky(std::unique_ptr<World> world);

    std::unique_ptr<World> primary;
    std::unique_ptr<World> sky;
};

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

    /* One step of the game over `elapsed`: it applies the interface's hold, runs speech
       and, in the world, the simulation in retail order: surprise, autosave pacing, the
       worlds' props, monsters, movement fatigue and the search sweep or the combat turn,
       combat starts, sight, groups, nearby world items and the worlds themselves. The
       main game and the camp step the game between reading input and drawing, passing
       the time since their previous step; `hold` says what of it the interface holds. */
    struct StepHold {
        // What of the world the interface holds (`ApplyWorldAdvancement`).
        WorldHold world;
        // A spell or item picker is open, so the combat turn waits.
        bool combat = false;
        // The party's portraits are not shown, so speech waits.
        bool speech = false;
    };
    void advance(std::chrono::milliseconds elapsed, StepHold hold);

    W8Party party{};
    GameStatus status{};
    Story story;
    W8MessageLog message_log;
    W8CombatLifecycle combat;
    /* What the party is doing. The camp commands set the place; dialogue and story scenes
       set the rest. */
    Activity activity;
    /* Spells still taking effect: casts whose missiles are in flight, and the
       sustained monster control the HYPN section saves. */
    std::vector<std::unique_ptr<W8SpellEffectEntry>> spell_effects;
    // Voice barks and quotes waiting for, or holding, a speaker.
    W8CharacterEventQueue speech;
    W8GameTimer gameplay_timer{300.0f, false};
    W8MonsterManagerEntry monster_manager_entries[8]{};
    /* The marching order being planned. The formation panel stages its edits here, and in
       a fight the party takes it when the round ends. */
    W8PartyFormationState planned_formation{};
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

    /* Whether the world moves on. Each step applies what the interface holds
       (`ApplyWorldAdvancement`); rules read the result. */
    WorldAdvancement advancement;
    /* No turns pass: the camp's Use handed an item to the main game's use-item picker,
       which has not taken it yet. The screens set it. */
    bool aging_held = false;
    SurpriseSequence surprise;
    /* The step time left until the next search sweep, and until monster sight and the
       nearby world items are next refreshed. */
    std::chrono::milliseconds search_sweep_due{};
    std::chrono::milliseconds sight_due{};
    std::chrono::milliseconds world_items_due{};
    /* Experience was awarded or the main game entered, so the level-up-ready notices are
       checked again once the interface is idle. */
    bool level_up_check_due = false;
    // The timed autosave has announced itself and saves on its next tick.
    bool save_notice_shown = false;

    // What happened, in order, for the UI to present.
    Events events;
    /* Last, so its worlds are destroyed first, while combat and speech still exist.
       Leaving the level (`LeaveGameSession`) has already emptied it when a game ends
       in play; destroying a world does no departure bookkeeping. */
    Level level;
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

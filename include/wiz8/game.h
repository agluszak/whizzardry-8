#pragma once

#include <cstdint>
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
#include "wiz8/story.h"

struct World;

/* The worlds the party is in: the loaded level and the sky drawn behind it. Each slot
   owns its world; queries borrow it. Replacing a slot hands back the previous owner,
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

    /* One simulation step, in retail order: surprise, autosave pacing, the worlds'
       props, monsters, movement fatigue and the search sweep or the combat turn,
       combat starts, sight, groups, nearby world items and the worlds themselves.
       The main game screen steps the game between reading input and drawing;
       `hold` says what of it the interface holds this step. */
    struct StepHold {
        // A picker or level transition is open, so the combat turn waits.
        bool combat = false;
    };
    void advance(StepHold hold);

    W8Party party{};
    W8GlobalStatus status{};
    Story story;
    W8MessageLog message_log;
    W8CombatLifecycle combat;
    /* What the party is doing. The screen router sets the place as the world, the camp or
       another screen becomes current; dialogue and story scenes set the rest. */
    Activity activity;
    /* Spells still taking effect: casts whose missiles are in flight, and the
       sustained monster control the HYPN section saves. */
    std::vector<std::unique_ptr<W8SpellEffectEntry>> spell_effects;
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

    /* Whether the world moves on. The main game screen reports what holds it
       (`ApplyWorldAdvancement`); rules read the result. */
    WorldAdvancement advancement;
    /* No turns pass: the camp's Use handed an item to the main game's use-item picker,
       which has not taken it yet. The screens set it. */
    bool aging_held = false;
    SurpriseSequence surprise;
    // When the next search sweep is due.
    std::uint64_t search_pulse_deadline = 0;
    // When monster sight and the nearby world items are next refreshed.
    std::uint64_t sight_deadline = 0;
    std::uint64_t world_items_deadline = 0;
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

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/message_log.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/npc_speech.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/random.h"
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

    /* Start a fight with the monsters hostile to the party; `surprise` says the party
       may be ambushed. Declines (returns false) while no member can fight, the party has
       no footing, or a hostile NPC's combat-start script is still owed. */
    bool beginCombat(int surprise);
    /* End the fight: award its experience, restore the formation and the world, and
       destroy the combat. A forced end (a departure or a scripted end) neither announces
       nor kills the summoned monsters. */
    void endCombat(bool forced_cleanup);

    // Commands the level's monster scripts and triggers issue.
    // A monster's script turns the group of the monster at `location_id` to `disposition`.
    void setMonsterGroupDisposition(int location_id, W8Disposition disposition);
    /* A monster's script starts the golem attack: two golem groups turn hostile, and the
       second joins the fight. */
    void startGolemAttack();
    /* A trap or fountain casts `spell` at `position`. Heal All also lifts every condition
       from the party. */
    void castTriggerSpell(glm::vec3 position, W8SpellId spell, unsigned int power_level);

    W8Party party{};
    GameStatus status{};
    /* Every gameplay draw, party creation's included. A game continues the stream of the
       game it replaces or that ended before it, so a session draws one sequence from one
       seed, as retail did; loot and NPC theft reseed it from saved values. */
    Rng rng;
    Story story;
    W8MessageLog message_log;
    // The fight in progress, if any.
    std::optional<Combat> combat;
    /* What the party is doing. The camp commands set the place; dialogue and story scenes
       set the rest. */
    Activity activity;
    /* Spells still taking effect: casts whose missiles are in flight, and the
       sustained monster control the HYPN section saves. */
    std::vector<std::unique_ptr<W8SpellEffectEntry>> spell_effects;
    // Voice barks and quotes waiting for, or holding, a speaker.
    W8CharacterEventQueue speech;
    // The NPC conversation's progress: its speaker, queued script work and owed reply.
    W8NpcDialogue npc_dialogue;
    /* The conversation's interface: layouts, trade tables, the saved transcript and the
       staged script notice. */
    W8NpcInteractionState npc_interaction;
    // The quote or portrait message on screen and the NPC voice that speaks it.
    W8NpcPresentation npc_presentation = W8NpcPresentation::none;
    W8NpcSpeech npc_speech;
    W8SedexusScene sedexus_scene;
    // The deadline of each shared three-minute spell cooldown (`g_cooldown_gated_spells`).
    std::array<std::uint64_t, 14> spell_cooldowns{};
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
       The main game leaves the level when it is discarded, before a game that ends in
       play is destroyed; destroying a world does no departure bookkeeping. */
    Level level;
};

/* The games of one run of the program: the game in progress, if any, and the random
   stream the next game continues, so a run draws one sequence from one seed, as retail
   did. The Application owns it, and the router hands it to the screens that begin and
   end games. One exists at a time; while it does, `CurrentGame()` reads its game. */
class GameSession {
public:
    GameSession();
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // The game in progress, or null.
    Game* game() { return game_ ? &*game_ : nullptr; }
    /* Begin a new game, ending any in progress. It continues the previous game's random
       stream, and its events go to the observer. */
    Game& begin();
    // End the game in progress, if any; the next game continues its random stream.
    void end();
    /* Seed the random stream of the game in progress, or of the next game when none is.
       The stream starts from the clock. */
    void seed(std::uint32_t seed);
    /* The composition root's presenter for every game's events; a game begun before one
       is installed keeps its events pending. */
    void observe(std::function<void(const Event&)> observer);

private:
    std::optional<Game> game_;
    Rng next_stream_;
    std::function<void(const Event&)> observer_;
};

/* The game in progress. Transitional: code that does not receive the game yet reaches
   it here; `rules` and `game` may not, and this goes with its last caller. Throws
   std::logic_error when no game is in progress. */
Game& CurrentGame();
bool GameInProgress();
// A game is in progress and the party has left party creation.
bool GameStarted();

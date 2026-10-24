#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/npc_ids.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_state.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/message_log.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/save_codec.h"
#include "wiz8/story.h"

class NativeObject;
struct GameData;
struct SavedGame;
struct World;
struct W8WorldItem;
struct W8MonsterInfo;

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
    // The loaded level; a screen that shows it is built only while one is loaded.
    World& loaded() const;

    std::unique_ptr<World> primary;
    std::unique_ptr<World> sky;
};

/* One play-through, from choosing a new game or loading one until the player quits to
   the menu. Starting or loading a game replaces the whole object, so nothing is reset
   field by field. Its members keep their retail-era types for now; it moves into the
   `game` layer as they move into the lower layers (ROADMAP.md, "Migration"). */
struct W8PreparedNpcBindings;

struct W8SpellEffectResult;

enum class ActionOrigin { Automatic, Player };

enum class DamageNotice { None, Standard, AmountOnly, Detailed };
struct DamageOptions {
    // Poison-style damage uses its own wording and does not wake sleeping members.
    bool poison = false;
    DamageNotice notice = DamageNotice::Standard;
};

struct TargetChoice {
    W8CombatSlot target;
    std::optional<W8WeaponSwap> equipment;
};

class Game {
public:
    // The game reads `data`, which its session's owner keeps alive longer than the game.
    explicit Game(const GameData& data) : data(data) {}
    ~Game();
    // Script fact tests can consume a one-shot latch; this is not a pure state query.
    unsigned char testFact(W8FactId fact);
    void setFact(W8FactId fact, unsigned char value, bool notify = true);
    void setFact(W8FactId fact_id, unsigned char value, bool notify, std::vector<Event>& outcomes);
    void seedNewGameFacts();
    void reactToItem(W8ItemInstance& item, W8Character* receiver = nullptr);
    void reactToItem(W8ItemInstance& item, W8Character* receiver, std::vector<Event>& outcomes);
    void acquireItemFacts(const W8ItemInstance& item);
    void acquireItemFacts(const W8ItemInstance& item, std::vector<Event>& outcomes);
    bool hasLeadingNpc(W8NpcId kind) const;
    // Settle the pending formation and publish its accepted or refused moves.
    void commitFormation();
    void reconcileCombatHands(unsigned int party_slot, std::vector<Event>& outcomes);
    unsigned int damageCharacter(unsigned int slot, unsigned int amount, DamageOptions options = {},
                                 W8SpellEffectResult* report = nullptr);
    unsigned int damageCharacter(unsigned int party_slot, unsigned int amount,
                                 DamageOptions options, W8SpellEffectResult* result_stats,
                                 std::vector<Event>& outcomes);
    void fatigueCharacter(unsigned int slot, int amount, bool scale_by_load,
                          W8SpellEffectResult* report = nullptr);
    void fatigueCharacter(unsigned int slot, int amount, bool scale_by_load,
                          W8SpellEffectResult* report, std::vector<Event>& outcomes);
    void aimStoredAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target);
    void aimStoredAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target,
                         std::vector<Event>& outcomes);
    W8RangeCategory actionRange(unsigned int slot, unsigned int hand, const W8Action& action,
                                std::optional<W8RangeCategory> attack_range = {}) const;
    W8TargetAssessment assessActionReach(unsigned int slot, unsigned int hand,
                                         const W8Action& action,
                                         std::optional<W8RangeCategory> attack_range = {},
                                         bool sighting = false) const;
    W8TargetNeed targetNeed(const W8Action& action, bool needs_item_target = true) const;
    bool targetInPlay(unsigned int slot, unsigned int hand, const W8Action& action) const;
    std::optional<TargetChoice> chooseTarget(unsigned int slot, const W8Action& action,
                                             bool allow_weapon_swap = true) const;
    std::optional<TargetChoice> chooseFallbackTarget(unsigned int slot,
                                                     const W8Action& action) const;
    bool repickActionTarget(unsigned int slot, W8TargetingContext stored, bool breathe = false);
    bool repickActionTarget(unsigned int slot, W8TargetingContext stored, bool breathe,
                            std::vector<Event>& outcomes);
    void commitWeaponChoice(unsigned int slot, W8WeaponSwap equipment);
    void commitWeaponChoice(unsigned int slot, W8WeaponSwap equipment,
                            std::vector<Event>& outcomes);
    bool canPerformAction(unsigned int slot, const W8Action& action,
                          bool allow_missing_target) const;
    bool canReactWith(unsigned int slot, W8ActionKind action) const;
    bool reactDefensively(unsigned int slot, W8ActionKind action);
    void pointCameraAtMonster(const W8MonsterInfo& monster, bool force, bool animate);
    void pointCameraAtPosition(const glm::vec3& position, bool force, bool animate);
    void pointCameraAtCombatTarget(const W8TargetSource* source, const W8CombatSlot* target);
    void setCombatAction(unsigned int slot, W8Action action,
                         ActionOrigin origin = ActionOrigin::Automatic, bool cue_ready = false);
    void setCombatAction(unsigned int slot, W8Action next, ActionOrigin origin, bool cue_ready,
                         std::vector<Event>& outcomes);
    void resetCombatChoice(unsigned int slot);
    void resetCombatChoice(unsigned int slot, std::vector<Event>& outcomes);
    bool targetPresent(const W8CombatSlot& target) const;
    bool targetMatches(const W8CombatSlot& target, W8TargetNeed needed) const;
    bool actionTargetsEnemies(unsigned int slot, const W8Action& action) const;
    void invalidateAimsAt(const W8CombatSlot& target, TargetChange change);
    void invalidateAimsAt(const W8CombatSlot& target, TargetChange change,
                          std::vector<Event>& outcomes);
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
    /* Replace the loaded level with `level_id` entered at `entrance`. The destination
       is prepared before departure; failure leaves the current world loaded. Reloading
       the current level through the -1 entrance restores the party's camera pose. */
    [[nodiscard]] bool travelToLevel(int level_id, int entrance);
    /* Enter the level a decoded save resumes at. */
    [[nodiscard]] bool
    enterSavedLevel(int level_id, std::vector<std::unique_ptr<W8SpellEffectEntry>> restored_casts);
    std::unique_ptr<World> loadWorld(int level_id, std::string source_path);
    std::unique_ptr<W8WorldItem> createWorldItem(World& world, W8ItemInstance* item,
                                                 const glm::vec3* position, int entity_flags);
    std::unique_ptr<W8WorldItem> spawnWorldItem(World& world, int item_id,
                                                const glm::vec3* position, int entity_flags);
    /* End level-bound activity, capture the outgoing level's state and release its
       world. The game, its party and its rng remain; ending the game is the session's
       operation. */
    void leaveLevel();
    /* The step time this game has run: the sum of the elapsed times its steps were given.
       Game-step deadlines use this clock; world holds use visual_time, while UI
       presentation and the saved game-day calendar are explicit timer domains. */
    std::chrono::milliseconds time{};
    // World visual time stops with world advancement and shared timer holds.
    // UI/cursor presentation continues on its own monotonic clock.
    std::chrono::milliseconds visual_time{};
    bool timers_paused = false;
    bool move_timer_suspended = false;
    W8GameTimeAccumulator motion_clock;
    TimerClockView timerClocks()
    {
        return {time,          visual_time,  status.game_time_days, status.game_time_ms,
                timers_paused, motion_clock, move_timer_suspended};
    }

    /* Start a fight with the monsters hostile to the party; `surprise` says the party
       may be ambushed. Declines (returns false) while no member can fight, the party has
       no footing, or a hostile NPC's combat-start script is still owed. */
    bool beginCombat(int surprise);
    /* End the fight: award its experience, restore the formation and the world, and
       destroy the combat. A forced end (a departure or a scripted end) neither announces
       nor kills the summoned monsters. */
    void endCombat(bool forced_cleanup);
    void resetSight();
    int normalizeLevel(int requested) const;
    signed char levelBand(int requested) const;
    W8NpcState* createNpcRuntimeNode(int npc_id);
    W8PreparedNpcBindings prepareNpcBindings(World& world);

    // Commands the level's monster scripts and triggers issue.
    // A monster's script turns the group of the monster at `location_id` to `disposition`.
    void setMonsterGroupDisposition(int location_id, W8Disposition disposition);
    /* A monster's script starts the golem attack: two golem groups turn hostile, and the
       second joins the fight. */
    void startGolemAttack();
    /* A trap or fountain casts `spell` at `position`. Heal All also lifts every condition
       from the party. */
    void castTriggerSpell(glm::vec3 position, W8SpellId spell, unsigned int power_level);

    const GameData& data;
    // The picked imported object, while the party is free to interact with the world.
    NativeObject* interactiveNativeObject();
    // Inspect that object; the cursor shows the same object as interactive.
    bool activateSelectedNativeObject();
    // Optional session content, rebuilt at level entry; never written into retail saves.
    std::filesystem::path native_object_file;

    W8Party party{};
    // The party's shared pool and hand, and every move of an item.
    Inventory inventory{*this};
    GameStatus status{};
    /* Every gameplay draw, party creation's included. A game continues the stream of the
       game it replaces or that ended before it, so a session draws one sequence from one
       seed, as retail did; loot and NPC theft reseed it from saved values. */
    Rng rng;
    Story story;
    /* Characters kept outside the party under their file names; a started game reads and
       writes character files here, and a save holds each in a CHAR section. */
    std::vector<wiz8::save::StoredCharacter> stored_characters;
    W8MessageLog message_log;
    // The fight in progress, if any.
    std::optional<Combat> combat;
    /* What the party is doing. The camp commands set the place; dialogue and story scenes
       set the rest. */
    Activity activity;
    // Voice barks and quotes waiting for, or holding, a speaker.
    W8CharacterEventQueue speech{*this};
    // The NPC conversation's progress: its speaker, queued script work and owed reply.
    W8NpcDialogue npc_dialogue;
    // The conversation participants, offered item, transcript and staged script notice.
    W8NpcInteractionState npc_interaction;
    W8SedexusScene sedexus_scene;
    // The deadline of each shared three-minute spell cooldown (in `SpellPracticeCooldown`).
    std::array<std::uint64_t, 14> spell_cooldowns{};
    /* The step time left until the timed autosave announces itself, counted in the world
       while the game's world timers run. */
    static constexpr std::chrono::minutes kAutosaveInterval{5};
    std::chrono::milliseconds autosave_due = kAutosaveInterval;
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

    /* Whether the world moves on. Each step applies what the interface holds
       (`ApplyWorldAdvancement`); rules read the result. */
    WorldAdvancement advancement;
    /* No turns pass: the camp's Use handed an item to the main game's use-item picker,
       which has not taken it yet. The screens set it. */
    bool aging_held = false;
    SurpriseSequence surprise;
    // The step time left until monster sight and the nearby world items are next refreshed.
    std::chrono::milliseconds sight_due{};
    std::chrono::milliseconds world_items_due{};
    /* Experience was awarded or the main game entered, so the level-up-ready notices are
       checked again once the interface is idle. */
    bool level_up_check_due = false;
    // The timed autosave has announced itself and saves on its next tick.
    bool save_notice_shown = false;

    // What happened, in order, for the UI to present.
    Events events;
    Search search{combat, events, level};
    /* Leaving a level stores its state here, entering one reads it back, and a save writes
       them beside the current level's. */
    LeftLevels left_levels;
    // Unkeyed camera sequences can continue across level changes, but not another game.
    ShakeSystem camera_shakes{timerClocks()};
    /* Last, so its worlds are destroyed first, while combat and speech still exist.
       Session and level transitions perform departure before releasing the world;
       screen disposal and world destruction do no departure bookkeeping. */
    Level level;

private:
    unsigned char testFact(W8FactId fact, std::vector<Event>& outcomes);
    unsigned char evaluateFact(W8FactId fact, std::vector<Event>& outcomes);
    void writeFact(W8FactId fact_id, unsigned char value, bool notify,
                   std::function_ref<void(std::vector<Event>)> publish);
    void recordFactChange(W8FactId fact_id, bool notify,
                          std::function_ref<void(std::vector<Event>)> publish);
    std::shared_ptr<W8NpcState> makeNpcRuntimeNode(int npc_id);
    void updateSurpriseMode();
    void advanceWorlds(std::chrono::milliseconds elapsed);
    void advanceSpellEffects(World& world);
};

/* The games of one run of the program: the game in progress, if any, and the random
   stream the next game continues, so a run draws one sequence from one seed, as retail
   did. The Application owns it, and the router hands it to the screens that begin and
   end games. One exists at a time; while it does, `CurrentGame()` reads its game. Every
   game reads `data`, which the caller keeps alive longer than the session. */
class GameSession {
public:
    explicit GameSession(const GameData& data);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // The game in progress, or null.
    Game* game() { return game_.get(); }
    std::filesystem::path native_object_file;
    /* Begin a new game, ending any in progress. It continues the previous game's random
       stream, and its events go to the observer. */
    Game& begin();
    /* Replace a decoded game at the presentation boundary: end combat while its views
       exist, release those views, install the saved owners and enter the saved level.
       A construction failure ends the replacement and propagates to the caller.
       Resource construction still requires the installed game; decoding is independent. */
    void load(SavedGame saved, std::function_ref<void()> release_presentation);
    /* End the game in progress, if any, leaving its level (departure bookkeeping); the next
       game continues its random stream. */
    void end();
    /* Seed the random stream of the game in progress, or of the next game when none is.
       The stream starts from the clock. */
    void seed(std::uint32_t seed);
    /* The composition root's presenter for every game's events; a game begun before one
       is installed keeps its events pending. */
    void observe(std::function<void(Game&, const Event&)> observer);

private:
    // Release the game in progress without leaving its level.
    void discard();
    const GameData& data_;
    std::unique_ptr<Game> game_;
    Rng next_stream_;
    std::function<void(Game&, const Event&)> observer_;
};

/* The game in progress. Transitional: code that does not receive the game yet reaches
   it here; `rules` and `game` may not, and this goes with its last caller. Throws
   std::logic_error when no game is in progress. */
Game& CurrentGame();
bool GameInProgress();
// A game is in progress and the party has left party creation.
bool GameStarted();

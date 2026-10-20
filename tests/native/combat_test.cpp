#include <memory>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "scoped_combat.h"
#include "scoped_game.h"
#include "wiz8/game.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/monster_roster.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/world/world.h"

namespace {

struct ScopedWorld {
    std::unique_ptr<World> previous = ReplaceWorld(std::make_unique<World>());
    ~ScopedWorld() { ReplaceWorld(std::move(previous)); }
};

struct ScopedSettings {
    W8GameSettings previous = g_settings;
    ~ScopedSettings() { g_settings = previous; }
};

// A fight in an empty level with two conscious party members.
struct CombatFixture {
    ScopedWorld world;
    ScopedSettings settings;
    ScopedGame game_scope;
    ScopedCombat combat_scope{true};
    Game& game = game_scope.game;
    W8CombatState& combat = *game.combat.state();

    CombatFixture()
    {
        g_settings.camera_rotation_mode = W8_CAMERA_ROTATION_DISABLED;
        g_settings.verbose_combat_messages = 0;
        for (int slot = 0; slot < 2; ++slot) {
            game.party.rows[slot].fOccupied = true;
            game.party.characters[slot].hp_current = 10;
        }
    }

    W8MonsterInfo& AddMonster(int location_id, unsigned int phase)
    {
        auto monster = std::make_unique<W8MonsterInfo>();
        monster->location_id = location_id;
        monster->hp_current = 10;
        monster->pCombat = std::make_unique<W8MonsterCombatState>();
        monster->pCombat->phase = phase;
        return *CurrentLevelPopulation().monster_roster.add(std::move(monster), false);
    }
};

} // namespace

TEST_CASE_METHOD(CombatFixture, "The party acts before a monster on the same phase")
{
    combat.round_counter = 10;
    combat.characters[0].phase = 10;
    combat.characters[1].phase = 10;
    game.party.characters[0].highest_condition = W8_CONDITION_ASLEEP;
    game.party.rows[0].pending_action = W8_ACTION_ATTACK;
    AddMonster(7, 10);

    ScheduleCombatActor();

    const auto* scheduled = std::get_if<TurnScheduled>(&combat.turn());
    REQUIRE(scheduled);
    REQUIRE(scheduled->actor == CombatActor{PartyActor{1}});
    // The sleeping member loses its action and counts as having acted.
    REQUIRE(game.party.rows[0].pending_action == W8_ACTION_NONE);
    REQUIRE(combat.characters[0].phase == 0);
    REQUIRE(combat.characters[0].dead);
}

TEST_CASE_METHOD(CombatFixture, "A turn ended while its action resolves stays idle")
{
    const CombatActor actor = PartyActor{0};
    combat.schedule(actor);
    combat.setStepOf(actor, ActionStep::resolving);
    REQUIRE(combat.resolvingTurnOf(actor));

    // The actor dies during its own swing; the swing then reports finishing.
    combat.endTurnOf(actor);
    combat.setStepOf(actor, ActionStep::finishing);

    REQUIRE(std::holds_alternative<TurnIdle>(combat.turn()));
    REQUIRE_FALSE(combat.actingSlot());
}

TEST_CASE_METHOD(CombatFixture, "Combat waits on a conscious NPC's queued combat script")
{
    REQUIRE(CombatWaitFor(game) == CombatWait::none);

    combat.npc_combat_script_pending[1] = true;
    REQUIRE(CombatWaitFor(game) == CombatWait::npc_script);

    game.party.characters[1].highest_condition = W8_CONDITION_ASLEEP;
    REQUIRE(CombatWaitFor(game) == CombatWait::none);
}

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

#include <cstdio>
#include "mutable_game_data.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/platform/random.h"

TEST_CASE_METHOD(CombatFixture, "GOLDEN capture")
{
    struct RestoreStrings {
        std::vector<std::string> strings = std::move(MutableData().strings);
        ~RestoreStrings() { MutableData().strings = std::move(strings); }
    } restore;
    MutableData().strings.assign(0x800, "s");
    for (int slot = 0; slot < 2; ++slot) {
        auto& character = game.party.characters[slot];
        character.name = slot == 0 ? "A" : "B";
        character.hp_current = 30;
        character.uiHPMax = 30;
        character.stamina = 100;
        character.uiStaminaMax = 100;
        for (auto& item : character.EquippedItem) item.iItemNo = -1;
        game.party.rows[slot].party_order_index = slot;
        game.party.formation.setPosition(slot, 0, slot, false, true);
    }
    auto& attacker = game.party.characters[0];
    attacker.Hand[0].in_play = true;
    attacker.Hand[0].uiHolds = HOLDS_NOTHING;
    attacker.Hand[0].swings = 1;
    attacker.Hand[0].attacks = 1;
    attacker.Hand[0].attack_score = 80;
    attacker.Hand[0].damage_dice = {2, 4, 1};
    combat.characters[0].hand_attack_values[0] = 1;
    combat.characters[0].current_hand = 0;
    auto& row = game.party.rows[0];
    row.pending_action = W8_ACTION_ATTACK;
    row.target_out_of_combat = {};
    row.target_out_of_combat.iType = W8_TARGET_KIND_CHARACTER;
    row.target_out_of_combat.iChar = 1;
    row.target_out_of_combat.iMonsterID = -1;
    row.target_out_of_combat.iGroupID = -1;

    SeedRandom(12345);
    REQUIRE(StartCharacterAttack(0, W8_ATTACK_MODE_PUNCH));
    std::vector<int> steps;
    for (int i = 0; i < 10; ++i) {
        const auto step = ResolveCharacterAttack(0);
        steps.push_back(step == ActionStep::finishing);
        if (step == ActionStep::finishing) break;
    }
    std::printf("steps:");
    for (int s : steps) std::printf(" %d", s);
    std::printf("\nhp %u stamina0 %d score %d events %zu next %u\n",
                game.party.characters[1].hp_current, game.party.characters[0].stamina,
                combat.characters[0].hand_records[0].score, game.events.pending().size(),
                Random15());
    for (const auto& event : game.events.pending()) std::printf(" event %zu\n", event.index());
}

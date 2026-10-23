#include <algorithm>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "harness.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/mgs_commands.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

namespace {

/* Live monsters fighting the party. */
std::vector<W8MonsterInfo*> CombatMonsters()
{
    std::vector<W8MonsterInfo*> monsters;
    for (const auto& owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster = owner.get();
        if (monster->fActive && monster->pCombat && monster->entity.find() && monster->hp_current)
            monsters.push_back(monster);
    }
    return monsters;
}

float NearestCombatMonster()
{
    float nearest = std::numeric_limits<float>::max();
    for (W8MonsterInfo* monster : CombatMonsters())
        nearest = std::min(nearest,
                           HorizontalDistance(monster->entity->nav.GetPosition(), PartyPosition()));
    return nearest;
}

/* The nearest of the three Crabs on the starting beach (retail level data). */
constexpr int kBeachCrab = 238;

/* Dismiss the quit prompt, then a real fight: walk up to the beach Crabs until
   they start combat, play rounds through the round key until the Crabs have
   closed in and both sides have landed blows, then finish the fight and leave.
   This covers monster path planning, monster attacks and the fighter's
   automatic re-targeting once enemies come into reach. */
void CombatScenario()
{
    GameHarness game(false);
    StartPremadeGame(game);
    Settle(game, "settle");
    // A closed dialog erases itself from the interface overlay.
    game.key(SDLK_ESCAPE);
    REQUIRE(game.within_frames(4, [] { return g_modal_owner && OverlayPixel({320, 240}); }));
    game.key(SDLK_ESCAPE);
    REQUIRE(game.within_frames(4, [] { return !g_modal_owner && !OverlayPixel({320, 240}); }));

    const W8Character& fighter = CurrentGame().party.characters[PremadeSlot()];
    Approach(game, kBeachCrab, 700);
    REQUIRE(CurrentGame().combat.has_value());
    // Closing and choosing through real menu targets must release its input layer
    // and restore the same portrait's controls, including deferred destruction.
    const int slot = PremadeSlot();
    int left, top, edge, band, unused_x, unused_y;
    GetPartySlotMenuAnchor(slot, &left, &top, &edge, &band, &unused_x, &unused_y, 0);
    const SDL_Point attack{left + 0x17 + 14, top + 64};
    game.click("open-action-menu", *MainGame()->combat_targets[slot]);
    REQUIRE(MainGame()->keyboardMenuForSlot(slot));
    game.click("cancel-action-menu", attack, SDL_BUTTON_RIGHT);
    REQUIRE_FALSE(MainGame()->keyboard_menu);
    game.click("reopen-action-menu", *MainGame()->combat_targets[slot]);
    REQUIRE(MainGame()->keyboardMenuForSlot(slot));
    game.click("choose-menu-defend", SDL_Point{left + 0x17 + 14, top + 46});
    REQUIRE_FALSE(MainGame()->keyboard_menu);
    REQUIRE(CurrentGame().party.rows[slot].committed.kind == W8_ACTION_DEFEND);
    game.click("reopen-menu-for-attack", *MainGame()->combat_targets[slot]);
    game.click("choose-menu-attack", attack);
    REQUIRE_FALSE(MainGame()->keyboard_menu);
    REQUIRE(CurrentGame().party.rows[slot].committed.kind == W8_ACTION_ATTACK);
    const std::vector<W8MonsterInfo*> crabs = CombatMonsters();
    REQUIRE(!crabs.empty());
    std::vector<int> crab_ids;
    unsigned crab_health = 0, reward = 0;
    for (W8MonsterInfo* crab : crabs) {
        crab_ids.push_back(crab->location_id);
        crab_health += crab->hp_current;
        reward += GetMonsterExperience(GetMonsterDataForInfo(crab));
    }
    const auto remaining_health = [&] {
        unsigned health = 0;
        for (int id : crab_ids)
            if (const W8MonsterInfo* crab = FindMonster(id, false)) health += crab->hp_current;
        return health;
    };
    const float start_distance = NearestCombatMonster();
    float closest = start_distance;
    const unsigned health = fighter.hp_current, experience = fighter.experience;
    const int kills = fighter.kill_count;
    CAPTURE(crab_ids, start_distance, health);

    for (int round = 0; round < 8 && CurrentGame().combat.has_value() &&
                        !(fighter.hp_current < health && remaining_health() < crab_health);
         ++round) {
        game.command(W8_MGS_COMMAND_START_COMBAT_ROUND);
        game.wait(
            "round",
            [] { return !CurrentGame().combat || !CurrentGame().combat->execution_active; }, 240);
        closest = std::min(closest, NearestCombatMonster());
    }
    CAPTURE(fighter.hp_current, remaining_health(), closest, game.frames());
    // The Crabs walked up rather than retreating from a zero-length route.
    REQUIRE(closest < start_distance / 2);
    REQUIRE(fighter.hp_current < health);
    // The fighter picked a Crab once one came into reach, and hit it.
    REQUIRE(remaining_health() < crab_health);

    // Rewards depend on every Crab dying, not on who struck the last blow.
    W8TargetSource attacker{};
    attacker.iType = W8_TARGET_SOURCE_CHARACTER;
    attacker.iChar = PremadeSlot();
    attacker.iMonsterID = -1;
    // A Crab slain in a round fades out and leaves the roster a few seconds later.
    std::vector<int> slain;
    for (int id : crab_ids) {
        W8MonsterInfo* crab = FindMonster(id, false);
        if (crab && crab->hp_current)
            ApplyDamageToMonster(crab, crab->hp_current, &attacker, false, 1, 0, nullptr, false);
        else
            slain.push_back(id);
    }
    for (int id : crab_ids) {
        // Resolve by ID after death/cleanup rather than relying on a retained pointer.
        const W8MonsterInfo* dead = FindMonster(id, false);
        CAPTURE(id, slain);
        if (!dead && std::ranges::contains(slain, id)) continue;
        REQUIRE((dead && !dead->fActive && dead->hp_current == 0 &&
                 dead->uiCondition[W8_CONDITION_DEAD] != 0));
    }
    if (CurrentGame().combat.has_value() && !CurrentGame().combat->execution_active)
        game.command(W8_MGS_COMMAND_START_COMBAT_ROUND);
    game.wait("leave-combat", [] { return CurrentGame().combat.has_value() == 0; }, 60);
    REQUIRE(!CurrentGame().combat);
    CHECK(fighter.experience == experience + reward);
    CHECK(fighter.kill_count == kills + int(crab_ids.size()));
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    REQUIRE(game.within_frames(4, [] { return CurrentGame().combat.has_value() != 0; }));
    REQUIRE((CurrentGame().combat && CurrentGame().combat->round_count == 0));
    REQUIRE(CurrentGame().combat->attack_report.reports.empty());
    auto& member = CurrentGame().party.characters[PremadeSlot()];
    member.party_weight_share = 123456;
    SettleActiveParty(CurrentGame());
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    game.wait("leave-combat-again", [] { return CurrentGame().combat.has_value() == 0; }, 10);
    REQUIRE(!CurrentGame().combat);
    REQUIRE(member.party_weight_share != 123456);

    // Placement consumes the supplied player exclusion, including its absence.
    auto& octree = *GetWorld()->spatial;
    const auto origin = GetWorld()->party.position();
    const auto available =
        octree.navigatorPositions(origin, 0, 100, 1, true, false, false, 5, false, std::nullopt);
    REQUIRE_FALSE(available.positions.empty());
    const BoundingSphere excluded{origin, 1e9f};
    REQUIRE(octree.navigatorPositions(origin, 0, 100, 1, true, false, false, 5, false, excluded)
                .positions.empty());
    const auto scattered = octree.scatterPositions(origin, 0, 200, 1, true, false, std::nullopt);
    REQUIRE_FALSE(scattered.empty());
    REQUIRE(octree.scatterPositions(origin, 0, 200, 1, true, false, excluded).empty());
    {
        // Keep the queried world alive while another, empty world is active.
        // Ground probes and monster lookups must still use the queried octree.
        struct RestoreWorld {
            std::unique_ptr<World> previous =
                CurrentGame().level.replacePrimary(std::make_unique<World>());
            ~RestoreWorld() { CurrentGame().level.replacePrimary(std::move(previous)); }
        } restore;
        REQUIRE(
            octree.navigatorPositions(origin, 0, 100, 1, true, false, false, 5, false, std::nullopt)
                .positions == available.positions);
        REQUIRE(octree.scatterPositions(origin, 0, 200, 1, true, false, std::nullopt) == scattered);
    }
    /* Shutting down mid-fight ends it before the interface is released, also
       while the inventory has suspended the main game and released its
       portrait controls. */
    EnterCombat(game);
    game.command(W8_MGS_COMMAND_INVENTORY);
    game.wait_screen("inventory-in-combat", W8_SCREEN_CAMP);
    REQUIRE(CurrentGame().combat.has_value());
    // HP exhaustion from the full live operation must apply death, not lose the result
    // to an earlier duplicate calculation. Teardown still ends combat from camp.
    member.uiHPMax = 100000;
    member.hp_current = 1;
    RecalculateCharacterDerivedStats(CurrentGame(), member);
    REQUIRE(member.hp_current == 0);
    REQUIRE(member.uiCondition[W8_CONDITION_DEAD] != 0);
    REQUIRE(member.highest_condition >= W8_CONDITION_DEAD);
    std::printf("combat: fight, kills and exit after %lu frames\n", game.frames());
}

} // namespace

TEST_CASE("combat", "[runtime]")
{
    RunScenario([&] { CombatScenario(); });
}

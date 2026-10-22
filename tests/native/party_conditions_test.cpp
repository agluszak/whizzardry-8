// Conditions, death and settlement on an empty level, without screens or retail data.
#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scoped_combat.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "test_worlds.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/live_party.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/world/world.h"

namespace {
GameData ConditionData()
{
    GameData data{.strings = std::vector<std::string>(0x400),
                  .spells = std::vector<W8SpellRuntimeRecord>(0x72)};
    data.strings[0x173] = "cheats death!";
    data.strings[0x180] = "is fearless.";
    data.strings[0x392] = "dies";
    return data;
}

struct PartyFixture {
    ScopedGameData data{ConditionData()};
    ScopedGame scope;
    Game& game = scope.game;

    PartyFixture()
    {
        game.level.replacePrimary(MakeWorld());
        game.party.members[0].emplace();
        auto& character = member();
        character.fInParty = true;
        character.iRace = W8_RACE_HUMAN;
        character.gender = W8_GENDER_MALE;
        character.iProfession = W8_PROFESSION_FIGHTER;
        for (auto& item : character.EquippedItem) item.iItemNo = -1;
        for (auto& item : character.backpack) item.iItemNo = -1;
        for (auto& attribute : character.attributes) attribute.base = 50;
        SettleActiveParty(game);
        character.hp_current = character.uiHPMax;
        game.events = {};
    }
    W8Character& member() { return game.party.characters[0]; }
    long deaths() const
    {
        return std::ranges::count_if(game.events.pending(), [](const Event& event) {
            return std::holds_alternative<CharacterDied>(event);
        });
    }
};
} // namespace

TEST_CASE_METHOD(PartyFixture, "A ceiling that exhausts hit points kills the member once",
                 "[conditions]")
{
    member().uiCondition[W8_CONDITION_POISONED] = 30;
    member().condition_argument = 4;
    // The pool was under a higher ceiling than the member's state now supports.
    member().uiHPMax += member().hp_current;

    SettleActiveParty(game);
    REQUIRE(member().uiCondition[W8_CONDITION_DEAD] == W8_CONDITION_INDEFINITE);
    REQUIRE(member().highest_condition == W8_CONDITION_DEAD);
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);
    REQUIRE((member().hp_current == 0 && member().stamina == 0 && member().death_count == 1));
    REQUIRE(deaths() == 1);

    // Dying again changes nothing: the death's consequences belong to the first one.
    REQUIRE_FALSE(InflictCondition(
        game, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    SettleActiveParty(game);
    REQUIRE((deaths() == 1 && member().death_count == 1));
}

TEST_CASE_METHOD(PartyFixture, "Cheating death leaves the member unconscious instead",
                 "[conditions]")
{
    ScopedCombat fight{true};
    member().iProfession = W8_PROFESSION_VALKYRIE;
    member().hp_current = 1;

    REQUIRE(CheatsDeath(member()));
    REQUIRE_FALSE(InflictCondition(game, 0, {.condition = W8_CONDITION_DEAD}));
    REQUIRE(member().uiCondition[W8_CONDITION_DEAD] == 0);
    REQUIRE(member().uiCondition[W8_CONDITION_UNCONSCIOUS] != 0);
    REQUIRE(member().highest_condition == W8_CONDITION_UNCONSCIOUS);
    // The first revival in a fight restores at least half of the maximum.
    REQUIRE(static_cast<int>(member().hp_current) >= member().uiHPMax / 2);
    REQUIRE((game.combat->characters[0].cheat_death_used && deaths() == 0));

    // Unconscious long enough, the member has no more deaths to cheat.
    member().uiCondition[W8_CONDITION_UNCONSCIOUS] = 7;
    REQUIRE_FALSE(CheatsDeath(member()));
}

TEST_CASE_METHOD(PartyFixture, "Traits and poison strength refuse a condition", "[conditions]")
{
    REQUIRE_FALSE(InflictCondition(game, 0, {.condition = W8_CONDITION_POISONED, .duration = 10}));
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);

    member().iProfession = W8_PROFESSION_SAMURAI;
    REQUIRE_FALSE(InflictCondition(game, 0, {.condition = W8_CONDITION_AFRAID, .duration = 10}));
    REQUIRE(member().uiCondition[W8_CONDITION_AFRAID] == 0);
    const auto& refusal = std::get<CharacterNotice>(game.events.pending().back());
    REQUIRE(refusal.text == "is fearless.");
}

TEST_CASE_METHOD(PartyFixture, "A longer condition is not shortened, and aging lifts it",
                 "[conditions]")
{
    REQUIRE(InflictCondition(game, 0, {.condition = W8_CONDITION_ASLEEP, .duration = 20}));
    REQUIRE_FALSE(InflictCondition(game, 0, {.condition = W8_CONDITION_ASLEEP, .duration = 5}));
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 20);

    AgeCondition(game, 0, W8_CONDITION_ASLEEP, 15);
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 5);
    AgeCondition(game, 0, W8_CONDITION_ASLEEP, 5);
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 0);
    REQUIRE(member().highest_condition == W8_CONDITION_NONE);
}

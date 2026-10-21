// Game-level inventory commands: the hand, the party pool and a member's backpack.
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scoped_game.h"
#include "scoped_game_data.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"

namespace {

/* A started game with one conscious member, one item record and a string table that reaches
   the inventory notices. No profession or race data is loaded, so the member derives no
   statistics. */
struct HeldItemFixture {
    ScopedGame game_scope;
    Game& game = game_scope.game;
    ScopedGameData data{GameData{.strings = std::vector<std::string>(0x910),
                                 .items = std::vector<W8ItemDatabaseRecord>(1)}};

    HeldItemFixture()
    {
        game.status.game_started = true;
        game.party.members[0].emplace();
        game.party.characters[0].fInParty = true;
        game.party.characters[0].gender = W8_GENDER_UNSET;
        game.party.characters[0].hp_current = 10;
        game.party.characters[0].uiExpLevel = 1;
        game.party.held.item = {.iItemNo = 0, .identified = true};
    }

    void FillBackpack()
    {
        for (auto& cell : game.party.characters[0].backpack) cell = {.iItemNo = 0};
    }

    void FillPool()
    {
        while (game.party.insertIntoPool(game.party.item_count, {.iItemNo = 0})) {
        }
    }
};

} // namespace

TEST_CASE_METHOD(HeldItemFixture, "A held item the member and the pool refuse stays held")
{
    FillBackpack();
    FillPool();

    // The portrait right-click path: the pool first, then the member.
    const auto given = GiveHeldItemToCharacterOrParty(game, 0, true);

    REQUIRE_FALSE(given.has_value());
    REQUIRE(game.party.held.item.iItemNo == 0);
    REQUIRE(game.party.characters[0].backpack[7].iItemNo == 0);
}

TEST_CASE_METHOD(HeldItemFixture, "A stored held item leaves the hand")
{
    FillPool();

    REQUIRE(GiveHeldItemToCharacterOrParty(game, 0, true).has_value());

    REQUIRE(game.party.held.item.iItemNo == -1);
    REQUIRE(game.party.characters[0].backpack[0].iItemNo == 0);
}

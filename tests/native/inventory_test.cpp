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
#include "wiz8/rules/item_instance.h"

namespace {

/* A started game with one conscious member, one item record and a string table that reaches
   the inventory notices. No profession or race data is loaded, so the member derives no
   statistics. */
struct HeldItemFixture {
    GameData data{.strings = std::vector<std::string>(0x910),
                  .items = std::vector<W8ItemDatabaseRecord>(1)};
    ScopedGame game_scope{data};
    Game& game = game_scope.game;
    ScopedGameData installed{GameData{data}};

    HeldItemFixture()
    {
        game.status.game_started = true;
        game.party.members[0].emplace();
        game.party.characters[0].fInParty = true;
        game.party.characters[0].gender = W8_GENDER_UNSET;
        game.party.characters[0].hp_current = 10;
        game.party.characters[0].uiExpLevel = 1;
        game.inventory.restore({}, W8ItemInstance{.iItemNo = 0, .identified = true});
    }

    void FillBackpack()
    {
        for (auto& cell : game.party.characters[0].backpack) cell = {.iItemNo = 0};
    }

    void FillPool()
    {
        game.inventory.restore(std::vector<W8ItemInstance>(Inventory::kPoolLimit, {.iItemNo = 0}),
                               game.inventory.held()->item);
    }
};

} // namespace

TEST_CASE_METHOD(HeldItemFixture, "A held item the member and the pool refuse stays held")
{
    FillBackpack();
    FillPool();

    // The portrait right-click path: the pool first, then the member.
    const auto given = game.inventory.giveHeld(0, StoreOrder::PoolFirst);

    REQUIRE_FALSE(given.has_value());
    REQUIRE(game.inventory.heldItemId() == 0);
    REQUIRE(game.party.characters[0].backpack[7].iItemNo == 0);
}

TEST_CASE_METHOD(HeldItemFixture, "A stored held item leaves the hand")
{
    FillPool();

    REQUIRE(game.inventory.giveHeld(0, StoreOrder::PoolFirst).has_value());

    REQUIRE(!game.inventory.held());
    REQUIRE(game.party.characters[0].backpack[0].iItemNo == 0);
}

TEST_CASE("A full pool reports partial deposits and retains every unplaced unit", "[inventory]")
{
    GameData data{.items = std::vector<W8ItemDatabaseRecord>(1)};
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].maximum_quantity = 100;
    ScopedGame scope{data};
    auto& inventory = scope.game.inventory;
    std::vector<W8ItemInstance> pool(Inventory::kPoolLimit, {.iItemNo = 0, .stack_count = 100});
    pool[0].stack_count = 95;
    inventory.restore(std::move(pool), W8ItemInstance{.iItemNo = 0, .stack_count = 20});
    const auto before = inventory.count(0);

    REQUIRE(inventory.depositHeld(0, true) == Inventory::Deposit::Partial);
    REQUIRE(inventory.pool().size() == Inventory::kPoolLimit);
    REQUIRE(inventory.pool()[0].stack_count == 100);
    REQUIRE(inventory.held()->item.stack_count == 15);
    REQUIRE(inventory.count(0) == before);
    REQUIRE(inventory.depositHeld(0, true) == Inventory::Deposit::Refused);
    REQUIRE(inventory.held()->item.stack_count == 15);
    REQUIRE(inventory.count(0) == before);
}

TEST_CASE("A deposit reports completion only after its hand and pool settle", "[inventory][events]")
{
    GameData data{.items = std::vector<W8ItemDatabaseRecord>(1)};
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].maximum_quantity = 100;
    ScopedGame scope{data};
    auto& inventory = scope.game.inventory;
    inventory.restore({W8ItemInstance{.iItemNo = 0, .stack_count = 95}},
                      W8ItemInstance{.iItemNo = 0, .stack_count = 20});
    unsigned int notifications = 0;
    scope.game.events.observe([&](const Event&) {
        ++notifications;
        REQUIRE(inventory.held() == nullptr);
        REQUIRE(inventory.pool().size() == 2);
        REQUIRE(inventory.count(0) == 115);
    });
    REQUIRE(inventory.depositHeld(0, true) == Inventory::Deposit::Complete);
    REQUIRE(notifications > 0);
}

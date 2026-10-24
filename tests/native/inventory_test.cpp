// Game-level inventory commands: the hand, the party pool and a member's backpack.
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scoped_game.h"
#include "scoped_game_data.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/random.h"
#include "wiz8/story.h"

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

TEST_CASE_METHOD(HeldItemFixture,
                 "A deposit reports completion only after its hand and pool settle",
                 "[inventory][events]")
{
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].maximum_quantity = 100;
    auto& inventory = game.inventory;
    inventory.restore({W8ItemInstance{.iItemNo = 0, .stack_count = 95}},
                      W8ItemInstance{.iItemNo = 0, .stack_count = 20});
    unsigned int notifications = 0;
    game.events = {};
    game.events.observe([&](const Event&) {
        ++notifications;
        REQUIRE(inventory.held() == nullptr);
        REQUIRE(inventory.pool().size() == 2);
        REQUIRE(inventory.count(0) == 115);
    });
    REQUIRE(inventory.depositHeld(0, true) == Inventory::Deposit::Complete);
    REQUIRE(notifications > 0);
}

TEST_CASE("Quantity normalization follows explicit definitions and preserves the selected byte",
          "[items][rules]")
{
    GameData data;
    data.items.resize(1);
    W8ItemInstance item{.iItemNo = 0, .stack_count = 4, .uses_or_charges = 7};
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item.stack_count == 4);
    REQUIRE(item.uses_or_charges == 0);
    item.stack_count = 0;
    item.uses_or_charges = 7;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item.stack_count == 7);
    REQUIRE(item.uses_or_charges == 0);
    item.stack_count = 0;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item.stack_count == 1);

    data.items[0].quantity_kind = W8_ITEM_QUANTITY_CHARGES;
    item.stack_count = 4;
    item.uses_or_charges = 7;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item.stack_count == 0);
    REQUIRE(item.uses_or_charges == 7);
    item.stack_count = 4;
    item.uses_or_charges = 0;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item.stack_count == 0);
    REQUIRE(item.uses_or_charges == 4);

    const W8ItemInstance empty;
    item = empty;
    NormalizeItemQuantityKind(data, item);
    REQUIRE(item == empty);
}

TEST_CASE("Acquiring the Astral Dominae clears one scheduled copy without extra randomness",
          "[inventory][facts]")
{
    GameData data;
    data.items.resize(0x300);
    data.npcs.resize(0x49);
    data.npcs[0x48].kind = 0x48;
    ScopedGameData definitions{std::move(data)};
    ScopedGame scope{Data()};
    Game game{Data()};
    auto outgoing_rng = scope.game.rng;
    auto npc = std::make_shared<W8NpcState>();
    npc->record = &Data().npcs[0x48];
    for (auto& id : npc->item_ids) id = -1;
    npc->item_ids[0] = npc->item_ids[1] = 0x242;
    game.story.npcs.push_back(npc);
    std::vector<Rng::Draw> draws;
    game.rng.trace(&draws);
    W8ItemInstance item{.iItemNo = 0x242};
    REQUIRE(game.inventory.storeInPool(item, false));
    REQUIRE(npc->item_ids[0] == -1);
    REQUIRE(npc->item_ids[1] == 0x242);
    REQUIRE(game.story.facts[W8_FACT_ASTRAL_POSSESS] == 1);
    REQUIRE(game.story.journal.size() == 1);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().range == 8); // Reaction selection even when the party is empty.
    REQUIRE_FALSE(game.inventory.pool().front().effect_used);

    // Same-value writes still apply mandatory consequences, but do not journal twice.
    game.setFact(W8_FACT_ASTRAL_POSSESS, 1);
    REQUIRE(npc->item_ids[1] == -1);
    REQUIRE(game.story.journal.size() == 1);
    REQUIRE(draws.size() == 1);
    REQUIRE(scope.game.story.journal.empty());
    REQUIRE(scope.game.story.facts[W8_FACT_ASTRAL_POSSESS] == 0);
    REQUIRE(scope.game.rng.next() == outgoing_rng.next());
    game.rng.trace(nullptr);
}

TEST_CASE("Repeated pool acquisitions preserve reaction draws and Chaos timing",
          "[inventory][facts]")
{
    GameData data;
    data.items.resize(0x300);
    ScopedGameData definitions{std::move(data)};
    ScopedGame scope{Data()};
    auto& game = scope.game;
    std::vector<Rng::Draw> draws;
    game.rng.trace(&draws);
    // Creation/import suppress only reactions: plot ownership still settles, without a draw.
    game.status.world_clock = 50;
    W8ItemInstance seeded{.iItemNo = 0x244};
    REQUIRE(game.inventory.storeInPool(seeded, false, true, ItemReactions::Suppressed));
    REQUIRE(draws.empty());
    REQUIRE(game.story.facts[W8_FACT_CHAOS_POSSESS] == 1);
    REQUIRE(game.status.fact_b8_clock == 50);
    for (int clock : {100, 200}) {
        game.status.world_clock = clock;
        game.status.fact_b8_pending = false;
        W8ItemInstance item{.iItemNo = 0x244};
        REQUIRE(game.inventory.storeInPool(item, false));
        REQUIRE(game.status.fact_b8_pending);
        REQUIRE(game.status.fact_b8_clock == clock);
    }
    REQUIRE(game.story.journal.size() == 1);
    W8ItemInstance ordinary{.iItemNo = 0, .effect_used = true};
    REQUIRE(game.inventory.storeInPool(ordinary, false));
    REQUIRE(draws.size() == 3);
    for (const auto& draw : draws) REQUIRE(draw.range == 8);
    game.rng.trace(nullptr);
}

TEST_CASE("Unmigrated fact handlers retain journal and nested diagnostic ordering",
          "[facts][events]")
{
    GameData data;
    data.facts.resize(W8_FACT_SLOT_COUNT);
    data.facts[W8_FACT_MOOK_MOOK_PC_SWAP_COMPLETE].symbolic_name = "Parent";
    data.facts[W8_FACT_QUEST_MOOK_BUDDY_TELLS_OF_CM].symbolic_name = "Child";
    Game game{data};
    game.status.log_fact_checks = true;
    std::vector<std::string> notices;
    game.events.observe(
        [&](const Event& event) { notices.push_back(std::get<TextNotice>(event).text); });
    game.setFact(W8_FACT_MOOK_MOOK_PC_SWAP_COMPLETE, 1);
    REQUIRE(notices == std::vector<std::string>{"Checking fact Parent which is TRUE",
                                                "Checking fact Parent which is TRUE",
                                                "Checking fact Child which is TRUE",
                                                "Checking fact Child which is TRUE",
                                                "Child set to TRUE", "Parent set to TRUE"});
    game.events.observe({});
}

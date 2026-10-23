// Links only the rules layer: a rules dependency on the engine fails to link here.
#include <initializer_list>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"

namespace {
W8Party PartyWithPool(std::initializer_list<int> item_ids)
{
    W8Party party;
    for (const int id : item_ids) party.item_pool.push_back({.iItemNo = id});
    return party;
}

int PoolItem(const W8Party& party, unsigned int slot)
{
    return party.item_pool[slot].iItemNo;
}
} // namespace

TEST_CASE("Taking or emptying a pool entry closes the pool up", "[party][storage]")
{
    auto party = PartyWithPool({10, 11, 12});
    REQUIRE(party.take({W8ItemStorage::Pool, 0, 1}).iItemNo == 11);
    REQUIRE(party.item_pool.size() == 2);
    REQUIRE(party.itemAt({W8ItemStorage::Pool, 0, 2}) == nullptr);
    REQUIRE((PoolItem(party, 0) == 10 && PoolItem(party, 1) == 12));

    // Moving a pool entry into an empty backpack slot closes the pool too.
    party.exchange({W8ItemStorage::Pool, 0, 0}, {W8ItemStorage::Backpack, 0, 0}, -1);
    REQUIRE((party.item_pool.size() == 1 && PoolItem(party, 0) == 12));
    REQUIRE(party.characters[0].backpack[0].iItemNo == 10);
}

TEST_CASE("An exchange that empties two pool entries closes both", "[party][storage]")
{
    auto party = PartyWithPool({10, 11, 12, 13});
    party.item_pool[1] = party.item_pool[2] = {};
    // Closing the earlier entry first would shift item 13 into the later index.
    party.exchange({W8ItemStorage::Pool, 0, 1}, {W8ItemStorage::Pool, 0, 2}, -1);
    REQUIRE(party.item_pool.size() == 2);
    REQUIRE((PoolItem(party, 0) == 10 && PoolItem(party, 1) == 13));
}

TEST_CASE("The hand forgets where its item came from once it is emptied", "[party][storage]")
{
    auto party = PartyWithPool({10});
    party.exchange({W8ItemStorage::Pool, 0, 0}, {W8ItemStorage::Hand}, 3);
    REQUIRE(party.held.item.iItemNo == 10);
    REQUIRE(
        (party.held.origin == W8ItemLocation{W8ItemStorage::Pool, 0, 0} && party.held.actor == 3));
    REQUIRE(party.item_pool.size() == 0);

    REQUIRE(party.take({W8ItemStorage::Hand}).iItemNo == 10);
    REQUIRE(party.held.item.iItemNo == -1);
    REQUIRE((party.held.origin == W8ItemLocation{} && party.held.actor == -1));
}

TEST_CASE("An item use is abandoned once its pool entry moves", "[party][actions]")
{
    auto party = PartyWithPool({10, 11, 12});
    const auto use = ItemUseAt(party, {W8ItemStorage::Pool, 0, 1});
    REQUIRE(ResolveItemUse(party, use) == &party.item_pool[1]);
    // A change of quantity keeps the use.
    party.item_pool[1].stack_count = 3;
    REQUIRE(ResolveItemUse(party, use) == &party.item_pool[1]);
    // Taking an earlier entry closes the pool up and moves item 12 into the use's location.
    party.take({W8ItemStorage::Pool, 0, 0});
    REQUIRE(ResolveItemUse(party, use) == nullptr);
}

TEST_CASE("A berserk choice yields to attack while the member cannot berserk", "[party][actions]")
{
    ActionChoices choices{};
    choices.chooseBasic(W8_ACTION_BERSERK, 0);
    W8Action committed{W8_ACTION_BERSERK, 0};

    ReconcileBerserkChoice(choices, committed, false);
    REQUIRE(choices.basic_kind == W8_ACTION_ATTACK);
    REQUIRE(committed.kind == W8_ACTION_ATTACK);

    ReconcileBerserkChoice(choices, committed, true);
    REQUIRE(choices.basic_kind == W8_ACTION_BERSERK);
    REQUIRE(committed.kind == W8_ACTION_BERSERK);

    // A plain attack choice stays an attack when berserk becomes possible.
    choices.chooseBasic(W8_ACTION_ATTACK, 0);
    committed = {W8_ACTION_DEFEND, 0};
    ReconcileBerserkChoice(choices, committed, true);
    REQUIRE(choices.basic_kind == W8_ACTION_ATTACK);
    REQUIRE(committed.kind == W8_ACTION_DEFEND);
}

TEST_CASE("Pool placement preserves stack quantities across growth and refusal",
          "[party][inventory]")
{
    GameData data;
    auto& record = data.items.emplace_back();
    record.quantity_kind = W8_ITEM_QUANTITY_STACK;
    record.maximum_quantity = 100;
    W8Party party;
    Rng rng;
    Events events;
    PartyStorage storage{data, party, rng, events};

    SECTION("The destination is a pool item")
    {
        party.item_pool = {{.iItemNo = 0, .stack_count = 200}};
        W8ItemInstance incoming{.iItemNo = 0, .stack_count = 5};
        REQUIRE(storage.placeInPool(OwnedItem(incoming), false, false));
        REQUIRE(incoming.iItemNo == -1);
        REQUIRE(party.item_pool.size() == 3);
        REQUIRE(party.item_pool[0].stack_count == 5);
        REQUIRE(party.item_pool[1].stack_count == 100);
        REQUIRE(party.item_pool[2].stack_count == 100);
    }
    SECTION("A partial merge stores the remaining units separately")
    {
        party.item_pool = {{.iItemNo = 0, .stack_count = 98}};
        W8ItemInstance incoming{.iItemNo = 0, .stack_count = 5};
        REQUIRE(storage.placeInPool(OwnedItem(incoming), false, false));
        REQUIRE(incoming.iItemNo == -1);
        REQUIRE(party.item_pool.size() == 2);
        REQUIRE(party.item_pool[0].stack_count == 3);
        REQUIRE(party.item_pool[1].stack_count == 100);
    }
    SECTION("A full pool merges what fits and leaves the remainder with its owner")
    {
        party.item_pool.reserve(1000);
        party.item_pool.assign(W8_PARTY_POOL_LIMIT,
                               W8ItemInstance{.iItemNo = 0, .stack_count = 100});
        party.item_pool[0].stack_count = 98;
        W8ItemInstance incoming{.iItemNo = 0, .stack_count = 5};
        REQUIRE_FALSE(storage.placeInPool(OwnedItem(incoming), false, false));
        REQUIRE(incoming.stack_count == 3);
        REQUIRE(party.item_pool.size() == W8_PARTY_POOL_LIMIT);
        REQUIRE(party.item_pool[0].stack_count == 100);
    }
    SECTION("Both transfer orders leave a refused held item in its source")
    {
        auto& character = party.characters[0];
        for (auto& item : character.backpack) item = {.iItemNo = 0, .stack_count = 100};
        party.item_pool.assign(W8_PARTY_POOL_LIMIT,
                               W8ItemInstance{.iItemNo = 0, .stack_count = 100});
        party.held.item = {.iItemNo = 0, .stack_count = 5};
        for (const bool party_first : {false, true}) {
            REQUIRE_FALSE(
                storage.placeWithCharacterOrPool({character, 0}, PartyItem({W8ItemStorage::Hand}),
                                                 party_first, false, false, PlayPhase::Creation));
            REQUIRE(party.held.item.stack_count == 5);
            REQUIRE(party.item_pool.size() == W8_PARTY_POOL_LIMIT);
        }
    }
    SECTION("A merged equipment stack is the actual acquisition receiver")
    {
        auto& character = party.characters[0];
        character.EquippedItem[8] = {.iItemNo = 0, .stack_count = 90};
        W8ItemInstance incoming{.iItemNo = 0, .stack_count = 5};
        const auto placed = storage.placeWithCharacter({character, 0}, OwnedItem(incoming), false,
                                                       false, false, PlayPhase::Creation);
        REQUIRE(placed);
        REQUIRE(placed->stored.storage == W8ItemStorage::None);
        REQUIRE(placed->received);
        REQUIRE(placed->received->location == W8ItemLocation{W8ItemStorage::Equipment, 0, 8});
        REQUIRE(&placed->received->get(party) == &character.EquippedItem[8]);
        REQUIRE(character.EquippedItem[8].stack_count == 95);
    }
    SECTION("An outsider's acquisition receiver belongs to its own character")
    {
        W8Character outsider{};
        outsider.EquippedItem[10] = {.iItemNo = 0, .stack_count = 90};
        W8ItemInstance incoming{.iItemNo = 0, .stack_count = 5};
        const auto placed =
            storage.placeWithCharacter({outsider, std::nullopt}, OwnedItem(incoming), false, false,
                                       false, PlayPhase::Creation);
        REQUIRE(placed);
        REQUIRE(placed->received);
        REQUIRE_FALSE(placed->received->location);
        REQUIRE(&placed->received->get(party) == &outsider.EquippedItem[10]);
        REQUIRE(outsider.EquippedItem[10].stack_count == 95);
    }
    SECTION("Consuming an earlier source preserves the receiving pool location")
    {
        party.item_pool = {{.iItemNo = 0, .stack_count = 5}, {.iItemNo = 0, .stack_count = 90}};
        const auto placed =
            storage.placeInPool(PartyItem({W8ItemStorage::Pool, 0, 0}), false, false);
        REQUIRE(placed);
        REQUIRE(party.item_pool.size() == 1);
        REQUIRE(party.item_pool[0].stack_count == 95);
        REQUIRE(placed->received->location == W8ItemLocation{W8ItemStorage::Pool, 0, 0});
        party.item_pool.reserve(1000);
        REQUIRE(&placed->received->get(party) == &party.item_pool[0]);
    }
    SECTION("An existing row can move within a full pool")
    {
        party.item_pool.assign(W8_PARTY_POOL_LIMIT,
                               W8ItemInstance{.iItemNo = 0, .stack_count = 100});
        party.item_pool.back().stack_count = 5;
        const auto placed =
            storage.placeInPool(PartyItem({W8ItemStorage::Pool, 0, 499}), false, false);
        REQUIRE(placed);
        REQUIRE(party.item_pool.size() == W8_PARTY_POOL_LIMIT);
        REQUIRE(party.item_pool.front().stack_count == 5);
        REQUIRE(placed->stored == W8ItemLocation{W8ItemStorage::Pool, 0, 0});
    }
    SECTION("Invalid pool insertion preserves the held item")
    {
        party.held.item = {.iItemNo = 0, .stack_count = 5};
        REQUIRE_FALSE(storage.insertHeldInPool(1));
        REQUIRE(party.item_pool.empty());
        REQUIRE(party.held.item.stack_count == 5);
    }
    SECTION("The source is a pool item")
    {
        party.item_pool = {{.iItemNo = 0, .stack_count = 5}};
        auto& character = party.characters[0];
        character.EquippedItem[0] = {.iItemNo = 0, .stack_count = 200};
        REQUIRE(storage.placeWithCharacter({character, 0}, PartyItem({W8ItemStorage::Pool, 0, 0}),
                                           false, false, false, PlayPhase::Creation));
        REQUIRE(character.EquippedItem[0].stack_count == 100);
        REQUIRE(character.backpack[0].stack_count == 5);
        REQUIRE(party.item_pool.size() == 1);
        REQUIRE(party.item_pool[0].stack_count == 100);
    }
}

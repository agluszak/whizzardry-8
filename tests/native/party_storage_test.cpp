// Links only the rules layer: a rules dependency on the engine fails to link here.
#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"

namespace {
W8Party PartyWithPool(std::initializer_list<int> item_ids)
{
    W8Party party;
    for (const int id : item_ids) REQUIRE(party.insertIntoPool(party.item_count, {.iItemNo = id}));
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
    REQUIRE(party.item_count == 2);
    REQUIRE((PoolItem(party, 0) == 10 && PoolItem(party, 1) == 12 && PoolItem(party, 2) == -1));

    // Moving a pool entry into an empty backpack slot closes the pool too.
    party.exchange({W8ItemStorage::Pool, 0, 0}, {W8ItemStorage::Backpack, 0, 0}, -1);
    REQUIRE((party.item_count == 1 && PoolItem(party, 0) == 12));
    REQUIRE(party.characters[0].backpack[0].iItemNo == 10);
}

TEST_CASE("An exchange that empties two pool entries closes both", "[party][storage]")
{
    auto party = PartyWithPool({10, 11, 12, 13});
    party.item_pool[1] = party.item_pool[2] = {};
    // Closing the earlier entry first would shift item 13 into the later index.
    party.exchange({W8ItemStorage::Pool, 0, 1}, {W8ItemStorage::Pool, 0, 2}, -1);
    REQUIRE(party.item_count == 2);
    REQUIRE((PoolItem(party, 0) == 10 && PoolItem(party, 1) == 13));
}

TEST_CASE("The hand forgets where its item came from once it is emptied", "[party][storage]")
{
    auto party = PartyWithPool({10});
    party.exchange({W8ItemStorage::Pool, 0, 0}, {W8ItemStorage::Hand}, 3);
    REQUIRE(party.held.item.iItemNo == 10);
    REQUIRE(
        (party.held.origin == W8ItemLocation{W8ItemStorage::Pool, 0, 0} && party.held.actor == 3));
    REQUIRE(party.item_count == 0);

    REQUIRE(party.take({W8ItemStorage::Hand}).iItemNo == 10);
    REQUIRE(party.held.item.iItemNo == -1);
    REQUIRE((party.held.origin == W8ItemLocation{} && party.held.actor == -1));
}

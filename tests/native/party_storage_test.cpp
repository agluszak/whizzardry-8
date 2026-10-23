// Links only the rules layer: a rules dependency on the engine fails to link here.
#include <array>
#include <initializer_list>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"

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

TEST_CASE("Party selection prefers awake members but falls back to any occupied slot", "[party]")
{
    W8Party party;
    REQUIRE_FALSE(party.firstSelectableSlot());
    for (const auto slot : {0, 2, 5}) party.members[slot].emplace();
    party.characters[0].hp_current = 0;
    party.characters[2].hp_current = 1;
    party.characters[2].highest_condition = W8_CONDITION_ASLEEP;
    party.characters[5].hp_current = 1;
    party.characters[5].highest_condition = W8_CONDITION_NONE;
    REQUIRE(party.firstSelectableSlot() == 5);

    party.characters[5].hp_current = 0;
    party.characters[5].highest_condition = W8_CONDITION_DEAD;
    REQUIRE(party.firstSelectableSlot() == 2);
    party.characters[2].hp_current = 0;
    party.characters[2].highest_condition = W8_CONDITION_DEAD;
    REQUIRE(party.firstSelectableSlot() == 0);
    party.members[0].reset();
    REQUIRE(party.firstSelectableSlot() == 2);
}

TEST_CASE("Random party selection keeps cyclic weighting and the gameplay draw", "[party][rng]")
{
    W8Party party;
    const std::array slots{0, 3, 7};
    for (const int slot : slots) {
        party.members[slot].emplace();
        party.characters[slot].hp_current = 1;
        party.characters[slot].highest_condition = W8_CONDITION_NONE;
    }
    std::vector<Rng::Draw> draws;
    Rng rng{2433};
    rng.trace(&draws);
    Rng expected{2433};
    const auto ordinal = expected.below(8);
    REQUIRE(party.randomSlot(rng) == slots[ordinal % slots.size()]);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().range == 8);
    REQUIRE(rng.next() == expected.next());
}

TEST_CASE("Random party selection only relaxes an empty eligibility stage", "[party][rng]")
{
    W8Party party;
    std::vector<Rng::Draw> draws;
    Rng rng;
    rng.trace(&draws);
    unsigned int expected_draws = 0;
    for (const auto fallback : {PartySelectionFallback::None, PartySelectionFallback::Living,
                                PartySelectionFallback::AnyMember}) {
        draws.clear();
        REQUIRE_FALSE(party.randomSlot(rng, fallback));
        ++expected_draws;
        REQUIRE(draws.size() == expected_draws);
        for (const auto& draw : draws) REQUIRE(draw.range == 8);
    }

    for (const int slot : {0, 2, 5}) party.members[slot].emplace();
    party.characters[0].hp_current = 0;
    party.characters[0].highest_condition = W8_CONDITION_DEAD;
    party.characters[2].hp_current = 1;
    party.characters[2].highest_condition = W8_CONDITION_ASLEEP;
    party.characters[5].hp_current = 1;
    party.characters[5].highest_condition = W8_CONDITION_NONE;
    draws.clear();
    REQUIRE(party.randomSlot(rng, PartySelectionFallback::AnyMember) == 5);
    REQUIRE(draws.size() == 1);
    draws.clear();
    REQUIRE_FALSE(party.randomSlot(rng, PartySelectionFallback::None, 5));
    REQUIRE(draws.size() == 1);
    draws.clear();
    REQUIRE(party.randomSlot(rng, PartySelectionFallback::Living, 5) == 2);
    REQUIRE(draws.size() == 2);
    party.members[2].reset();
    draws.clear();
    REQUIRE(party.randomSlot(rng, PartySelectionFallback::AnyMember, 5) == 0);
    REQUIRE(draws.size() == 3);
}

TEST_CASE("Living-party reactions draw uniformly only when there is a choice", "[party][rng]")
{
    W8Party party;
    std::vector<Rng::Draw> draws;
    Rng rng{2433};
    rng.trace(&draws);
    REQUIRE_FALSE(party.randomLivingSlot(rng));
    party.members[0].emplace();
    party.characters[0].hp_current = 1;
    party.characters[0].highest_condition = W8_CONDITION_ASLEEP;
    REQUIRE(party.randomLivingSlot(rng) == 0);
    REQUIRE_FALSE(party.randomLivingSlot(rng, 0));
    REQUIRE(draws.empty());

    for (const int slot : {2, 7}) {
        party.members[slot].emplace();
        party.characters[slot].hp_current = 1;
        party.characters[slot].highest_condition = W8_CONDITION_NONE;
    }
    Rng expected{2433};
    const std::array slots{0, 2, 7};
    REQUIRE(party.randomLivingSlot(rng) == slots[expected.below(3)]);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().range == 3);
    draws.clear();
    REQUIRE(party.randomLivingSlot(rng, 2, true) == 7);
    REQUIRE(draws.empty());
    party.characters[7].hp_current = 0;
    REQUIRE_FALSE(party.randomLivingSlot(rng, 2, true));
    party.characters[2].highest_condition = W8_CONDITION_DEAD;
    REQUIRE(party.randomLivingSlot(rng) == 0);
    REQUIRE(draws.empty());
    REQUIRE(rng.next() == expected.next());
}

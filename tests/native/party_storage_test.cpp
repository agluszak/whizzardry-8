// Links only the rules layer: a rules dependency on the engine fails to link here.
#include <catch2/catch_test_macros.hpp>

#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"

namespace {
// One stacking item with room for ten.
GameData StackOfTen()
{
    GameData data;
    auto& record = data.items.emplace_back();
    record.quantity_kind = W8_ITEM_QUANTITY_STACK;
    record.maximum_quantity = 10;
    return data;
}

W8Character EmptyCharacter()
{
    W8Character character{};
    for (auto& item : character.EquippedItem) item = {};
    for (auto& item : character.backpack) item = {};
    return character;
}
} // namespace

TEST_CASE("A merge moves only what fits and touches nothing else", "[inventory][rules]")
{
    const auto data = StackOfTen();
    W8ItemInstance destination{.iItemNo = 0, .stack_count = 7};
    W8ItemInstance source{.iItemNo = 0, .stack_count = 5};
    REQUIRE(MergeStack(data, destination, source) == 3);
    REQUIRE((destination.stack_count == 10 && source.stack_count == 2));

    // Identification has to agree, and a legacy overfull stack takes nothing more.
    W8ItemInstance unidentified{.iItemNo = 0, .stack_count = 1, .identified = true};
    REQUIRE(MergeStack(data, unidentified, source) == 0);
    W8ItemInstance overfull{.iItemNo = 0, .stack_count = 14};
    REQUIRE(MergeStack(data, overfull, source) == 0);
    REQUIRE((overfull.stack_count == 14 && source.stack_count == 2));
}

TEST_CASE("A character keeps what it cannot place with the giver", "[inventory][rules]")
{
    const auto data = StackOfTen();
    auto character = EmptyCharacter();
    for (auto& item : character.backpack) item = {.iItemNo = 0, .stack_count = 10};
    character.backpack[3].stack_count = 6;
    W8ItemInstance item{.iItemNo = 0, .stack_count = 9};

    // Four units fit in the one open stack; the rest has no free slot.
    REQUIRE_FALSE(PlaceWithCharacter(data, character, item, false, true, true));
    REQUIRE(character.backpack[3].stack_count == 10);
    REQUIRE(item.stack_count == 5);

    character.backpack[5] = {};
    const auto placed = PlaceWithCharacter(data, character, item, false, true, true);
    REQUIRE(placed);
    REQUIRE((placed->storage == W8ItemStorage::Backpack && placed->slot == 5 && !placed->merged));
    REQUIRE(character.backpack[5].stack_count == 5);
    REQUIRE(item.iItemNo == -1);
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

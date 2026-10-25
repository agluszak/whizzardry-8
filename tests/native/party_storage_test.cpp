// Links only the rules layer: a rules dependency on the engine fails to link here.
#include <array>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
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

TEST_CASE(
    "Party modifiers fold combat effects without losing byte arithmetic or recovery eligibility",
    "[party][rules][effects]")
{
    W8Party party;
    std::array<W8EffectSlot, 12> effects{};
    std::array<W8EffectSlot, 9> combat{};
    std::array<W8EffectSlot, 6> radius{};
    effects[0].Activate(0x20, 3, 1); // +2 armor before the fight's penalties.
    combat[0].Activate(0x31, 5, 1);
    combat[1].Activate(0x31, 2, 1);
    radius[0].Activate(2, 1, 1);
    auto& bard = party.characters[7];
    party.members[7].emplace();
    bard.iProfession = W8_PROFESSION_BARD;
    bard.iRace = W8_RACE_HUMAN;
    bard.hp_current = 1;
    party.refreshModifiers(effects, combat, radius);
    REQUIRE(party.modifiers.armor_matchup == -5);
    REQUIRE(party.modifiers.armor_flat == 2);
    REQUIRE(party.modifiers.hit_bonus == 2);
    REQUIRE(party.modifiers.boost_health_regen == 1);
    REQUIRE(party.modifiers.boost_stamina_regen == 1);
    REQUIRE(party.modifiers.boost_spell_regen == 1);
    bard.highest_condition = W8_CONDITION_ASLEEP;
    party.refreshModifiers(effects, {}, {});
    REQUIRE(party.modifiers.armor_matchup == 2);
    REQUIRE(party.modifiers.hit_bonus == 0);
    REQUIRE(party.modifiers.boost_health_regen == 0);
}

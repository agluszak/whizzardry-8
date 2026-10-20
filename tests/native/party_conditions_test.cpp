// Links only the rules layer: conditions, death and settlement run without the engine.
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/rule_context.h"

namespace {
// Records what the rules owe the game, so a test can check it happened once and in order.
struct RecordingHost final : PartyHost {
    std::vector<std::string> calls;
    bool combat_revival_owed = false;

    PartyStatRules statRules() override { return {}; }
    bool worldSuspended() override { return false; }
    int levelBand() override { return 0; }
    std::optional<int> swallowerLevel(const W8CharacterConditionRecord&) override
    {
        return std::nullopt;
    }
    bool spendFirstCombatRevival(unsigned int) override
    {
        calls.emplace_back("revival");
        return std::exchange(combat_revival_owed, false);
    }
    bool unequipUnusable(unsigned int) override { return false; }
    void handsChanged(unsigned int) override {}
    void overloaded(unsigned int) override { calls.emplace_back("overloaded"); }
    void conditionStarted(unsigned int, W8Condition) override {}
    void conditionLifted(unsigned int, W8Condition) override {}
    void standingChanged(unsigned int) override {}
    void enchantmentChanged(unsigned int, W8EnchantmentSlot) override {}
    void died(unsigned int) override { calls.emplace_back("died"); }
    void incapacitated(unsigned int) override { calls.emplace_back("incapacitated"); }
    void conditionReaction(unsigned int) override { calls.emplace_back("reaction"); }
    void recoveryReaction(unsigned int, W8Condition) override {}

    long count(const std::string& call) const { return std::ranges::count(calls, call); }
};

struct PartyFixture {
    GameData data{.strings = std::vector<std::string>(0x400),
                  .spells = std::vector<W8SpellRuntimeRecord>(0x72)};
    W8Party party;
    Rng rng{1};
    Events events;
    RecordingHost host;
    RuleContext rules{data, party, rng, events, true};

    PartyFixture()
    {
        data.strings[0x173] = "cheats death!";
        data.strings[0x180] = "is fearless.";
        data.strings[0x392] = "dies";
        party.members[0].emplace();
        auto& character = party.characters[0];
        character.fInParty = true;
        character.iRace = W8_RACE_HUMAN;
        character.gender = W8_GENDER_MALE;
        character.iProfession = W8_PROFESSION_FIGHTER;
        for (auto& item : character.EquippedItem) item.iItemNo = -1;
        for (auto& item : character.backpack) item.iItemNo = -1;
        for (auto& attribute : character.attributes) attribute.base = 50;
        SettleParty(rules, host);
        character.hp_current = character.uiHPMax;
        events = {};
    }
    W8Character& member() { return party.characters[0]; }
    long deaths() const
    {
        return std::ranges::count_if(events.pending(), [](const Event& event) {
            return std::holds_alternative<CharacterDied>(event);
        });
    }
};
} // namespace

TEST_CASE_METHOD(PartyFixture, "A ceiling that exhausts hit points kills the member once",
                 "[rules][conditions]")
{
    member().uiCondition[W8_CONDITION_POISONED] = 30;
    member().condition_argument = 4;
    // The pool was under a higher ceiling than the member's state now supports.
    member().uiHPMax += member().hp_current;

    SettleParty(rules, host);
    REQUIRE(member().uiCondition[W8_CONDITION_DEAD] == W8_CONDITION_INDEFINITE);
    REQUIRE(member().highest_condition == W8_CONDITION_DEAD);
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);
    REQUIRE((member().hp_current == 0 && member().stamina == 0 && member().death_count == 1));
    REQUIRE((deaths() == 1 && host.count("died") == 1 && host.count("incapacitated") == 1));

    // Dying again changes nothing: the death's consequences belong to the first one.
    REQUIRE_FALSE(InflictCondition(
        rules, host, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    SettleParty(rules, host);
    REQUIRE((deaths() == 1 && host.count("died") == 1 && member().death_count == 1));
}

TEST_CASE_METHOD(PartyFixture, "Cheating death leaves the member unconscious instead",
                 "[rules][conditions]")
{
    member().iProfession = W8_PROFESSION_VALKYRIE;
    host.combat_revival_owed = true;
    member().hp_current = 1;

    REQUIRE(CheatsDeath(member()));
    REQUIRE_FALSE(InflictCondition(rules, host, 0, {.condition = W8_CONDITION_DEAD}));
    REQUIRE(member().uiCondition[W8_CONDITION_DEAD] == 0);
    REQUIRE(member().uiCondition[W8_CONDITION_UNCONSCIOUS] != 0);
    REQUIRE(member().highest_condition == W8_CONDITION_UNCONSCIOUS);
    // The first revival in a fight restores at least half of the maximum.
    REQUIRE(static_cast<int>(member().hp_current) >= member().uiHPMax / 2);
    REQUIRE((host.count("revival") == 1 && host.count("died") == 0 && deaths() == 0));

    // Unconscious long enough, the member has no more deaths to cheat.
    member().uiCondition[W8_CONDITION_UNCONSCIOUS] = 7;
    REQUIRE_FALSE(CheatsDeath(member()));
}

TEST_CASE_METHOD(PartyFixture, "Traits and poison strength refuse a condition",
                 "[rules][conditions]")
{
    REQUIRE_FALSE(
        InflictCondition(rules, host, 0, {.condition = W8_CONDITION_POISONED, .duration = 10}));
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);

    member().iProfession = W8_PROFESSION_SAMURAI;
    REQUIRE_FALSE(
        InflictCondition(rules, host, 0, {.condition = W8_CONDITION_AFRAID, .duration = 10}));
    REQUIRE(member().uiCondition[W8_CONDITION_AFRAID] == 0);
    const auto& refusal = std::get<CharacterNotice>(events.pending().back());
    REQUIRE(refusal.text == "is fearless.");
    REQUIRE(host.count("reaction") == 0);
}

TEST_CASE_METHOD(PartyFixture, "A longer condition is not shortened, and aging lifts it",
                 "[rules][conditions]")
{
    REQUIRE(InflictCondition(rules, host, 0, {.condition = W8_CONDITION_ASLEEP, .duration = 20}));
    REQUIRE_FALSE(
        InflictCondition(rules, host, 0, {.condition = W8_CONDITION_ASLEEP, .duration = 5}));
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 20);
    REQUIRE(host.count("reaction") == 1);

    AgeCondition(rules, host, 0, W8_CONDITION_ASLEEP, 15);
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 5);
    AgeCondition(rules, host, 0, W8_CONDITION_ASLEEP, 5);
    REQUIRE(member().uiCondition[W8_CONDITION_ASLEEP] == 0);
    REQUIRE(member().highest_condition == W8_CONDITION_NONE);
}

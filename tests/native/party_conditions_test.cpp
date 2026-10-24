// Conditions, death and settlement on an empty level, without screens or retail data.
#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scoped_combat.h"
#include "scoped_game.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/portrait_animation.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/load_category.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"
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
    GameData data = ConditionData();
    ScopedGame scope{data};
    Game& game = scope.game;

    PartyFixture()
    {
        game.level.replacePrimary(std::make_unique<World>());
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
    game.monster_manager_entries[0].portrait.blink_closing = true;
    // The pool was under a higher ceiling than the member's state now supports.
    member().uiHPMax += member().hp_current;

    SettleActiveParty(game);
    REQUIRE(member().uiCondition[W8_CONDITION_DEAD] == W8_CONDITION_INDEFINITE);
    REQUIRE(member().highest_condition == W8_CONDITION_DEAD);
    REQUIRE(game.monster_manager_entries[0].portrait.target_pose == 2);
    REQUIRE_FALSE(game.monster_manager_entries[0].portrait.blink_closing);
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);
    REQUIRE((member().hp_current == 0 && member().stamina == 0 && member().death_count == 1));
    REQUIRE(deaths() == 1);

    // Dying again changes nothing: the death's consequences belong to the first one.
    REQUIRE_FALSE(InflictCondition(
        game, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    SettleActiveParty(game);
    REQUIRE((deaths() == 1 && member().death_count == 1));
}

TEST_CASE_METHOD(PartyFixture, "Death settles the party fight before returning",
                 "[conditions][events]")
{
    ScopedCombat fight{true};
    game.party.rows[0].executing.kind = W8_ACTION_ATTACK;
    game.combat->characters[0].phase = 2;
    member().uiCondition[W8_CONDITION_POISONED] = 30;
    member().condition_argument = 4;

    REQUIRE(InflictCondition(
        game, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    REQUIRE(game.combat->pending_death_count == 1);
    REQUIRE(game.combat->characters[0].dead);
    REQUIRE(game.combat->characters[0].phase == 0);
    REQUIRE(game.party.rows[0].executing.kind == W8_ACTION_NONE);
    REQUIRE(member().hp_current == 0);
    REQUIRE(member().stamina == 0);
    REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);
    REQUIRE(deaths() == 1);
}

TEST_CASE_METHOD(PartyFixture, "Death observers see completed combat and condition consequences",
                 "[conditions][events]")
{
    ScopedCombat fight{true};
    data.npcs.emplace_back();
    auto npc = std::make_shared<W8NpcState>();
    npc->record = &data.npcs.back();
    game.story.npcs.push_back(npc);
    game.party.members[0]->npc_index = 0;
    game.party.rows[0].executing.kind = W8_ACTION_ATTACK;
    game.combat->characters[0].phase = 2;
    member().uiCondition[W8_CONDITION_POISONED] = 30;
    member().condition_argument = 4;
    unsigned int notifications = 0;
    unsigned int death_notifications = 0;
    game.events.observe([&](const Event& event) {
        ++notifications;
        if (std::holds_alternative<CharacterDied>(event)) ++death_notifications;
        REQUIRE(npc->spawned == 1);
        REQUIRE(game.combat->pending_death_count == 1);
        REQUIRE(game.combat->characters[0].dead);
        REQUIRE(game.combat->characters[0].phase == 0);
        REQUIRE(game.party.rows[0].executing.kind == W8_ACTION_NONE);
        REQUIRE(member().hp_current == 0);
        REQUIRE(member().stamina == 0);
        REQUIRE(member().uiCondition[W8_CONDITION_POISONED] == 0);
    });
    REQUIRE(InflictCondition(
        game, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    REQUIRE(notifications > 1);
    REQUIRE(death_notifications == 1);
    REQUIRE(game.events.pending().empty());
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

TEST_CASE_METHOD(PartyFixture, "Absorbed damage publishes its notice after spending the shield",
                 "[conditions][damage][events]")
{
    auto& shield = member().enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL];
    shield.turns = 1;
    shield.magnitude = 20;
    const auto hp = member().hp_current;
    unsigned int notices = 0;
    game.events.observe([&](const Event& event) {
        REQUIRE(std::holds_alternative<CharacterNotice>(event));
        REQUIRE(shield.magnitude == 15);
        REQUIRE(member().hp_current == hp);
        ++notices;
    });
    REQUIRE(ApplyDamageToCharacter(0, 5, false, true, false, nullptr, false) == 0);
    REQUIRE(notices == 1);
}

TEST_CASE_METHOD(PartyFixture,
                 "Lethal damage completes without a screen and publishes settled outcomes",
                 "[conditions][damage][events]")
{
    ScopedCombat fight{true};
    member().hp_current = 5;
    member().stamina = member().uiStaminaMax = 500;
    unsigned int hits = 0;
    unsigned int deaths = 0;
    game.events.observe([&](const Event& event) {
        REQUIRE(member().hp_current == 0);
        REQUIRE(member().stamina == 0);
        REQUIRE(game.combat->characters[0].dead);
        REQUIRE(game.combat->pending_death_count == 1);
        if (const auto* hit = std::get_if<CharacterDamaged>(&event)) {
            ++hits;
            REQUIRE(hit->amount == 5);
            REQUIRE(hit->fatal);
        }
        if (std::holds_alternative<CharacterDied>(event)) ++deaths;
    });
    REQUIRE(ApplyDamageToCharacter(0, 99, false, true, false, nullptr, false) == 5);
    REQUIRE(hits == 1);
    REQUIRE(deaths == 1);
}

TEST_CASE_METHOD(PartyFixture, "Candidate conditions change only its own formation",
                 "[conditions][formation]")
{
    Game candidate{data};
    candidate.level.replacePrimary(std::make_unique<World>());
    candidate.party.members[0].emplace();
    candidate.party.characters[0] = member();
    candidate.party.formation.placeCharacter(0);
    const auto outgoing = game.party.formation;
    const auto outgoing_events = game.events.pending().size();

    REQUIRE(InflictCondition(candidate, 0, {.condition = W8_CONDITION_ASLEEP, .duration = 5}));
    REQUIRE(candidate.party.formation.rowOf(0) == 0);
    REQUIRE(InflictCondition(
        candidate, 0, {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE}));
    REQUIRE_FALSE(candidate.party.formation.cellOf(0));
    REQUIRE(candidate.party.formation.positions[0].revival_row == 0);
    REQUIRE(game.party.formation == outgoing);
    REQUIRE(member().highest_condition == W8_CONDITION_NONE);
    REQUIRE(game.events.pending().size() == outgoing_events);
}

TEST_CASE("Stamina spending owns scaling, clamping and the fatigue band", "[conditions][fatigue]")
{
    W8Character character{};
    character.hp_current = 1;
    character.stamina = character.uiStaminaMax = 100;
    character.load_category = W8_LOAD_HEAVY;
    character.enchantments[W8_ENCHANTMENT_HASTE].turns = 1;
    character.uiCondition[W8_CONDITION_SLOWED] = 1;
    // Slowing overrides haste: heavy load's 100% penalty becomes 75%.
    REQUIRE_FALSE(character.spendStamina(20, true));
    REQUIRE(character.stamina == 65);
    REQUIRE(character.spendStamina(100, false));
    REQUIRE(character.stamina == 0);
    REQUIRE(character.fatigue_band == 4);
    REQUIRE_FALSE(character.spendStamina(10, true));
}

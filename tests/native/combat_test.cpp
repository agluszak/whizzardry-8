#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "scoped_combat.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/monster_roster.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/simulation_sink.h"
#include "wiz8/world/world.h"

namespace {

struct ScopedSettings {
    W8GameSettings previous = g_settings;
    ~ScopedSettings() { g_settings = previous; }
};

// A fight in an empty level with two conscious party members, reading the fixture's data.
struct CombatFixture {
    ScopedSettings settings;
    GameData data;
    ScopedGame game_scope{data};
    ScopedCombat combat_scope{true};
    Game& game = game_scope.game;
    Combat& combat = *game.combat;

    CombatFixture()
    {
        game.level.replacePrimary(std::make_unique<World>());
        g_settings.camera_rotation_mode = W8_CAMERA_ROTATION_DISABLED;
        g_settings.verbose_combat_messages = 0;
        for (int slot = 0; slot < 2; ++slot) {
            game.party.members[slot].emplace();
            game.party.characters[slot].hp_current = 10;
        }
    }

    W8MonsterInfo& AddMonster(int location_id, unsigned int phase)
    {
        auto monster = std::make_unique<W8MonsterInfo>();
        monster->location_id = location_id;
        monster->hp_current = 10;
        monster->pCombat = std::make_unique<W8MonsterCombatState>();
        monster->pCombat->phase = phase;
        return *CurrentLevelPopulation().monster_roster.add(std::move(monster), false);
    }
};

/* The fight in a started game with two item records, so inventory edits settle the
   party. No profession or race data is loaded, so the members derive no statistics. */
struct InventoryCombatFixture : CombatFixture {
    InventoryCombatFixture()
    {
        data.items.resize(2);
        game.status.game_started = true;
        for (int slot = 0; slot < 2; ++slot) {
            game.party.characters[slot].fInParty = true;
            game.party.characters[slot].gender = W8_GENDER_UNSET;
        }
    }
};

} // namespace

TEST_CASE_METHOD(CombatFixture, "The party acts before a monster on the same phase")
{
    combat.round_counter = 10;
    combat.characters[0].phase = 10;
    combat.characters[1].phase = 10;
    game.party.characters[0].highest_condition = W8_CONDITION_ASLEEP;
    game.party.rows[0].executing.kind = W8_ACTION_ATTACK;
    AddMonster(7, 10);

    combat.scheduleNextActor(game);

    const auto* scheduled = std::get_if<TurnScheduled>(&combat.turn());
    REQUIRE(scheduled);
    REQUIRE(scheduled->actor == CombatActor{PartyActor{1}});
    // The sleeping member loses its action and counts as having acted.
    REQUIRE(game.party.rows[0].executing.kind == W8_ACTION_NONE);
    REQUIRE(combat.characters[0].phase == 0);
    REQUIRE(combat.characters[0].dead);
}

TEST_CASE_METHOD(CombatFixture, "A turn ended while its action resolves stays idle")
{
    const CombatActor actor = PartyActor{0};
    combat.schedule(actor);
    combat.setStepOf(actor, ActionStep::resolving);
    REQUIRE(combat.resolvingTurnOf(actor));

    // The actor dies during its own swing; the swing then reports finishing.
    combat.endTurnOf(actor);
    combat.setStepOf(actor, ActionStep::finishing);

    REQUIRE(std::holds_alternative<TurnIdle>(combat.turn()));
    REQUIRE_FALSE(combat.actingSlot());
}

TEST_CASE_METHOD(CombatFixture, "Combat waits on a conscious NPC's queued combat script")
{
    REQUIRE(combat.waitingOn(game) == CombatWait::none);

    combat.npc_combat_script_pending[1] = true;
    REQUIRE(combat.waitingOn(game) == CombatWait::npc_script);

    game.party.characters[1].highest_condition = W8_CONDITION_ASLEEP;
    REQUIRE(combat.waitingOn(game) == CombatWait::none);
}

TEST_CASE_METHOD(InventoryCombatFixture,
                 "Swapping both weapon pairs toggles the alternate hand once")
{
    auto& character = game.party.characters[0];
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo = 1;

    game.inventory.commitWeaponSwap(0, {.primary = true, .secondary = true});

    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == 0);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == 1);
    REQUIRE(combat.characters[0].alternate_hand);
}

TEST_CASE_METHOD(InventoryCombatFixture, "Spending one charge keeps a committed use of the item")
{
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_CHARGES;
    data.items[0].initial_quantity = {.base = 0, .count = 1, .sides = 5};
    const W8ItemLocation source{W8ItemStorage::Hand};
    const W8ItemLocation destination{W8ItemStorage::Backpack, 0, 1};
    auto& backpack = game.party.characters[0].backpack;
    game.inventory.restore({}, W8ItemInstance{.iItemNo = 0, .uses_or_charges = 3});
    backpack[1] = {.iItemNo = 0, .uses_or_charges = 4};
    auto& committed = game.party.rows[0].committed;
    committed.kind = W8_ACTION_USE_ITEM;
    committed.detail_block.item_use = {-1, source, 0};
    // The member has acted this round; the use waits for the next one.
    combat.characters[0].dead = true;

    game.inventory.pourHeldUses(0, destination);

    REQUIRE(game.inventory.held()->item.uses_or_charges == 2);
    REQUIRE(backpack[1].uses_or_charges == 5);
    REQUIRE(committed.kind == W8_ACTION_USE_ITEM);
    REQUIRE(committed.detail_block.item_use.item == source);
}

TEST_CASE("A deflected stored-spell missile keeps its outcome outside combat")
{
    const ScopedGame game_scope;
    Game& game = game_scope.game;
    const ScopedGameData missile_data{GameData{.missiles = std::vector<W8MissileTableRecord>(36)}};
    game.level.replacePrimary(std::make_unique<World>());
    World& world = *game.level.primary;
    Monster target(world);
    target.location_id = 7;
    target.nav.movement.location_id = target.location_id;
    auto info = std::make_unique<W8MonsterInfo>();
    info->location_id = target.location_id;
    info->hp_current = 10;
    info->modifiers.missile_deflect_chance = 100;
    world.population->monster_roster.add(std::move(info), false);

    Missile missile(world);
    missile.missile_table_index = 0x23;
    missile.m_Source = {.iType = W8_TARGET_SOURCE_CHARACTER, .iChar = 0};
    missile.combat_slot.iType = W8_TARGET_KIND_MONSTER;
    missile.combat_slot.iMonsterID = target.location_id;

    REQUIRE(world.simulation->missileReached(missile, target.nav));
    REQUIRE(missile.deflected);
    REQUIRE_FALSE(game.combat);

    SECTION("The flight ends without a fight") {}
    SECTION("A fight starts with a different missile outcome")
    {
        game.combat.emplace().missile_hit_result = W8_MISSILE_HIT;
    }

    // Deflection prevents Rocket Blast even without a fight to remember the hit.
    missile.Land();
    REQUIRE(missile.flight_done);
}

TEST_CASE("Weapon reach and berserk eligibility need no installed game", "[combat][rules]")
{
    GameData data;
    data.items.resize(1);
    data.items[0].range_category = W8_RANGE_SHORT;
    W8Character character{};
    character.iProfession = W8_PROFESSION_FIGHTER;
    character.iRace = W8_RACE_HUMAN;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.Hand[0].in_play = true;
    character.Hand[0].uiHolds = 1;
    REQUIRE(GetCharAttackRange(data, &character, 0) == W8_RANGE_SHORT);
    REQUIRE(GetCharAttackRange(data, &character, 2) == W8_RANGE_SHORT);
    REQUIRE(CanCharacterBerserk(data, &character));
    data.items[0].range_category = W8_RANGE_LONG;
    REQUIRE_FALSE(CanCharacterBerserk(data, &character));
    character.Hand[0].in_play = false;
    REQUIRE(GetCharAttackRange(data, &character, 2) == W8_RANGE_NONE);
    REQUIRE_FALSE(CanCharacterBerserk(data, &character));
    // Retail gates against the primary range even when only the secondary can attack.
    character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo = 0;
    character.Hand[1].in_play = true;
    character.Hand[1].uiHolds = 1;
    REQUIRE(GetCharAttackRange(data, &character, 2) == W8_RANGE_LONG);
    REQUIRE(CanCharacterBerserk(data, &character));
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mgs_commands.h"
#include "save_payload.h"
#include "scoped_combat.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "sight_state.h"
#include "temporary_directory.h"
#include "wiz8/character_skills.h"
#include "wiz8/chunk.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_generators.h"
#include "wiz8/monster_roster.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_trade_price.h"
#include "wiz8/platform/input.h"
#include "wiz8/resource_cache.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/load_category.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/save_codec.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/world.h"

// A game in progress with an empty level world.
struct LevelWorldFixture : ScopedGame {
    LevelWorldFixture() { game.level.replacePrimary(std::make_unique<World>()); }
};

TEST_CASE("World-clock rollover preserves seconds and NPC cooldowns", "[timers][npc]")
{
    ScopedGame scope;
    auto& game = scope.game;
    game.surprise_possible = true;
    game.status.world_clock = 0x7fffffffu;
    game.status.world_clock_ms = 999;
    UpdateGameClock(game, 1);
    REQUIRE(game.status.world_clock == 0x80000000u);
    REQUIRE(game.status.world_clock_ms == 0);

    auto npc = std::make_unique<W8NpcState>();
    npc->talk_cooldown_active = true;
    npc->trade_cooldown_active = true;
    npc->talk_cooldown_clock = 0x7ffffffe;
    npc->trade_cooldown_clock = 0x7ffffffe;
    game.story.npcs.push_back(std::move(npc));
    auto& restored = *game.story.npcs.front();
    AdvanceNpcTimers(0);
    REQUIRE(restored.talk_cooldown_active);
    REQUIRE(restored.trade_cooldown_active);
    game.status.world_clock += 0xa8c0u;
    AdvanceNpcTimers(0);
    REQUIRE_FALSE(restored.talk_cooldown_active);
    REQUIRE_FALSE(restored.trade_cooldown_active);

    game.status.world_clock = 0xffffffffu;
    game.status.world_clock_ms = 999;
    UpdateGameClock(game, 1);
    REQUIRE(game.status.world_clock == 0);
    REQUIRE(game.status.world_clock_ms == 0);

    game.surprise_possible = false;
    game.party.members[0].emplace();
    game.party.members[0]->npc_bound = true;
    game.party.members[0]->npc_index = 0;
    game.party.characters[0].hp_current = 1;
    std::vector<Rng::Draw> draws;
    game.rng.trace(&draws);
    for (const std::uint32_t now : {100u, 0xffffffe1u}) {
        game.status.world_clock = now;
        restored.event_clock = static_cast<int>(now + 60u);
        UpdateNpcEvents();
        REQUIRE(draws.empty());
        REQUIRE(restored.event_clock == static_cast<int>(now + 60u));
    }
    game.rng.trace(nullptr);
}

// Item records with no profession, race, gender, attribute or skill restriction.
static std::vector<W8ItemDatabaseRecord> UnrestrictedItems(std::size_t count)
{
    W8ItemDatabaseRecord record{};
    record.profession_mask = 0xffff;
    record.race_mask = 0xffff;
    record.gender_mask = 3;
    record.attribute_requirements[0].stat_id = 0xff;
    record.attribute_requirements[1].stat_id = 0xff;
    record.skill_requirements[0].stat_id = 0xff;
    record.skill_requirements[1].stat_id = 0xff;
    return std::vector<W8ItemDatabaseRecord>(count, record);
}

// Level zero, with an encounter budget between one and twenty.
static GameData EncounterLevelData()
{
    GameData data;
    data.levels.resize(1);
    data.levels[0].minimum_encounter_budget = 1;
    data.levels[0].maximum_encounter_budget = 20;
    return data;
}

// A level script whose masters the test arms directly.
class MasterTestScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void arm(Update update, Capture capture = {})
    {
        addMaster(std::move(update), std::move(capture));
    }
};

static bool masterFunctionMutation()
{
    MasterTestScript script(MakeScriptContext(CurrentGame()));
    std::vector<int> calls;
    const auto later = [&calls](int command) {
        calls.push_back(30 + command);
        return false;
    };
    script.arm(
        [&](int command) {
            calls.push_back(10 + command);
            // Armed during the run, so it waits for the next one.
            if (command == 0) script.arm(later);
            return false;
        },
        [](W8SavedLocationState& state) { state.emplace_back("SavedScript", 17); });
    script.arm([&](int command) {
        calls.push_back(20 + command);
        // A nested script's result cannot overwrite this callback's completion.
        later(1);
        return true;
    });
    script.runMasters();
    REQUIRE((calls == std::vector<int>{10, 20, 31}));
    calls.clear();
    const auto saved = script.captureState();
    REQUIRE(calls.empty());
    REQUIRE((saved == W8SavedLocationState{{"SavedScript", 17}}));
    // The completed master left; the first and the one it armed remain.
    script.runMasters();
    REQUIRE((calls == std::vector<int>{10, 30}));
    return true;
}

static bool groupMemberLookup()
{
    W8MonsterGroup group{};
    REQUIRE((group.MemberIds().empty() && group.MemberLocationId(0) == -1));
    group.monsters = {101, 202};
    REQUIRE(group.MemberIds().size() == 2);
    REQUIRE((group.MemberLocationId(0) == 101 && group.MemberLocationId(1) == 202));
    REQUIRE((group.MemberLocationId(2) == -1 && group.MemberLocationId(-1) == -1));
    group.monsters.erase(group.monsters.begin());
    REQUIRE(group.MemberLocationId(0) == 202);
    return true;
}

TEST_CASE("Resource names match without regard to case", "[collections]")
{
    // The sky asks for "SkyGrad0000.ifl"; the prepared file is skygrad0000.ifl.
    ResourceCache<int> cache;
    const auto gradient = std::make_shared<int>(7);
    cache.insert("skygrad0000.ifl", gradient);
    REQUIRE(cache.find("SkyGrad0000.ifl") == gradient);
    REQUIRE(cache.find("skytop0000.ifl") == nullptr);
}

TEST_CASE("Group activity is derived from its own population without recounts", "[collections]")
{
    W8LevelPopulation population;
    W8MonsterGroup group{};
    auto* first = population.monster_roster.add(std::make_unique<W8MonsterInfo>(), false);
    auto* second = population.monster_roster.add(std::make_unique<W8MonsterInfo>(), true);
    first->location_id = 101;
    second->location_id = 202;
    group.monsters = {101, 202};
    REQUIRE(population.activeMemberCount(group) == 2);
    second->control_state = W8_MONSTER_CONTROL_LURED;
    REQUIRE(population.activeMemberCount(group) == 1);
    first->highest_condition = W8_CONDITION_TURNCOAT;
    REQUIRE(population.activeMemberCount(group) == 0);
    second->control_state = W8_MONSTER_CONTROL_NONE;
    REQUIRE(population.activeMemberCount(group) == 1);
    group.monsters.pop_back();
    REQUIRE(population.activeMemberCount(group) == 0);
}

TEST_CASE("Spell picker metadata follows its draft and caster", "[targeting]")
{
    GameData data;
    data.spells.resize(3);
    auto& spell = data.spells[1];
    spell.power_class = W8_SPELL_POWER_SELECTABLE;
    spell.realm = W8_SPELL_REALM_FIRE;
    spell.spell_point_cost = 5;
    data.spells[2] = spell;
    data.spells[2].power_class = W8_SPELL_POWER_FIXED;
    data.spells[2].spell_point_cost = 10;
    const ScopedGameData installed(std::move(data));
    W8Character caster{};
    W8SpellCastingView view{};
    view.caster = &caster;
    caster.iSPLeft[W8_SPELL_REALM_FIRE] = 20;
    view.draft.detail = 1;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_SELECTABLE);
    REQUIRE(view.AffordablePowerLevels() == 4);
    caster.iSPLeft[W8_SPELL_REALM_FIRE] = 5;
    REQUIRE(view.AffordablePowerLevels() == 1);
    view.draft.detail = 2;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_FIXED);
    REQUIRE(view.AffordablePowerLevels() == 0);
    view.draft.detail = 0;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_CLASS_NONE);
    REQUIRE(view.AffordablePowerLevels() == 0);
}

static bool gameplayCollectionLifetime()
{
    auto group = std::make_unique<W8MonsterGroup>();
    auto* group_ptr = group.get();
    auto& roster = CurrentLevelPopulation().monster_roster;
    REQUIRE((roster.live().empty() && GetMonsterListEntry(0) == nullptr));
    auto* live = roster.add(std::make_unique<W8MonsterInfo>(), false);
    auto* unborn = roster.add(std::make_unique<W8MonsterInfo>(), true);
    CurrentLevelPopulation().encounter_groups.push_back(std::move(group));
    REQUIRE(GetMonsterListEntry(0) == live);
    REQUIRE((GetMonsterListEntry(-1) == nullptr && GetMonsterListEntry(1) == nullptr));
    REQUIRE((GetUnbornMonsterEntry(0) == unborn && GetUnbornMonsterEntry(1) == nullptr));
    REQUIRE(CurrentLevelPopulation().encounter_groups.front().get() == group_ptr);

    // Preparing and abandoning another population cannot publish its identities.
    live->location_id = 7;
    {
        World replacement;
        auto* candidate =
            replacement.population->monster_roster.add(std::make_unique<W8MonsterInfo>(), false);
        candidate->location_id = 7;
        REQUIRE(replacement.population->findMonster(7) == candidate);
        REQUIRE(FindMonster(7, false) == live);
    }
    REQUIRE(FindMonster(7, false) == live);

    // Promotion moves membership only: the same record, now live, and promotion is one-way.
    REQUIRE(roster.promote(unborn));
    REQUIRE(
        (roster.unborn().empty() && roster.live().size() == 2 && roster.live()[1].get() == unborn));
    REQUIRE(!roster.promote(unborn));
    // Removal unindexes from whichever list holds the record, then frees it.
    REQUIRE(RemoveMonster(live, W8MonsterRemoval::Unload));
    REQUIRE((roster.live().size() == 1 && roster.live()[0].get() == unborn));
    REQUIRE(!RemoveMonster(live, W8MonsterRemoval::Unload));

    // Reset destroys the owned records and leaves immediately reusable collections.
    *GetWorld()->population = {};
    REQUIRE((GetMonsterListEntry(0) == nullptr && GetUnbornMonsterEntry(0) == nullptr));
    REQUIRE(CurrentLevelPopulation().encounter_groups.empty());
    return true;
}

static bool npcStockOrdering()
{
    GameData data;
    data.items.resize(2);
    data.items[0].unidentified_name_index = 20;
    data.items[1].unidentified_name_index = 10;
    const ScopedGameData installed(std::move(data));

    W8NpcState npc{};
    npc.items.resize(3);
    npc.items[0].item.iItemNo = 0;
    npc.items[0].available_at = 100;
    npc.items[1].item.iItemNo = 1;
    npc.items[2].item.iItemNo = 0;
    npc.items[2].available_at = 200;
    SortNpcItems(&npc);
    // Item 1 sorts first; the two item-0 entries keep their order.
    REQUIRE(npc.items[0].item.iItemNo == 1);
    REQUIRE((npc.items[1].available_at == 100 && npc.items[2].available_at == 200));
    REQUIRE((GetNpcItemAt(&npc, -1) == nullptr && GetNpcItemAt(&npc, 3) == nullptr));
    REQUIRE(GetNpcItemAt(&npc, 0) == &npc.items[0]);
    return true;
}

static bool keyboardBindingReplacement()
{
    ScopedTemporaryDirectory temporary{"wiz8-keyboard"};
    const auto& root = temporary.path;
    const auto path = root / "bindings.bin";
    // Three retail ten-byte records; the last replaces the first command.
    const std::array<std::uint8_t, 34> records{{
        3, 0, 0, 0,   'A', 0, 0, 0,   1, 0, 0, 0, 0, 0, 'B', 0, 0,
        0, 1, 0, 200, 0,   0, 0, 'C', 0, 0, 0, 1, 0, 0, 0,   0, 0,
    }};
    static_assert(W8_MGS_COMMAND_CANCEL == 0 && W8_MGS_COMMAND_MOVE_FORWARD == 200);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        file->write(records.data(), records.size());
    }
    MGSKeyboard keyboard;
    REQUIRE(keyboard.GetBinding(0) == nullptr);
    {
        auto file = wiz8::open_file(path);
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return keyboard.Load(chunk, true); }));
    }
    // A duplicate command replaces its record and moves to the end of the
    // ordered bindings. The command index must refer to that replacement.
    REQUIRE(keyboard.FindBinding(W8_MGS_COMMAND_MOVE_FORWARD) == 0);
    REQUIRE(keyboard.FindBinding(W8_MGS_COMMAND_CANCEL) == 1);
    REQUIRE((keyboard.GetBinding(0)->key == SDLK_B && keyboard.GetBinding(1)->key == SDLK_C));
    REQUIRE(keyboard.GetBinding(-1) == nullptr);
    REQUIRE(keyboard.GetBinding(2) == keyboard.GetBinding(0));
    GetInputState().keys.insert(SDLK_A);
    REQUIRE(!keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    GetInputState().keys.erase(SDLK_A);
    GetInputState().keys.insert(SDLK_C);
    REQUIRE(keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    GetInputState().keys.erase(SDLK_C);
    keyboard.GetBinding(0)->key = SDLK_KP_ENTER;
    keyboard.GetBinding(0)->modifiers = SHIFT_DOWN | CTRL_DOWN;
    keyboard.GetBinding(1)->key = SDLK_UNKNOWN;
    keyboard.GetBinding(1)->unmapped_retail_key = 0x1234;
    {
        auto file = wiz8::open_file(root / "saved.bin", wiz8::OpenMode::replace);
        REQUIRE(keyboard.Save(*file));
    }
    {
        auto file = wiz8::open_file(root / "saved.bin");
        std::array<std::uint8_t, 24> saved{};
        file->read_exact(saved.data(), saved.size());
        const std::array<std::uint8_t, 24> expected{{
            2, 0, 0, 0, 108, 0, 3, 0, 1, 0, 200, 0, 0, 0, 0x34, 0x12, 0, 0, 1, 0, 0, 0, 0, 0,
        }};
        REQUIRE(saved == expected);
    }
    {
        auto file = wiz8::open_file(root / "saved.bin");
        MGSKeyboard restored;
        REQUIRE(ReadSavePayload(*file, [&](W8Chunk& chunk) { return restored.Load(chunk, true); }));
        const InputEvent press{.type = InputEventType::KeyDown,
                               .modifiers = SHIFT_DOWN | CTRL_DOWN,
                               .key = SDLK_KP_ENTER};
        REQUIRE(restored.FindCommandForEvent(&press) == W8_MGS_COMMAND_MOVE_FORWARD);
        REQUIRE(restored.GetBinding(1)->key == SDLK_UNKNOWN);
        REQUIRE(restored.GetBinding(1)->unmapped_retail_key == 0x1234);
        const InputEvent no_key{.type = InputEventType::KeyDown};
        REQUIRE(restored.FindCommandForEvent(&no_key) == W8_MGS_COMMAND_NONE);
    }
    keyboard.Clear();
    REQUIRE(keyboard.GetBinding(0) == nullptr);
    REQUIRE(!keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    return true;
}

static bool journalSerialization()
{
    const int format = 1, count = 3;
    // Retail entries: level, fact, a one-byte flag and three uninitialized bytes.
    struct DiskEntry {
        std::int32_t level, fact;
        std::uint8_t flag[4];
    };
    const std::array<DiskEntry, count> disk{{{1, 10, {0x00, 0x60, 0x50, 0x00}},
                                             {5, 20, {0x01, 0x60, 0x50, 0x00}},
                                             {9, 30, {0x01, 0xff, 0xff, 0xff}}}};
    static_assert(sizeof(disk) == count * 12);
    std::vector<std::byte> payload;
    const std::array<std::span<const std::byte>, 3> parts{std::as_bytes(std::span(&format, 1)),
                                                          std::as_bytes(std::span(&count, 1)),
                                                          std::as_bytes(std::span(disk))};
    for (const auto bytes : parts) payload.insert(payload.end(), bytes.begin(), bytes.end());
    const auto journal = wiz8::save::decode_journal(payload);
    REQUIRE(journal.size() == count);
    for (int i = 0; i < count; ++i) {
        // Only the low byte is the flag; the padding must not turn it on.
        REQUIRE((journal[i].level == disk[i].level && journal[i].fact == disk[i].fact &&
                 journal[i].alternate_text == (disk[i].flag[0] != 0)));
    }
    // The uninitialized bytes round-trip unchanged.
    REQUIRE(wiz8::save::encode(journal) == payload);
    payload.resize(8);
    payload[4] = std::byte{4}; // More entries than the payload holds.
    REQUIRE_THROWS(wiz8::save::decode_journal(payload));
    return true;
}

static bool visibilityRecordLifetime()
{
    auto& roster = CurrentLevelPopulation().monster_roster;
    auto& observer = *roster.add(std::make_unique<W8MonsterInfo>(), false);
    auto& target = *roster.add(std::make_unique<W8MonsterInfo>(), false);
    observer.location_id = 100;
    observer.fActive = true;
    target.location_id = 200;
    target.fActive = true;
    REQUIRE(!FindMonToMonVisibility(&observer, &target));
    observer.monster_visibility = std::make_unique<W8MonsterVisibility>();
    auto& records = *observer.monster_visibility;
    auto& row = records[target.location_id];
    row.about_location_id = target.location_id;
    row.sight_state = W8_SIGHT_SEEN;
    auto* original = FindMonToMonVisibility(&observer, &target);
    REQUIRE(original == &row);
    for (int id = 1000; id < 2000; ++id) records[id].about_location_id = id;
    REQUIRE(FindMonToMonVisibility(&observer, &target) == original);
    REQUIRE(original->sight_state == W8_SIGHT_SEEN);
    ReleaseMonToMonVisibilityList(&target);
    REQUIRE(!FindMonToMonVisibility(&observer, &target));
    REQUIRE(records.size() == 1000);
    ReleaseMonToMonVisibilityList(&observer);
    REQUIRE(!observer.monster_visibility);
    ReleaseMonToMonVisibilityList(&observer);
    return true;
}

TEST_CASE_METHOD(LevelWorldFixture, "collections_test")
{
    REQUIRE(gameplayCollectionLifetime());
    REQUIRE(masterFunctionMutation());
    REQUIRE(groupMemberLookup());
    REQUIRE(npcStockOrdering());
    REQUIRE(keyboardBindingReplacement());
    REQUIRE(journalSerialization());
    REQUIRE(visibilityRecordLifetime());
}

TEST_CASE_METHOD(LevelWorldFixture, "Engagement propagates to the original leader's direct allies",
                 "[groups]")
{
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
    struct RestoreGroups {
        std::vector<std::unique_ptr<W8MonsterGroup>> saved =
            std::exchange(CurrentLevelPopulation().monster_groups, {});
        ~RestoreGroups() { CurrentLevelPopulation().monster_groups = std::move(saved); }
    } restore;
    for (int i = 0; i < 4; ++i)
        CurrentLevelPopulation().monster_groups.push_back(std::make_unique<W8MonsterGroup>());
    auto& leader = *CurrentLevelPopulation().monster_groups[0];
    auto& first = *CurrentLevelPopulation().monster_groups[1];
    auto& second = *CurrentLevelPopulation().monster_groups[2];
    auto& unrelated = *CurrentLevelPopulation().monster_groups[3];
    leader.group_id = 1;
    first.group_id = 2;
    second.group_id = 3;
    unrelated.group_id = 4;
    leader.allied_group_ids[0] = first.group_id;
    leader.allied_group_ids[2] = second.group_id;
    first.allied_group_ids[2] = unrelated.group_id;

    SetMonsterGroupEngagementState(leader.group_id, true);
    for (const auto* group : {&leader, &first, &second}) {
        REQUIRE(group->engagement);
        REQUIRE(group->engagement_ticks == 1);
    }
    REQUIRE_FALSE(unrelated.engagement);
    SetMonsterGroupEngagementState(leader.group_id, true);
    REQUIRE(second.engagement_ticks == 2);
    SetMonsterGroupEngagementState(leader.group_id, false);
    for (const auto* group : {&leader, &first, &second}) {
        REQUIRE_FALSE(group->engagement);
        REQUIRE(group->engagement_ticks == 0);
    }
}

TEST_CASE("Proposed weapons use the same hand rules without changing equipment", "[equipment]")
{
    const ScopedGame game;
    GameData data;
    auto& records = data.items;
    records.resize(3);
    records[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    records[0].weapon_skill = W8_SKILL_SWORD;
    records[1].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    records[1].weapon_skill = W8_SKILL_THROWING_SLING;
    records[2].equip_class = W8_ITEM_EQUIP_CLASS_SHIELD;
    records[2].weapon_skill = W8_SKILL_SHIELD;
    auto binding = data;
    binding.items[0].binds_on_equip = 1;
    const ScopedGameData installed(std::move(data));
    W8Character character{};
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo = 2;
    const auto choice = PlanWeaponSetSwap(Data(), character);
    REQUIRE(choice);
    REQUIRE(choice.value().primary);
    REQUIRE(choice.value().secondary);
    const auto proposed =
        EvaluateWeaponHands(Data(), character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON],
                            character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON]);
    REQUIRE(proposed[0].in_play);
    REQUIRE_FALSE(proposed[1].in_play);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 0);
    REQUIRE_FALSE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].bound);
    const ScopedGameData bound(std::move(binding));
    REQUIRE_FALSE(PlanWeaponSetSwap(Data(), character));
    REQUIRE_FALSE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].bound);
}

TEST_CASE("Weapon-set replacement derives both hands from the completed arrangement", "[equipment]")
{
    const ScopedGame game;
    auto& characters = CurrentGame().party.characters;
    GameData data{.items = UnrestrictedItems(3)};
    data.items[0].equip_class = W8_ITEM_EQUIP_CLASS_RANGED_WEAPON;
    data.items[0].unidentified_name_index = 0x83;
    data.items[1].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    data.items[2].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    const ScopedGameData installed(std::move(data));
    CurrentGame().party.members[0].emplace();
    auto& character = characters[0];
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo = 2;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    SetHandType(Data(), &character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    REQUIRE(character.Hand[1].uiHolds == 3);
    CommitWeaponSetSwap(CurrentGame(), 0, {true, false});
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 1);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == 0);
    REQUIRE(character.Hand[0].uiHolds == 1);
    REQUIRE(character.Hand[1].uiHolds == 1);
}

TEST_CASE("An inventory command's notice follows its consequences in the game's events",
          "[equipment][events]")
{
    const ScopedGame scoped;
    auto& game = scoped.game;
    GameData data{.strings = std::vector<std::string>(0x200), .items = UnrestrictedItems(2)};
    data.strings[0x1ec] = "changes weapons.";
    for (auto& record : data.items) record.equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    const ScopedGameData installed(std::move(data));
    game.party.members[0].emplace();
    auto& character = game.party.characters[0];
    character.fInParty = true;
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;

    REQUIRE(SwapWeaponSetSlots(game, 0, true));
    const auto& pending = game.events.pending();
    REQUIRE(pending.size() == 3);
    REQUIRE(std::get<PartySlotChanged>(pending[0]).party_slot == 0);
    REQUIRE(std::holds_alternative<EquipmentSettled>(pending[1]));
    const auto& notice = std::get<CharacterNotice>(pending[2]);
    REQUIRE(notice.party_slot == 0);
    REQUIRE(notice.text == "changes weapons.");

    // An observer receives what was pending, in order, and later events as they happen.
    std::vector<std::size_t> observed;
    game.events.observe([&](const Event& event) { observed.push_back(event.index()); });
    REQUIRE(game.events.pending().empty());
    REQUIRE(observed == std::vector<std::size_t>{Event{PartySlotChanged{}}.index(),
                                                 Event{EquipmentSettled{}}.index(),
                                                 Event{CharacterNotice{}}.index()});
    game.events.push(PartyLoadChanged{});
    REQUIRE(observed.back() == Event{PartyLoadChanged{}}.index());
    REQUIRE(game.events.pending().empty());
}

TEST_CASE_METHOD(LevelWorldFixture,
                 "A failed alternate-weapon search leaves equipment and binding state untouched",
                 "[equipment]")
{
    auto& characters = CurrentGame().party.characters;
    auto& rows = CurrentGame().party.rows;
    GameData data;
    auto& records = data.items;
    records.resize(2);
    records[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    records[0].weapon_skill = W8_SKILL_SWORD;
    records[1].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    records[1].weapon_skill = W8_SKILL_THROWING_SLING;
    records[1].range_category = W8_RANGE_LONG;
    records[1].binds_on_equip = 1;
    const ScopedGameData installed(std::move(data));
    struct RestoreState {
        unsigned char autoswap = g_settings.autoswap_weapons;
        ScopedCombat combat;
        ~RestoreState() { g_settings.autoswap_weapons = autoswap; }
    } restore;
    g_settings.autoswap_weapons = true;
    REQUIRE(CurrentLevelPopulation().monster_roster.live().empty());
    auto& character = characters[0];
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    W8CombatSlot target{};
    const W8Action action{W8_ACTION_ATTACK, -1, target, {}};
    REQUIRE_FALSE(ChooseFallbackMonsterTarget(0, action));
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 0);
    const auto& alternate = character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON];
    REQUIRE(alternate.iItemNo == 1);
    REQUIRE_FALSE(alternate.bound);
    REQUIRE_FALSE(alternate.bind_announced);
    REQUIRE_FALSE(rows[0].weapon_swap_pending);
}

TEST_CASE("Target eligibility uses the supplied action and target without a screen", "[targeting]")
{
    const ScopedGame game;
    auto& rows = CurrentGame().party.rows;
    auto& characters = CurrentGame().party.characters;
    auto& row = rows[0];
    auto& character = characters[0];
    CurrentGame().party.members[0].emplace();
    row.committed.kind = W8_ACTION_NONE;
    character.hp_current = 1;
    character.highest_condition = W8_CONDITION_NONE;
    W8CombatSlot target{};
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = 0;
    W8Action action{W8_ACTION_PROTECT, -1, target, {}};
    REQUIRE(TargetIsInPlay(0, 0, action));
    target.iType = W8_TARGET_KIND_NONE;
    REQUIRE(TargetIsInPlay(0, 0, action)); // The supplied action owns its target.
    action.target.iType = W8_TARGET_KIND_NONE;
    REQUIRE_FALSE(TargetIsInPlay(0, 0, action));
}

TEST_CASE("Spell drafts cancel without changing committed or remembered choices", "[targeting]")
{
    const ScopedGame game;
    auto& rows = CurrentGame().party.rows;
    auto& characters = CurrentGame().party.characters;
    CurrentGame().party.members[0].emplace();
    characters[0].hp_current = 1;
    rows[0].committed.kind = W8_ACTION_CAST_SPELL;
    rows[0].committed.detail = 5;
    rows[0].committed.detail_block.spell.power_level = 2;
    rows[0].committed.target = {W8_TARGET_KIND_MONSTER, -1, 17, -1, {}, {}};
    rows[0].remembered.spell = {W8_ACTION_CAST_SPELL, 7};
    W8Action draft = rows[0].remembered.spell;
    draft.detail = 9;
    draft.detail_block.spell.power_level = 3;
    W8CombatSlot target{W8_TARGET_KIND_CHARACTER, 0, -1, -1, {}, {}};
    AimDraft(0, draft, target);
    const auto assessed = ParticipatingAction(0, draft);
    REQUIRE(assessed.detail == 9);
    REQUIRE(assessed.target == target);
    draft.target.iType = W8_TARGET_KIND_NONE;
    REQUIRE(assessed.target == target);
    REQUIRE(rows[0].committed.detail == 5);
    REQUIRE(rows[0].committed.detail_block.spell.power_level == 2);
    REQUIRE(rows[0].committed.target.iMonsterID == 17);
    REQUIRE(rows[0].remembered.spell.detail == 7);
    REQUIRE(rows[0].remembered.spell.target.iType == W8_TARGET_KIND_NONE);
}

TEST_CASE("Trade confirmation rejects changed inventory and stock sources", "[trade]")
{
    const ScopedGame game;
    auto& characters = CurrentGame().party.characters;
    CurrentGame().party.members[0].emplace();
    auto& item = characters[0].backpack[3];
    item = {.iItemNo = 17, .stack_count = 4};
    const W8TradeSelection selected{W8TradeSource::Backpack, 0, 3, item};
    REQUIRE(ResolveTradeSelection(CurrentGame(), nullptr, selected) == &item);
    item.uses_or_charges = 2;
    REQUIRE_FALSE(ResolveTradeSelection(CurrentGame(), nullptr, selected));
    W8NpcState npc{};
    npc.items = {{0, {.iItemNo = 17, .stack_count = 4}, 1},
                 {0, {.iItemNo = 18, .stack_count = 3}, 1}};
    const W8TradeSelection stock{W8TradeSource::NpcStock, -1, 0, npc.items[0].item, 1};
    REQUIRE(ResolveTradeSelection(CurrentGame(), &npc, stock) == &npc.items[0].item);
    std::swap(npc.items[0], npc.items[1]);
    REQUIRE_FALSE(ResolveTradeSelection(CurrentGame(), &npc, stock));
    REQUIRE(CompleteNpcTrade(CurrentGame(), &npc, 0, stock, 1, W8TradeAction::Exchange).outcome ==
            W8TradeOutcome::InvalidSelection);
    REQUIRE(npc.items[0].item.stack_count == 3);
    // A rejected attempt must not reseed the gameplay RNG or practice pickpocketing.
    CurrentGame().rng.seed(1);
    REQUIRE(CompleteNpcTrade(CurrentGame(), &npc, 0, stock, 1, W8TradeAction::Steal).outcome ==
            W8TradeOutcome::InvalidSelection);
    const W8TradeSelection current{W8TradeSource::NpcStock, -1, 0, npc.items[0].item, 1};
    for (int amount : {0, -1, 256})
        REQUIRE(CompleteNpcTrade(CurrentGame(), &npc, 0, current, amount, W8TradeAction::Steal)
                    .outcome == W8TradeOutcome::InvalidSelection);
    REQUIRE(npc.suspicion == 0);
    REQUIRE(CurrentGame().rng.next() == 41);
    W8NpcDatabaseRecord owner_record{};
    owner_record.character.wanted_item_ids[0] = 18;
    auto owner = std::make_shared<W8NpcState>();
    owner->record = &owner_record;
    CurrentGame().story.npcs.push_back(owner);
    CurrentGame().party.members[0]->npc_index = 0;
    item = selected.expected;
    for (auto action : {W8TradeAction::Exchange, W8TradeAction::Offer})
        REQUIRE(CompleteNpcTrade(CurrentGame(), &npc, 0, selected, 1, action).outcome ==
                W8TradeOutcome::WantedByOwner);
    REQUIRE(item == selected.expected);
}

TEST_CASE("A picker constrains layout without restoring an obsolete preference", "[layout]")
{
    const ScopedGame game;
    struct Restore {
        std::unique_ptr<W8SpellCastingView> view = std::move(gpSCSV);
        W8MainUiMode mode = g_settings.main_ui_mode;
        ~Restore()
        {
            gpSCSV = std::move(view);
            g_settings.main_ui_mode = mode;
        }
    } restore;
    gpSCSV = std::make_unique<W8SpellCastingView>();
    g_settings.main_ui_mode = W8_MAIN_UI_MODE_RADAR;
    REQUIRE(GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_FORMATION);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_RADAR);
    g_settings.main_ui_mode = W8_MAIN_UI_MODE_PORTRAITS;
    gpSCSV.reset();
    REQUIRE(GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS);
}

TEST_CASE("A refused purchase leaves stock, payment and a full destination unchanged", "[trade]")
{
    const ScopedGame game;
    GameData data;
    data.items.resize(2);
    data.items[0].flags = W8_ITEM_FLAG_NO_DISCARD;
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].maximum_quantity = 10;
    data.items[0].initial_quantity = {0, 1, 6};
    data.items[0].value = 10;
    const ScopedGameData installed(std::move(data));
    CurrentGame().party.gold = 100;
    CurrentGame().party.item_count = 500;
    for (auto& item : CurrentGame().party.item_pool) item = {.iItemNo = 1};

    CurrentGame().party.held.item = {.iItemNo = 1};
    W8NpcDatabaseRecord record{};
    record.sell_price_factor = 1.0f;
    W8NpcState npc{};
    npc.record = &record;
    npc.items = {{0, {.iItemNo = 0, .stack_count = 1}, 1}};
    CurrentGame().rng.seed(1);
    REQUIRE(CalculateNpcTradeStackPrice(&npc, 0, W8_TRADE_PRICE_PARTY_BUYS, 1, true) == 10);
    REQUIRE(CurrentGame().rng.next() == 41); // Drawing a price must not roll an item quantity.
    const W8TradeSelection stock{W8TradeSource::NpcStock, -1, 0, npc.items[0].item, 1};
    const auto result = CompleteNpcTrade(CurrentGame(), &npc, 0, stock, 1, W8TradeAction::Exchange);
    REQUIRE(result.outcome == W8TradeOutcome::Refused);
    REQUIRE(CurrentGame().party.gold == 100);
    REQUIRE(CurrentGame().party.item_count == 500);
    REQUIRE(CurrentGame().party.item_pool[0].iItemNo == 1);
    REQUIRE(CurrentGame().party.held.item.iItemNo == 1);
    REQUIRE(npc.items.size() == 1);
    REQUIRE(npc.items[0].quantity == 1);
    CurrentGame().party.item_count = 1;
    CurrentGame().party.item_pool[0] = {.iItemNo = 0, .stack_count = 8, .identified = true};
    npc.items[0].item.stack_count = 2;
    const W8TradeSelection stack{W8TradeSource::NpcStock, -1, 0, npc.items[0].item, 1};
    REQUIRE(CompleteNpcTrade(CurrentGame(), &npc, 0, stack, 2, W8TradeAction::Exchange).outcome ==
            W8TradeOutcome::Complete);
    REQUIRE(CurrentGame().party.gold == 80);
    REQUIRE(CurrentGame().party.item_count == 1);
    REQUIRE(CurrentGame().party.item_pool[0].stack_count == 10);
    REQUIRE(npc.items.empty());
}

TEST_CASE("Encounter retirement spends one budget unit per formation, without registration",
          "[groups]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(0));
    const ScopedCombat combat{true};
    const ScopedGameData installed(EncounterLevelData());
    GetWorld()->encounters.random_budget = 8;
    W8MonsterGroup unowned{};
    unowned.encounter_registered = true;
    REQUIRE_FALSE(DestroyMonsterGroup(&unowned, nullptr));
    REQUIRE(GetWorld()->encounters.random_budget == 8);

    auto& population = CurrentLevelPopulation();
    for (int id : {1, 2}) {
        auto group = std::make_unique<W8MonsterGroup>();
        group->group_id = id;
        group->encounter_registered = true;
        population.monster_groups.push_back(std::move(group));
    }
    auto* leader = population.monster_groups[0].get();
    auto* ally = population.monster_groups[1].get();
    leader->allied_group_ids[0] = ally->group_id;
    ally->leader_group_id = leader->group_id;
    REQUIRE(population.activeEncounterLeaders() == std::vector{leader});
    REQUIRE(FindMonsterGroup(leader->group_id) == leader);
    REQUIRE(FindMonsterGroup(999, false) == nullptr);

    RetireMonsterGroupAndAllies(ally);
    REQUIRE(population.activeEncounterLeaders().empty());
    REQUIRE(GetWorld()->encounters.random_budget == 7);
    RetireMonsterGroupAndAllies(leader);
    REQUIRE(GetWorld()->encounters.random_budget == 7);
    REQUIRE(DestroyMonsterGroup(ally, nullptr));
    REQUIRE(DestroyMonsterGroup(leader, nullptr));
    REQUIRE(GetWorld()->encounters.random_budget == 7);
}

TEST_CASE("Encounter state prepares independently and retains the retail save fields",
          "[groups][save]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(0));
    GameData data;
    data.levels.resize(2);
    data.levels[1].minimum_encounter_budget = 2;
    data.levels[1].maximum_encounter_budget = 9;
    data.levels[1].encounter_culling_seconds = 123;
    const ScopedGameData installed(std::move(data));
    auto& live = GetWorld()->encounters;
    live.random_budget = 6;
    live.saved_budget = 77;
    live.interval_min = 31;
    live.default_interval = 17;
    live.interval_max = 44;
    live.save_flag = 1;
    World replacement(1);
    replacement.encounters.random_budget = 100;
    StartLevelEncounters(replacement);
    REQUIRE(replacement.encounters.random_budget == 9);
    REQUIRE(replacement.encounters.culling_seconds == 123);
    REQUIRE(live.random_budget == 6);
    REQUIRE(live.culling_seconds == 180);

    const ScopedTemporaryDirectory directory("encounter-state");
    W8Chunk chunk;
    REQUIRE(chunk.OpenWrite(directory.path / "state.riff"));
    chunk.WriteChunk(W8ChunkId("ENCT"), [&] { SaveEncounterState(chunk.WriteStream()); });
    chunk.Close();
    REQUIRE(chunk.OpenRead(directory.path / "state.riff"));
    chunk.ForEachChunk([&](unsigned) {
        const auto loaded = ReadEncounterState(chunk);
        REQUIRE(loaded.random_budget == 6);
        REQUIRE(loaded.saved_budget == 77);
        REQUIRE(loaded.culling_seconds == 180);
        REQUIRE(loaded.interval_min == 31);
        REQUIRE(loaded.default_interval == 17);
        REQUIRE(loaded.interval_max == 44);
        REQUIRE(loaded.save_flag == 1);
        REQUIRE(chunk.RemainingBytes() == 0);
    });
}

TEST_CASE_METHOD(LevelWorldFixture, "Full formations reject a link before detaching its source",
                 "[groups]")
{
    W8MonsterGroup destination{}, source{};
    destination.group_id = 1;
    std::ranges::fill(destination.allied_group_ids, 99);
    source.group_id = 2;
    source.leader_group_id = 17;
    REQUIRE_FALSE(LinkMonsterGroupToLeader(&destination, &source));
    REQUIRE(source.leader_group_id == 17);
    REQUIRE(std::ranges::all_of(destination.allied_group_ids, [](int id) { return id == 99; }));
}

TEST_CASE("Allied leader election preserves one encounter with zero leadership ranks", "[groups]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(0));
    const ScopedCombat combat{true};
    const ScopedGameData installed(EncounterLevelData());
    GetWorld()->encounters.random_budget = 8;
    auto& population = CurrentLevelPopulation();
    std::array<std::unique_ptr<Monster>, 3> representations;
    for (int index = 0; index < 3; ++index) {
        auto group = std::make_unique<W8MonsterGroup>();
        group->group_id = index + 1;
        group->leader_location_id = index + 1;
        group->encounter_registered = true;
        group->monsters = {index + 1};
        population.monster_groups.push_back(std::move(group));
        auto record = std::make_unique<W8MonsterInfo>();
        record->location_id = index + 1;
        auto& monster = representations[index];
        monster = std::make_unique<Monster>(GetWorld());
        monster->location_id = index + 1;
        monster->nav.movement.leadership_rank = 0;
        record->entity.attach(*monster);
        population.monster_roster.add(std::move(record), false);
    }
    auto* old = population.monster_groups[0].get();
    auto* first = population.monster_groups[1].get();
    auto* second = population.monster_groups[2].get();
    old->allied_group_ids[0] = 2;
    old->allied_group_ids[1] = 3;
    first->leader_group_id = second->leader_group_id = 1;
    old->monsters.clear(); // Final member removal owns group retirement.
    REQUIRE(DestroyMonsterGroup(old, population.findMonster(1)));
    REQUIRE(population.activeEncounterLeaders() == std::vector{first});
    REQUIRE(GetWorld()->encounters.random_budget == 7);
    REQUIRE(first->leader_group_id == 0);
    REQUIRE(first->allied_group_ids[0] == 3);
    REQUIRE(second->leader_group_id == 2);
    REQUIRE(representations[2]->nav.linked_navigator == &(representations[1].get())->nav);
}

TEST_CASE_METHOD(LevelWorldFixture,
                 "A trigger wakes a placed group whose member is unloaded or gone",
                 "[groups][triggers]")
{
    auto& population = CurrentLevelPopulation();
    auto group = std::make_unique<W8MonsterGroup>();
    group->monster_id = 7;
    group->monsters = {3};
    auto* placed = group.get();
    population.monster_groups.push_back(std::move(group));
    auto record = std::make_unique<W8MonsterInfo>();
    record->location_id = 3; // Not loaded yet, so it has no representation.
    population.monster_roster.add(std::move(record), false);

    WakeMonsterGroup(7);
    REQUIRE(placed->members_active);

    placed->members_active = false;
    placed->monsters.clear(); // Its member has left.
    WakeMonsterGroup(7);
    REQUIRE_FALSE(placed->members_active);
}

TEST_CASE_METHOD(LevelWorldFixture,
                 "Combat list rows cannot select a group outside the displayed list",
                 "[groups][targeting]")
{
    auto& groups = CurrentLevelPopulation().monster_groups;
    auto group = std::make_unique<W8MonsterGroup>();
    group->members_active = true;
    group->fInCombat = true;
    group->ubDisposition = W8_DISPOSITION_HOSTILE;
    group->monsters = {1};
    auto* displayed = group.get();
    groups.push_back(std::move(group));
    groups.push_back(std::make_unique<W8MonsterGroup>()); // Not displayed.
    REQUIRE(GetLiveMonsterGroupAtIndex(0) == displayed);
    REQUIRE(GetLiveMonsterGroupAtIndex(1) == nullptr);
    REQUIRE(GetLiveMonsterGroupAtIndex(-1) == nullptr);
}

TEST_CASE("Threat queries follow roster membership and owned combat state", "[collections]")
{
    W8LevelPopulation population;
    auto& roster = population.monster_roster;
    auto* live = roster.add(std::make_unique<W8MonsterInfo>(), false);
    auto* unborn = roster.add(std::make_unique<W8MonsterInfo>(), true);
    REQUIRE_FALSE(roster.hasActiveMonsters());
    unborn->fActive = true;
    unborn->ubDisposition = W8_DISPOSITION_HOSTILE;
    unborn->pCombat = std::make_unique<W8MonsterCombatState>();
    REQUIRE(roster.hasActiveMonsters());
    REQUIRE(roster.hostileCombatCount() == 0);
    REQUIRE(roster.promote(unborn));
    REQUIRE(roster.hostileCombatCount() == 1);
    live->fActive = true;
    live->ubDisposition = W8_DISPOSITION_HOSTILE;
    live->pCombat = std::make_unique<W8MonsterCombatState>();
    REQUIRE(roster.hostileCombatCount() == 2);
    live->ubDisposition = W8_DISPOSITION_FRIENDLY;
    REQUIRE(roster.hostileCombatCount() == 1);
    unborn->fActive = false;
    REQUIRE(roster.hostileCombatCount() == 0);
    live->ubDisposition = W8_DISPOSITION_HOSTILE;
    live->pCombat.reset();
    REQUIRE(roster.hostileCombatCount() == 0);
    live->fActive = false;
    REQUIRE_FALSE(roster.hasActiveMonsters());
}

TEST_CASE("Retail monster combat markers describe ownership without restoring a fight", "[save]")
{
    W8MonsterInfo monster{};
    monster.location_id = 42;
    monster.pCombat = std::make_unique<W8MonsterCombatState>();
    auto image = wiz8::retail::encode_image(monster);
    REQUIRE(image.fInCombat);
    W8MonsterInfo restored{};
    wiz8::retail::decode_image(image, restored);
    REQUIRE(restored.location_id == 42);
    REQUIRE_FALSE(restored.pCombat);
    REQUIRE(monster.pCombat);
    monster.pCombat.reset();
    REQUIRE_FALSE(wiz8::retail::encode_image(monster).fInCombat);
}

namespace {
// A defined fighter with no equipment and every base attribute at fifty.
W8Character MakeFighter()
{
    W8Character character{};
    character.iProfession = W8_PROFESSION_FIGHTER;
    character.original_profession = W8_PROFESSION_FIGHTER;
    character.iRace = W8_RACE_HUMAN;
    character.gender = W8_GENDER_MALE;
    character.uiExpLevel = 1;
    for (auto& attribute : character.attributes) attribute.base = 50;
    return character;
}
}

TEST_CASE("Attribute-cap notices follow the party's activity without a screen", "[party][events]")
{
    struct Scenario {
        bool started;
        Activity::Place place;
        bool announces;
    };
    const Scenario scenarios[] = {{true, Activity::Place::World, true},
                                  {true, Activity::Place::Camp, false},
                                  {false, Activity::Place::World, false}};
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.started, scenario.place);
        const ScopedGame scoped;
        auto& game = scoped.game;
        game.status.game_started = scenario.started;
        game.activity.place = scenario.place;
        GameData data{.strings = std::vector<std::string>(0x1000)};
        data.strings[0x1d6] = "%s %s %s";
        const ScopedGameData installed(std::move(data));
        auto character = MakeFighter();
        character.attributes[W8_ATTRIBUTE_STRENGTH].base = 100;

        ApplyAttributeChange(game, &character, W8_ATTRIBUTE_STRENGTH);

        const auto& pending = game.events.pending();
        REQUIRE(pending.size() == (scenario.announces ? 2u : 1u));
        REQUIRE(std::get<SkillAvailabilityChanged>(pending[0]).available);
        if (scenario.announces) REQUIRE(std::holds_alternative<NoticeDialog>(pending[1]));
    }
}

TEST_CASE("Shared party weight uses stable slot ties and drops the odd half-unit",
          "[party][encumbrance]")
{
    GameData data;
    data.items.resize(1);
    data.items[0].weight = 7;
    const ScopedGameData installed(std::move(data));
    W8Party party;
    for (int slot : {0, 1}) {
        party.members[slot].emplace();
        party.characters[slot] = MakeFighter();
    }
    party.item_pool[0] = {.iItemNo = 0, .stack_count = 1};
    party.item_count = 1;
    REQUIRE(UpdatePartyStats(Data(), party, {}).load_redistributed);
    REQUIRE(party.characters[0].party_weight_share == 2);
    REQUIRE(party.characters[1].party_weight_share == 1);
    REQUIRE(party.characters[0].total_carried_weight == 2);
    REQUIRE(party.characters[1].total_carried_weight == 1);
    party.members[0].reset();
    REQUIRE(UpdatePartyStats(Data(), party, {}).load_redistributed);
    REQUIRE(party.characters[0].party_weight_share == 0);
    REQUIRE(party.characters[1].party_weight_share == 3);
}

TEST_CASE("A recalculation reports crossing the carrying capacity once", "[party][encumbrance]")
{
    GameData data;
    data.items.resize(1);
    data.items[0].weight = 10;
    const ScopedGameData installed(std::move(data));
    auto character = MakeFighter();
    character.backpack[0] = {.iItemNo = 0, .stack_count = 200};
    // Strength and vitality of fifty carry (50 + 2 * 50) * 12 = 1800 against 2000.
    REQUIRE(UpdateStandaloneCharacterStats(Data(), character, {}).became_overloaded);
    REQUIRE(character.carrying_capacity == 1800);
    REQUIRE(character.inventory_weight == 2000);
    REQUIRE(character.load_category == W8_LOAD_EXTREME);
    REQUIRE_FALSE(UpdateStandaloneCharacterStats(Data(), character, {}).became_overloaded);
}

TEST_CASE("Party load follows supplied rules independently of the live combat and party",
          "[party][encumbrance][combat]")
{
    const ScopedGame game;
    const ScopedCombat combat{true};
    GameData data;
    data.items.resize(1);
    data.items[0].weight = 18;
    const ScopedGameData installed(std::move(data));
    CurrentGame().party.characters[0].party_weight_share = 123;
    W8Party party;
    party.members[0].emplace();
    auto& character = party.characters[0];
    character = MakeFighter();
    character.fInParty = true;
    character.iRace = W8_RACE_ANDROID;
    character.party_weight_share = 4;
    party.item_pool[0] = {.iItemNo = 0, .stack_count = 1};
    party.item_count = 1;
    PartyStatRules rules;
    rules.load_timing = PartyLoadTiming::AfterCombat;
    REQUIRE_FALSE(UpdatePartyStats(Data(), party, rules).load_redistributed);
    REQUIRE(party.redistribution_pending);
    REQUIRE(character.party_weight_share == 4);
    REQUIRE(character.total_carried_weight == 4);
    rules.load_timing = PartyLoadTiming::Immediate;
    rules.characters[0].defensive_action = true;
    REQUIRE(UpdatePartyStats(Data(), party, rules).load_redistributed);
    REQUIRE_FALSE(party.redistribution_pending);
    REQUIRE(character.party_weight_share == 9);
    REQUIRE(character.armor_class_components[W8_AC_COMPONENT_DEFENSIVE_ACTION] == 2);
    REQUIRE(CurrentGame().combat.has_value());
    REQUIRE(CurrentGame().party.characters[0].party_weight_share == 123);
}

TEST_CASE("Learned keywords go straight into the story's notebook", "[npc][dialogue]")
{
    const ScopedGame game;
    auto& notebook = CurrentGame().story.keywords;
    // Older saves stored the empty notebook's placeholder line as a keyword.
    notebook.push_back({std::string(W8_NO_KEYWORDS_PLACEHOLDER), W8_DIALOGUE_CATEGORY_ALL});
    LearnDialogueKeyword("Arnika", W8_DIALOGUE_CATEGORY_PLACES, false);
    LearnDialogueKeyword("ARNIKA", W8_DIALOGUE_CATEGORY_MISC, false);
    REQUIRE(notebook.size() == 1);
    REQUIRE(notebook[0].text == "Arnika");
    REQUIRE(notebook[0].category == W8_DIALOGUE_CATEGORY_PLACES);
}

TEST_CASE("Using up an offered item drains the offer and leaves the hand alone", "[npc][items]")
{
    const ScopedGame game;
    auto& offer = CurrentGame().npc_interaction.offered_item;
    offer = W8ItemInstance{.iItemNo = 7, .stack_count = 2};
    CurrentGame().party.held.item = W8ItemInstance{.iItemNo = 9, .stack_count = 1};
    RemoveNpcScriptItem(MakeScriptContext(CurrentGame()), &*offer, 0, -1);
    REQUIRE(offer->stack_count == 1);
    RemoveNpcScriptItem(MakeScriptContext(CurrentGame()), &*offer, 0, -1);
    REQUIRE_FALSE(offer);
    REQUIRE(CurrentGame().party.held.item.iItemNo == 9);
}

TEST_CASE("Rebinding an NPC to a new level keeps its state", "[npc]")
{
    const ScopedGame game;
    auto& story = CurrentGame().story;
    story.npcs.push_back(std::make_unique<W8NpcState>());
    W8NpcState* npc = story.npcs[0].get();
    npc->gold = 120;
    npc->binding_unavailable = 1;
    W8PreparedNpcBindings pending;
    pending.records.push_back({.slot = 0, .location_id = 42, .level_band = 3, .bound_level = 7});
    InstallNpcBindings(std::move(pending));
    REQUIRE(story.npcs[0].get() == npc);
    REQUIRE(npc->gold == 120);
    REQUIRE(npc->location_id == 42);
    REQUIRE(npc->bound_level == 7);
    REQUIRE(npc->is_present);
    REQUIRE(npc->binding_unavailable == 0);
}

TEST_CASE("Formation reconciliation repairs only its owning party", "[party][formation]")
{
    const ScopedGame game;
    W8Party party;
    party.formation.setPosition(0, W8FormationCell{0, 0}, false, true);
    party.formation.setPosition(1, W8FormationCell{0, 1}, false, true);
    party.members[1].emplace();
    const auto previous = party;
    party.formation.reconcile(party.members);
    REQUIRE_FALSE(party.formation.cellOf(0));
    REQUIRE(party.formation.cellOf(1) == W8FormationCell{0, 0});
    REQUIRE(party.formation.rowOccupants(0) == 1);
    REQUIRE(previous.formation.rowOccupants(0) == 2);
    REQUIRE(previous.formation.cellOf(1) == W8FormationCell{0, 1});
}

TEST_CASE("Formation swaps exchange cells and save the derived placement", "[party][formation]")
{
    W8PartyFormationState formation;
    formation.setPosition(0, W8FormationCell{0, 0}, false, true);
    formation.setPosition(1, W8FormationCell{2, 1}, false, true);
    formation.positions[1].revival_row = 3;
    formation.swap(0, 1);
    REQUIRE(formation.cellOf(0) == W8FormationCell{2, 1});
    REQUIRE(formation.cellOf(1) == W8FormationCell{0, 0});
    REQUIRE(formation.positions[0].facing == 2); // Each faces the way its new row does.
    REQUIRE(formation.positions[1].facing == 0);

    const auto disk = wiz8::retail::encode_image(formation);
    REQUIRE(disk.positions[0].row == 2);
    REQUIRE(disk.positions[0].column == 1);
    REQUIRE(disk.positions[1].old_row == 3);
    REQUIRE(disk.positions[2].row == -1);
    REQUIRE(disk.row_occupants[0] == 1);
    REQUIRE(disk.row_occupants[2] == 1);
    W8PartyFormationState loaded;
    wiz8::retail::decode_image(disk, loaded);
    REQUIRE(loaded == formation);

    auto corrupt = disk;
    corrupt.occupants[4][2] = 1; // Slot 1 in two cells.
    REQUIRE_THROWS(wiz8::retail::decode_image(corrupt, loaded));
}

TEST_CASE("Pools follow a changed ceiling and keep their level otherwise", "[party][stats]")
{
    const ScopedGame active;
    auto& character = CurrentGame().party.characters[0];
    character = MakeFighter();
    REQUIRE(UpdateStandaloneCharacterStats(Data(), character, {}).valid);
    // A new character's pools fill as their ceilings rise from zero.
    REQUIRE(character.uiHPMax > 0);
    REQUIRE(character.hp_current == static_cast<unsigned int>(character.uiHPMax));
    REQUIRE(character.uiStaminaMax > 0);
    REQUIRE(character.stamina == character.uiStaminaMax);
    REQUIRE(character.fatigue_band == 0);

    // Recalculating with unchanged inputs leaves spent pools alone.
    character.hp_current = 1;
    character.stamina = 0;
    REQUIRE_FALSE(UpdateStandaloneCharacterStats(Data(), character, {}).hp_exhausted);
    REQUIRE(character.hp_current == 1);
    REQUIRE(character.stamina == 0);
    REQUIRE(character.fatigue_band == 4);

    // A raised ceiling carries its gain into the pool.
    const int ceiling = character.uiHPMax;
    character.hp_adjustment = 5;
    UpdateStandaloneCharacterStats(Data(), character, {});
    REQUIRE(character.uiHPMax == ceiling + 5);
    REQUIRE(character.hp_current == 6);

    // A penalty can consume the whole stamina ceiling while the member is tired.
    // Restoring that pool must keep full fatigue instead of dividing by zero.
    const int stamina_ceiling = character.uiStaminaMax;
    character.stamina = 1;
    character.fatigue_penalty = stamina_ceiling;
    UpdateStandaloneCharacterStats(Data(), character, {});
    REQUIRE(character.uiStaminaMax == 0);
    REQUIRE(character.fatigue_band == 4);
    RestoreCharacterStamina(0, stamina_ceiling - 1, false);
    REQUIRE(character.stamina == 0);
    REQUIRE(character.fatigue_band == 4);
}

TEST_CASE("A detached hit-point calculation reports death without applying live conditions",
          "[party][stats][ownership]")
{
    const ScopedGame game;
    auto character = MakeFighter();
    character.uiHPMax = 1000;
    character.hp_current = 1;
    const auto live_hp = CurrentGame().party.characters[0].hp_current;
    const auto live_condition = CurrentGame().party.characters[0].highest_condition;
    REQUIRE(UpdateStandaloneCharacterStats(Data(), character, {}).hp_exhausted);
    REQUIRE(character.hp_current == 0);
    REQUIRE(CurrentGame().party.characters[0].hp_current == live_hp);
    REQUIRE(CurrentGame().party.characters[0].highest_condition == live_condition);
    REQUIRE(character.highest_condition == W8_CONDITION_NONE);
    REQUIRE_FALSE(UpdateStandaloneCharacterStats(Data(), character, {}).hp_exhausted);
}

TEST_CASE("Standalone character updates ignore saved membership flags", "[party][stats][ownership]")
{
    const ScopedGame game;
    CurrentGame().party.characters[0].party_weight_share = 99;
    W8Character foreign{};
    foreign.iProfession = W8_PROFESSION_FIGHTER;
    foreign.iRace = W8_RACE_HUMAN;
    foreign.gender = W8_GENDER_MALE;
    foreign.fInParty = true; // A saved flag does not establish ownership.
    for (auto& attribute : foreign.attributes) attribute.effective = 50;
    for (auto& item : foreign.EquippedItem) item.iItemNo = -1;
    for (auto& item : foreign.backpack) item.iItemNo = -1;
    foreign.party_weight_share = 12;
    CurrentGame().party.modifiers.damage_bonus = 41;
    foreign.condition_modifiers.damage_bonus = 2;
    const auto result = UpdateStandaloneCharacterStats(Data(), foreign, {});
    REQUIRE(result.valid);
    REQUIRE(foreign.bonus.damage_bonus == 2);
    REQUIRE(foreign.party_weight_share == 0);
    REQUIRE(CurrentGame().party.characters[0].party_weight_share == 99);
    // Character creation uses a party storage slot before that row is occupied.
    CurrentGame().party.characters[0] = foreign;
    auto& draft = CurrentGame().party.characters[0];
    draft.carrying_capacity = 0;
    draft.party_weight_share = 99;
    RecalculateCharacterDerivedStats(CurrentGame(), draft);
    REQUIRE(draft.carrying_capacity > 0);
    REQUIRE(draft.party_weight_share == 0);
    REQUIRE(draft.bonus.damage_bonus == 2);
}

TEST_CASE("Weapon swings read the damage bonus the operation started from", "[party][stats]")
{
    GameData data;
    data.items.resize(1);
    data.items[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    data.items[0].weapon_skill = W8_SKILL_SWORD;
    const ScopedGameData installed(std::move(data));
    W8Character character{};
    character.iProfession = W8_PROFESSION_FIGHTER;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.attributes[W8_ATTRIBUTE_SPEED].effective = 100;
    character.attributes[W8_ATTRIBUTE_STRENGTH].effective = 50;
    character.skills[W8_SKILL_SWORD].level = 90;
    character.skills[W8_SKILL_CLOSE_COMBAT].level = 90;
    character.Hand[0].damage_bonus = 3;
    const auto baseline = CaptureCharacterBaseline(character);

    // Retail scores swings before resetting the bonus: (100 + 30 + 90) / 3 = 73.
    DeriveCharacterStats(Data(), character, {}, baseline);
    REQUIRE(character.Hand[0].damage_bonus == 0);
    REQUIRE(character.Hand[0].swings == 2);
    // Another pass of the same operation reads the same baseline.
    DeriveCharacterStats(Data(), character, {}, baseline);
    REQUIRE(character.Hand[0].swings == 2);
    // The next operation starts from this arrangement's bonus: (100 + 0 + 90) / 3 = 63.
    DeriveCharacterStats(Data(), character, {}, CaptureCharacterBaseline(character));
    REQUIRE(character.Hand[0].swings == 1);
}

namespace {
// The active party as an inventory command sees it outside a running game.
struct ScopedActiveParty {
    ScopedGame game;
};

W8Character& PrepareMember(unsigned int slot)
{
    CurrentGame().party.members[slot].emplace();
    auto& character = CurrentGame().party.characters[slot];
    character.iProfession = W8_PROFESSION_FIGHTER;
    character.original_profession = W8_PROFESSION_FIGHTER;
    character.iRace = W8_RACE_HUMAN;
    character.gender = W8_GENDER_MALE;
    character.uiExpLevel = 1;
    character.fInParty = true;
    for (auto& attribute : character.attributes) attribute.base = 50;
    return character;
}
}

TEST_CASE("A split stack travels through the hand and merges back", "[party][inventory]")
{
    const ScopedActiveParty active;
    GameData data;
    auto& record = data.items.emplace_back();
    record.quantity_kind = W8_ITEM_QUANTITY_STACK;
    record.maximum_quantity = 100;
    record.weight = 10;
    const ScopedGameData installed(std::move(data));
    auto& actor = PrepareMember(0);
    auto& party = CurrentGame().party;
    actor.backpack[0] = W8ItemInstance{.iItemNo = 0, .stack_count = 10};

    REQUIRE(SplitItemStack(CurrentGame(), {W8ItemStorage::Backpack, 0, 0}, 3, 0));
    REQUIRE(actor.backpack[0].stack_count == 7);
    REQUIRE(party.held.item.stack_count == 3);
    REQUIRE(party.held.origin == W8ItemLocation{W8ItemStorage::Backpack, 0, 0});
    REQUIRE(actor.inventory_weight == 70);
    REQUIRE(SplitItemStack(CurrentGame(), {W8ItemStorage::Backpack, 0, 0}, 1, 0));
    REQUIRE(party.held.item.stack_count == 4);
    REQUIRE(actor.inventory_weight == 60);
    const auto result =
        MergeInventoryStacks(CurrentGame(), {W8ItemStorage::Backpack, 0, 0}, {W8ItemStorage::Hand});
    REQUIRE(result.complete);
    REQUIRE(party.held.item.iItemNo == -1);
    REQUIRE(party.held.origin == W8ItemLocation{});
    REQUIRE(actor.backpack[0].stack_count == 10);
    REQUIRE(actor.inventory_weight == 100);
}

TEST_CASE("A held stack refuses an unstorable split without losing or duplicating items",
          "[party][inventory]")
{
    const ScopedActiveParty active;
    GameData data;
    data.items.resize(1);
    data.items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    data.items[0].maximum_quantity = 100;
    const ScopedGameData installed(std::move(data));
    auto& party = CurrentGame().party;
    party.held = {W8ItemInstance{.iItemNo = 0, .stack_count = 10}, {}, 0};
    for (auto& item : party.characters[0].backpack)
        item = W8ItemInstance{.iItemNo = 0, .stack_count = 100};
    for (auto& item : party.item_pool) item = W8ItemInstance{.iItemNo = 0, .stack_count = 100};
    party.item_count = 500;
    REQUIRE_FALSE(SplitItemStack(CurrentGame(), {W8ItemStorage::Hand}, 4, 0));
    REQUIRE(party.held.item.stack_count == 10);
    REQUIRE(party.item_count == 500);
    REQUIRE(party.characters[0].backpack[0].stack_count == 100);
    REQUIRE_FALSE(SplitItemStack(CurrentGame(), {W8ItemStorage::Hand}, 11, 0));
    REQUIRE_FALSE(SplitItemStack(CurrentGame(), {W8ItemStorage::Hand}, 0, 0));
}

TEST_CASE("Unusable equipment comes off in slot order and never stays equipped",
          "[party][inventory][stats]")
{
    const ScopedActiveParty active;
    GameData data{.items = UnrestrictedItems(3)};
    // The first item supplies strength to the second, but itself requires speed.
    data.items[0].modifier_0b3_index = W8_ATTRIBUTE_STRENGTH;
    data.items[0].modifier_0b3_value = 20;
    data.items[0].attribute_requirements[0] = {W8_ATTRIBUTE_SPEED, 60};
    data.items[1].attribute_requirements[0] = {W8_ATTRIBUTE_STRENGTH, 60};
    auto undiscardable = data;
    undiscardable.items[1].flags |= W8_ITEM_FLAG_NO_DISCARD;
    const ScopedGameData installed(std::move(data));
    auto& character = PrepareMember(0);
    character.EquippedItem[W8_EQUIP_SLOT_HEAD].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo = 1;

    SettleActiveParty(CurrentGame());
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_HEAD].iItemNo == -1);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == -1);
    REQUIRE(character.backpack[0].iItemNo == 0);
    REQUIRE(character.backpack[1].iItemNo == 1);
    REQUIRE(character.attributes[W8_ATTRIBUTE_STRENGTH].effective == 50);

    // With no storage left, an item that may not be discarded falls into the hand.
    auto& party = CurrentGame().party;
    const ScopedGameData no_discard(std::move(undiscardable));
    for (auto& item : character.backpack) item = W8ItemInstance{.iItemNo = 2};
    for (auto& item : party.item_pool) item = W8ItemInstance{.iItemNo = 2};
    party.item_count = 500;
    character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo = 1;
    SettleActiveParty(CurrentGame());
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == -1);
    REQUIRE(party.held.item.iItemNo == 1);
}

TEST_CASE("Unusable equipment that nothing can take stays equipped until there is room",
          "[party][inventory]")
{
    const ScopedActiveParty active;
    GameData data{.items = UnrestrictedItems(3)};
    data.items[0].attribute_requirements[0] = {W8_ATTRIBUTE_STRENGTH, 60};
    const ScopedGameData installed(std::move(data));
    auto& character = PrepareMember(0);
    auto& party = CurrentGame().party;
    for (auto& item : character.backpack) item = W8ItemInstance{.iItemNo = 1};
    for (auto& item : party.item_pool) item = W8ItemInstance{.iItemNo = 1};
    party.item_count = 500;
    party.held.item = W8ItemInstance{.iItemNo = 2};
    character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo = 0;

    // No level is loaded, so there is nowhere to drop it either.
    SettleActiveParty(CurrentGame());
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == 0);
    REQUIRE(party.held.item.iItemNo == 2);
    REQUIRE(party.item_count == 500);

    character.backpack[7] = {};
    SettleActiveParty(CurrentGame());
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == -1);
    REQUIRE(character.backpack[7].iItemNo == 0);
}

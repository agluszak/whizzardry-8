#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator> // IWYU pragma: keep - public header for std::istreambuf_iterator.
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <catch2/catch_test_macros.hpp>

#include "fact_state.h"
#include "layouts/npc_state.h"
#include "mgs_commands.h"
#include "mutable_game_data.h"
#include "save_payload.h"
#include "scoped_asset_roots.h"
#include "scoped_combat.h"
#include "scoped_game.h"
#include "scoped_records.h"
#include "sight_state.h"
#include "temporary_directory.h"
#include "wiz8/character_skills.h"
#include "wiz8/chunk.h"
#include "wiz8/cursor.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/monster.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_generators.h"
#include "wiz8/monster_roster.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_trade_price.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/load_category.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

struct LevelWorldFixture {
    std::unique_ptr<World> previous = ReplaceWorld(std::make_unique<World>());
    ~LevelWorldFixture() { ReplaceWorld(std::move(previous)); }
};

static std::vector<int> masterCalls;

static bool laterMaster(int command)
{
    masterCalls.push_back(30 + command);
    return false;
}

static bool firstMaster(int command)
{
    masterCalls.push_back(10 + command);
    if (command == 0) {
        g_master_functions.push_back({laterMaster});
    }
    return false;
}

static bool removingMaster(int command)
{
    masterCalls.push_back(20 + command);
    // A nested script's result cannot overwrite this callback's completion.
    laterMaster(1);
    return true;
}

static bool masterFunctionMutation()
{
    g_master_functions = {{firstMaster}, {removingMaster}};
    RunMasterFunctions();
    REQUIRE((masterCalls == std::vector<int>{10, 20, 31}));
    REQUIRE((g_master_functions == std::vector<W8LevelMaster>{{firstMaster}, {laterMaster}}));
    masterCalls.clear();
    g_master_functions[0].capture = [](W8SavedLocationState& state) {
        state.emplace_back("SavedScript", 17);
    };
    const auto saved = CaptureMasterFunctionState();
    REQUIRE(masterCalls.empty());
    REQUIRE((saved == W8SavedLocationState{{"SavedScript", 17}}));
    g_master_functions.clear();
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
    const ScopedRecords spells(MutableData().spells, 3);
    W8Character caster{};
    W8SpellCastingView view{};
    view.caster = &caster;
    auto& spell = MutableData().spells[1];
    spell.power_class = W8_SPELL_POWER_SELECTABLE;
    spell.realm = W8_SPELL_REALM_FIRE;
    spell.spell_point_cost = 5;
    caster.iSPLeft[spell.realm] = 20;
    view.draft.detail = 1;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_SELECTABLE);
    REQUIRE(view.AffordablePowerLevels() == 4);
    caster.iSPLeft[spell.realm] = 5;
    REQUIRE(view.AffordablePowerLevels() == 1);
    MutableData().spells[2] = spell;
    MutableData().spells[2].power_class = W8_SPELL_POWER_FIXED;
    MutableData().spells[2].spell_point_cost = 10;
    view.draft.detail = 2;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_FIXED);
    REQUIRE(view.AffordablePowerLevels() == 0);
    view.draft.detail = 0;
    REQUIRE(view.PowerClass() == W8_SPELL_POWER_CLASS_NONE);
    REQUIRE(view.AffordablePowerLevels() == 0);
}

static bool gameplayCollectionLifetime()
{
    const ScopedGame game;
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
    gXStatus = {};
    *GetWorld()->population = {};
    REQUIRE((GetMonsterListEntry(0) == nullptr && GetUnbornMonsterEntry(0) == nullptr));
    REQUIRE(CurrentLevelPopulation().encounter_groups.empty());
    return true;
}

static bool npcStockOrdering()
{
    const ScopedRecords items(MutableData().items, 2);
    auto& records = MutableData().items;
    records[0].unidentified_name_index = 20;
    records[1].unidentified_name_index = 10;

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
    ScopedTemporaryDirectory temporary{"wiz8-collections"};
    const auto& root = temporary.path;
    const auto assets = root / "assets", user = root / "user";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(user);
    ScopedAssetRoots roots{{wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user)}};
    const auto input_path = root / "journal.bin", output_path = root / "journal-copy.bin";
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
    {
        auto file = wiz8::open_file(input_path, wiz8::OpenMode::replace);
        file->write(&format, sizeof(format));
        file->write(&count, sizeof(count));
        file->write(disk.data(), sizeof(disk));
    }
    {
        auto file = wiz8::open_file(input_path);
        ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadJournalEntries(chunk); });
    }
    REQUIRE(g_fact_journal_entries.size() == count);
    for (int i = 0; i < count; ++i) {
        const auto& actual = g_fact_journal_entries[i];
        // Only the low byte is the flag; the padding must not turn it on.
        REQUIRE((actual.level == disk[i].level && actual.fact == disk[i].fact &&
                 actual.alternate_text == (disk[i].flag[0] != 0)));
    }
    {
        auto file = wiz8::open_file(output_path, wiz8::OpenMode::replace);
        SaveFactJournal(*file);
    }
    std::ifstream input(input_path, std::ios::binary), output(output_path, std::ios::binary);
    const std::vector<char> original{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>{}};
    const std::vector<char> roundtrip{std::istreambuf_iterator<char>(output),
                                      std::istreambuf_iterator<char>{}};
    REQUIRE((original == roundtrip && original.size() == 8 + 3 * 12));
    // Reusing the journal with an empty chunk must shrink its live extent.
    {
        auto file = wiz8::open_file(input_path, wiz8::OpenMode::replace);
        const int empty = 0;
        file->write(&format, sizeof(format));
        file->write(&empty, sizeof(empty));
    }
    {
        auto file = wiz8::open_file(input_path);
        ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadJournalEntries(chunk); });
    }
    REQUIRE(g_fact_journal_entries.empty());
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
    gXStatus = {};
    return true;
}

TEST_CASE_METHOD(LevelWorldFixture, "collections_test")
{
    const ScopedGame game;
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
    const ScopedRecords items(MutableData().items, 3);
    auto& records = MutableData().items;
    records[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    records[0].weapon_skill = W8_SKILL_SWORD;
    records[1].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    records[1].weapon_skill = W8_SKILL_THROWING_SLING;
    records[2].equip_class = W8_ITEM_EQUIP_CLASS_SHIELD;
    records[2].weapon_skill = W8_SKILL_SHIELD;
    W8Character character{};
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo = 2;
    const auto choice = PlanWeaponSetSwap(character);
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
    records[0].binds_on_equip = 1;
    REQUIRE_FALSE(PlanWeaponSetSwap(character));
    REQUIRE_FALSE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].bound);
}

TEST_CASE("Weapon-set replacement derives both hands from the completed arrangement", "[equipment]")
{
    const ScopedGame game;
    auto& characters = CurrentGame().party.characters;
    const ScopedRecords items(MutableData().items, 3);
    for (auto& record : MutableData().items) {
        record.profession_mask = 0xffff;
        record.race_mask = 0xffff;
        record.gender_mask = 3;
        record.attribute_requirements[0].stat_id = 0xff;
        record.attribute_requirements[1].stat_id = 0xff;
        record.skill_requirements[0].stat_id = 0xff;
        record.skill_requirements[1].stat_id = 0xff;
    }
    MutableData().items[0].equip_class = W8_ITEM_EQUIP_CLASS_RANGED_WEAPON;
    MutableData().items[0].unidentified_name_index = 0x83;
    MutableData().items[1].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    MutableData().items[2].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    CurrentGame().party.rows[0].fOccupied = true;
    auto& character = characters[0];
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo = 2;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    SetHandType(Data(), &character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    REQUIRE(character.Hand[1].uiHolds == 3);
    CommitWeaponSetSwap(0, {true, false});
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 1);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == 0);
    REQUIRE(character.Hand[0].uiHolds == 1);
    REQUIRE(character.Hand[1].uiHolds == 1);
}

TEST_CASE_METHOD(LevelWorldFixture,
                 "A failed alternate-weapon search leaves equipment and binding state untouched",
                 "[equipment]")
{
    const ScopedGame game;
    auto& characters = CurrentGame().party.characters;
    auto& rows = CurrentGame().party.rows;
    const ScopedRecords items(MutableData().items, 2);
    auto& records = MutableData().items;
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
    records[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    records[0].weapon_skill = W8_SKILL_SWORD;
    records[1].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    records[1].weapon_skill = W8_SKILL_THROWING_SLING;
    records[1].range_category = W8_RANGE_LONG;
    records[1].binds_on_equip = 1;
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
    row.fOccupied = true;
    row.committed_action.kind = W8_ACTION_NONE;
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
    struct RestoreState {
        std::unique_ptr<W8SpellCastingView> view = std::move(gpSCSV);
        ~RestoreState() { gpSCSV = std::move(view); }
    } restore;
    rows[0].fOccupied = true;
    characters[0].hp_current = 1;
    rows[0].committed_action.kind = W8_ACTION_CAST_SPELL;
    rows[0].committed_action.detail = 5;
    rows[0].committed_action.detail_block.spell.power_level = 2;
    rows[0].committed_action.target = {W8_TARGET_KIND_MONSTER, -1, 17, -1, {}, {}};
    rows[0].spell = {W8_ACTION_CAST_SPELL, 7};
    gpSCSV = std::make_unique<W8SpellCastingView>();
    gpSCSV->draft = rows[0].spell;
    gpSCSV->draft.detail = 9;
    gpSCSV->draft.detail_block.spell.power_level = 3;
    W8CombatSlot target{W8_TARGET_KIND_CHARACTER, 0, -1, -1, {}, {}};
    CommitActionTarget(0, W8_TARGETING_CONTEXT_SHARED, target, false);
    const auto assessed = SelectCombatAction(0, W8_TARGETING_CONTEXT_SHARED);
    REQUIRE(assessed.detail == 9);
    REQUIRE(assessed.target == target);
    gpSCSV->draft.target.iType = W8_TARGET_KIND_NONE;
    REQUIRE(assessed.target == target);
    gpSCSV.reset();
    REQUIRE(rows[0].committed_action.detail == 5);
    REQUIRE(rows[0].committed_action.detail_block.spell.power_level == 2);
    REQUIRE(rows[0].committed_action.target.iMonsterID == 17);
    REQUIRE(rows[0].spell.detail == 7);
    REQUIRE(rows[0].spell.target.iType == W8_TARGET_KIND_NONE);
}

TEST_CASE("Trade confirmation rejects changed inventory and stock sources", "[trade]")
{
    const ScopedGame game;
    auto& rows = CurrentGame().party.rows;
    auto& characters = CurrentGame().party.characters;
    rows[0].fOccupied = true;
    auto& item = characters[0].backpack[3];
    item = {.iItemNo = 17, .stack_count = 4};
    const W8TradeSelection selected{W8TradeSource::Backpack, 0, 3, item};
    REQUIRE(ResolveTradeSelection(nullptr, selected) == &item);
    item.uses_or_charges = 2;
    REQUIRE_FALSE(ResolveTradeSelection(nullptr, selected));
    W8NpcState npc{};
    npc.items = {{0, {.iItemNo = 17, .stack_count = 4}, 1},
                 {0, {.iItemNo = 18, .stack_count = 3}, 1}};
    const W8TradeSelection stock{W8TradeSource::NpcStock, -1, 0, npc.items[0].item, 1};
    REQUIRE(ResolveTradeSelection(&npc, stock) == &npc.items[0].item);
    std::swap(npc.items[0], npc.items[1]);
    REQUIRE_FALSE(ResolveTradeSelection(&npc, stock));
    REQUIRE(CompleteNpcTrade(&npc, stock, 1).outcome == W8TradeOutcome::InvalidSelection);
    REQUIRE(npc.items[0].item.stack_count == 3);
}

TEST_CASE("A picker constrains layout without restoring an obsolete preference", "[layout]")
{
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
    const ScopedRecords items(MutableData().items, 2);
    MutableData().items[0].flags = W8_ITEM_FLAG_NO_DISCARD;
    MutableData().items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    MutableData().items[0].maximum_quantity = 10;
    MutableData().items[0].initial_quantity = {0, 1, 6};
    MutableData().items[0].value = 10;
    CurrentGame().party.gold = 100;
    CurrentGame().party.item_count = 500;
    for (auto& item : CurrentGame().party.item_pool) item = {.iItemNo = 1};

    CurrentGame().party.held.item = {.iItemNo = 1};
    W8NpcDatabaseRecord record{};
    record.sell_price_factor = 1.0f;
    W8NpcState npc{};
    npc.record = &record;
    npc.items = {{0, {.iItemNo = 0, .stack_count = 1}, 1}};
    SeedRandom(1);
    REQUIRE(CalculateNpcTradeStackPrice(&npc, 0, W8_TRADE_PRICE_PARTY_BUYS, 1, true) == 10);
    REQUIRE(Random15() == 41); // Drawing a price must not roll an item quantity.
    const auto result = CompleteNpcItemPurchase(&npc, 0, 1, false);
    REQUIRE(result.outcome == W8TradeOutcome::Refused);
    REQUIRE(CurrentGame().party.gold == 100);
    REQUIRE(CurrentGame().party.item_count == 500);
    REQUIRE(CurrentGame().party.item_pool[0].iItemNo == 1);
    REQUIRE(CurrentGame().party.held.item.iItemNo == 1);
    REQUIRE(npc.items.size() == 1);
    REQUIRE(npc.items[0].quantity == 1);
}

TEST_CASE("Encounter retirement spends one budget unit per formation, without registration",
          "[groups]")
{
    const ScopedGame game;
    struct Restore {
        std::unique_ptr<World> world = ReplaceWorld(std::make_unique<World>(0));
        ScopedCombat combat{true};
        ~Restore() { ReplaceWorld(std::move(world)); }
    } restore;
    const ScopedRecords levels(MutableData().levels, 1);
    MutableData().levels[0].minimum_encounter_budget = 1;
    MutableData().levels[0].maximum_encounter_budget = 20;
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
    struct Restore {
        std::unique_ptr<World> world = ReplaceWorld(std::make_unique<World>(0));
        ~Restore() { ReplaceWorld(std::move(world)); }
    } restore;
    const ScopedRecords levels(MutableData().levels, 2);
    MutableData().levels[1].minimum_encounter_budget = 2;
    MutableData().levels[1].maximum_encounter_budget = 9;
    MutableData().levels[1].encounter_culling_seconds = 123;
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
    const ScopedGame game;
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
    struct Restore {
        std::unique_ptr<World> world = ReplaceWorld(std::make_unique<World>(0));
        ScopedCombat combat{true};
        ~Restore() { ReplaceWorld(std::move(world)); }
    } restore;
    const ScopedRecords levels(MutableData().levels, 1);
    MutableData().levels[0].minimum_encounter_budget = 1;
    MutableData().levels[0].maximum_encounter_budget = 20;
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
        record->p3D = monster.get();
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

TEST_CASE("Detached party weight uses stable slot ties and drops the odd half-unit",
          "[party][encumbrance]")
{
    const ScopedRecords items(MutableData().items, 1);
    MutableData().items[0].weight = 7;
    W8Party party;
    for (int slot : {0, 1}) {
        party.rows[slot].fOccupied = true;
        party.characters[slot].carrying_capacity = 100;
        party.characters[slot].load_category = W8_LOAD_NONE;
    }
    party.item_pool[0] = {.iItemNo = 0, .stack_count = 1};
    party.item_count = 1;
    REQUIRE(RefreshPartyLoad(Data(), party, {}));
    REQUIRE(party.characters[0].party_weight_share == 2);
    REQUIRE(party.characters[1].party_weight_share == 1);
    REQUIRE(party.characters[0].total_carried_weight == 2);
    REQUIRE(party.characters[1].total_carried_weight == 1);
    party.rows[0].fOccupied = false;
    REQUIRE(RefreshPartyLoad(Data(), party, {}));
    REQUIRE(party.characters[0].party_weight_share == 0);
    REQUIRE(party.characters[1].party_weight_share == 3);
}

TEST_CASE("Inventory weight reports overload without scheduling a voice event",
          "[party][encumbrance]")
{
    const ScopedRecords items(MutableData().items, 1);
    MutableData().items[0].weight = 7;
    W8Character character{};
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    for (auto& item : character.backpack) item.iItemNo = -1;
    character.carrying_capacity = 10;
    character.inventory_weight = 7;
    character.backpack[0] = {.iItemNo = 0, .stack_count = 2};
    const auto changed = RecalculateInventoryWeight(Data(), character);
    REQUIRE(character.inventory_weight == 14);
    REQUIRE(changed.changed);
    REQUIRE(changed.became_overloaded);
    const auto unchanged = RecalculateInventoryWeight(Data(), character);
    REQUIRE_FALSE(unchanged.changed);
    REQUIRE_FALSE(unchanged.became_overloaded);
}

TEST_CASE("Party load follows supplied rules independently of the live combat and party",
          "[party][encumbrance][combat]")
{
    const ScopedGame game;
    const ScopedCombat combat{true};
    const ScopedRecords items(MutableData().items, 1);
    MutableData().items[0].weight = 18;
    CurrentGame().party.characters[0].party_weight_share = 123;
    W8Party party;
    party.rows[0].fOccupied = true;
    auto& character = party.characters[0];
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    for (auto& item : character.backpack) item.iItemNo = -1;
    character.fInParty = true;
    character.iRace = W8_RACE_ANDROID;
    character.carrying_capacity = 10;
    character.load_category = W8_LOAD_NONE;
    character.party_weight_share = 4;
    party.item_pool[0] = {.iItemNo = 0, .stack_count = 1};
    party.item_count = 1;
    PartyStatRules rules;
    rules.load_timing = PartyLoadTiming::AfterCombat;
    REQUIRE_FALSE(RefreshPartyLoad(Data(), party, rules));
    REQUIRE(party.redistribution_pending);
    REQUIRE(character.party_weight_share == 4);
    rules.load_timing = PartyLoadTiming::Immediate;
    rules.characters[0].defensive_action = true;
    REQUIRE(RefreshPartyLoad(Data(), party, rules));
    REQUIRE_FALSE(party.redistribution_pending);
    REQUIRE(character.party_weight_share == 9);
    REQUIRE(character.armor_class_components[W8_AC_COMPONENT_DEFENSIVE_ACTION] == 2);
    REQUIRE(CurrentGame().combat.active());
    REQUIRE(CurrentGame().party.characters[0].party_weight_share == 123);
}

TEST_CASE("Formation reconciliation repairs only its owning party", "[party][formation]")
{
    const ScopedGame game;
    W8Party party;
    party.formation.setPosition(0, 0, 0, false, true);
    party.formation.setPosition(1, 0, 1, false, true);
    party.rows[1].fOccupied = true;
    const auto previous = party;
    party.formation.reconcile(party.rows);
    REQUIRE(party.formation.positions[0].bQuadrant == -1);
    REQUIRE(party.formation.positions[1].bQuadrantSlot == 0);
    REQUIRE(party.formation.ubQuadrantOccupants[0] == 1);
    REQUIRE(previous.formation.ubQuadrantOccupants[0] == 2);
    REQUIRE(previous.formation.positions[1].bQuadrantSlot == 1);
}

TEST_CASE("A detached hit-point calculation reports death without applying live conditions",
          "[party][stats][ownership]")
{
    const ScopedGame game;
    W8Character character{};
    character.iProfession = W8_PROFESSION_FIGHTER;
    character.original_profession = W8_PROFESSION_FIGHTER;
    character.uiHPMax = 1000;
    character.hp_current = 1;
    character.profession_levels[W8_PROFESSION_FIGHTER] = 1;
    const auto live_hp = CurrentGame().party.characters[0].hp_current;
    const auto live_condition = CurrentGame().party.characters[0].highest_condition;
    REQUIRE(RecalculateCharacterHitPoints(&character));
    REQUIRE(character.hp_current == 0);
    REQUIRE(CurrentGame().party.characters[0].hp_current == live_hp);
    REQUIRE(CurrentGame().party.characters[0].highest_condition == live_condition);
    REQUIRE_FALSE(RecalculateCharacterHitPoints(&character));
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
    foreign.equipment_bonus.damage_bonus = 2;
    RebuildCharacterModifierBlock(&foreign, nullptr);
    REQUIRE(foreign.bonus.damage_bonus == 2);
    W8Party detached;
    detached.modifiers.damage_bonus = 7;
    RebuildCharacterModifierBlock(&foreign, &detached.modifiers);
    REQUIRE(foreign.bonus.damage_bonus == 9);
    const auto result = UpdateStandaloneCharacterStats(Data(), foreign, {});
    REQUIRE(result.valid);
    REQUIRE(foreign.party_weight_share == 0);
    REQUIRE(CurrentGame().party.characters[0].party_weight_share == 99);
    // Character creation uses a party storage slot before that row is occupied.
    CurrentGame().party.characters[0] = foreign;
    auto& draft = CurrentGame().party.characters[0];
    draft.carrying_capacity = 0;
    draft.party_weight_share = 99;
    RecalculateCharacterDerivedStats(draft);
    REQUIRE(draft.carrying_capacity > 0);
    REQUIRE(draft.party_weight_share == 0);
    REQUIRE(draft.bonus.damage_bonus == 0);
}

TEST_CASE("Weapon swings see the damage bonus left by the previous calculation", "[party][stats]")
{
    const ScopedRecords items(MutableData().items, 1);
    MutableData().items[0].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    MutableData().items[0].weapon_skill = W8_SKILL_SWORD;
    W8Character character{};
    character.iProfession = W8_PROFESSION_FIGHTER;
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.attributes[W8_ATTRIBUTE_SPEED].effective = 100;
    character.attributes[W8_ATTRIBUTE_STRENGTH].effective = 50;
    character.skills[W8_SKILL_SWORD].level = 90;
    character.skills[W8_SKILL_CLOSE_COMBAT].level = 90;
    character.Hand[0].damage_bonus = 3;

    // Retail scores swings before resetting the bonus: (100 + 30 + 90) / 3 = 73.
    CalcAttacks(Data(), &character, false);
    REQUIRE(character.Hand[0].damage_bonus == 0);
    REQUIRE(character.Hand[0].swings == 2);
    // The next calculation sees this arrangement's bonus: (100 + 0 + 90) / 3 = 63.
    CalcAttacks(Data(), &character, false);
    REQUIRE(character.Hand[0].swings == 1);
}

namespace {
// The active party as an inventory command sees it outside a running game.
struct ScopedActiveParty {
    ScopedGame game;
    int cursor = std::exchange(gXStatus.iCurrentCursor, W8_CURSOR_NONE);
    ~ScopedActiveParty() { gXStatus.iCurrentCursor = cursor; }
};

W8Character& PrepareMember(unsigned int slot)
{
    CurrentGame().party.rows[slot].fOccupied = true;
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
    const ScopedRecords items(MutableData().items, 1);
    auto& record = MutableData().items[0];
    record.quantity_kind = W8_ITEM_QUANTITY_STACK;
    record.maximum_quantity = 100;
    record.weight = 10;
    auto& actor = PrepareMember(0);
    auto& party = CurrentGame().party;
    actor.backpack[0] = W8ItemInstance{.iItemNo = 0, .stack_count = 10};

    REQUIRE(SplitItemStack({W8ItemStorage::Backpack, 0, 0}, 3, 0));
    REQUIRE(actor.backpack[0].stack_count == 7);
    REQUIRE(party.held.item.stack_count == 3);
    REQUIRE(party.held.origin == W8ItemLocation{W8ItemStorage::Backpack, 0, 0});
    REQUIRE(actor.inventory_weight == 70);
    REQUIRE(SplitItemStack({W8ItemStorage::Backpack, 0, 0}, 1, 0));
    REQUIRE(party.held.item.stack_count == 4);
    REQUIRE(actor.inventory_weight == 60);
    const auto result =
        MergeInventoryStacks({W8ItemStorage::Backpack, 0, 0}, {W8ItemStorage::Hand});
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
    const ScopedRecords items(MutableData().items, 1);
    MutableData().items[0].quantity_kind = W8_ITEM_QUANTITY_STACK;
    MutableData().items[0].maximum_quantity = 100;
    auto& party = CurrentGame().party;
    party.held = {W8ItemInstance{.iItemNo = 0, .stack_count = 10}, {}, 0};
    for (auto& item : party.characters[0].backpack)
        item = W8ItemInstance{.iItemNo = 0, .stack_count = 100};
    for (auto& item : party.item_pool) item = W8ItemInstance{.iItemNo = 0, .stack_count = 100};
    party.item_count = 500;
    REQUIRE_FALSE(SplitItemStack({W8ItemStorage::Hand}, 4, 0));
    REQUIRE(party.held.item.stack_count == 10);
    REQUIRE(party.item_count == 500);
    REQUIRE(party.characters[0].backpack[0].stack_count == 100);
    REQUIRE_FALSE(SplitItemStack({W8ItemStorage::Hand}, 11, 0));
    REQUIRE_FALSE(SplitItemStack({W8ItemStorage::Hand}, 0, 0));
}

TEST_CASE("Unusable equipment comes off in slot order and never stays equipped",
          "[party][inventory][stats]")
{
    const ScopedActiveParty active;
    const ScopedRecords items(MutableData().items, 3);
    for (auto& record : MutableData().items) {
        record.profession_mask = 0xffff;
        record.race_mask = 0xffff;
        record.gender_mask = 3; // Both genders.
        record.attribute_requirements[0].stat_id = 0xff; // No requirement.
        record.attribute_requirements[1].stat_id = 0xff;
        record.skill_requirements[0].stat_id = 0xff;
        record.skill_requirements[1].stat_id = 0xff;
    }
    // The first item supplies strength to the second, but itself requires speed.
    MutableData().items[0].modifier_0b3_index = W8_ATTRIBUTE_STRENGTH;
    MutableData().items[0].modifier_0b3_value = 20;
    MutableData().items[0].attribute_requirements[0] = {W8_ATTRIBUTE_SPEED, 60};
    MutableData().items[1].attribute_requirements[0] = {W8_ATTRIBUTE_STRENGTH, 60};
    auto& character = PrepareMember(0);
    character.EquippedItem[W8_EQUIP_SLOT_HEAD].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo = 1;

    SettleActiveParty();
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_HEAD].iItemNo == -1);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == -1);
    REQUIRE(character.backpack[0].iItemNo == 0);
    REQUIRE(character.backpack[1].iItemNo == 1);
    REQUIRE(character.attributes[W8_ATTRIBUTE_STRENGTH].effective == 50);

    // With no storage left, an item that may not be discarded falls into the hand.
    auto& party = CurrentGame().party;
    MutableData().items[1].flags |= W8_ITEM_FLAG_NO_DISCARD;
    for (auto& item : character.backpack) item = W8ItemInstance{.iItemNo = 2};
    for (auto& item : party.item_pool) item = W8ItemInstance{.iItemNo = 2};
    party.item_count = 500;
    character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo = 1;
    SettleActiveParty();
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_TORSO].iItemNo == -1);
    REQUIRE(party.held.item.iItemNo == 1);
}

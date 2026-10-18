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
#include "input.h"
#include "layouts/item_instance.h"
#include "layouts/npc_state.h"
#include "mgs_commands.h"
#include "save_payload.h"
#include "scoped_asset_roots.h"
#include "scoped_records.h"
#include "sight_state.h"
#include "temporary_directory.h"
#include "wiz8/character_skills.h"
#include "wiz8/conditions.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/equipment_slots.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/layouts/world.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_roster.h"
#include "wiz8/npc_items.h"
#include "wiz8/xstatus.h"

struct LevelWorldFixture {
    std::unique_ptr<W8World> previous = ReplaceWorld(std::make_unique<W8World>());
    ~LevelWorldFixture() { ReplaceWorld(std::move(previous)); }
};

static std::vector<int> masterCalls;

static void laterMaster(int command)
{
    masterCalls.push_back(30 + command);
    g_remove_current_master_function = false;
}

static void firstMaster(int command)
{
    masterCalls.push_back(10 + command);
    g_remove_current_master_function = false;
    if (command == 0) {
        g_master_functions.push_back({laterMaster});
    }
}

static void removingMaster(int command)
{
    masterCalls.push_back(20 + command);
    g_remove_current_master_function = true;
}

static bool masterFunctionMutation()
{
    g_master_functions = {{firstMaster}, {removingMaster}};
    RunMasterFunctions();
    REQUIRE((masterCalls == std::vector<int>{10, 20}));
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
    REQUIRE((GetMonsterGroupEncounter(0) == group_ptr && GetMonsterGroupEncounter(1) == nullptr));

    // Preparing and abandoning another population cannot publish its identities.
    live->location_id = 7;
    {
        W8World replacement;
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
    REQUIRE(GetMonsterGroupEncounter(0) == nullptr);
    return true;
}

static bool npcStockOrdering()
{
    const ScopedRecords items(g_item_records, 2);
    auto& records = g_item_records;
    records[0].unidentified_name_index = 20;
    records[1].unidentified_name_index = 10;

    W8NpcState npc{};
    npc.items.resize(3);
    npc.items[0].available_at = 100;
    npc.items[1].item.iItemNo = 1;
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
    const ScopedRecords items(g_item_records, 3);
    auto& records = g_item_records;
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
    REQUIRE(choice->primary);
    REQUIRE(choice->secondary);
    const auto proposed =
        EvaluateWeaponHands(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON],
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
    std::array<W8Character, 8> characters{};
    struct RestoreBuffers {
        W8StatusBuffers previous = g_status.buffers;
        ~RestoreBuffers() { g_status.buffers = previous; }
    } restore;
    g_status.buffers.Char = characters.data();
    const ScopedRecords items(g_item_records, 3);
    g_item_records[0].equip_class = W8_ITEM_EQUIP_CLASS_RANGED_WEAPON;
    g_item_records[0].unidentified_name_index = 0x83;
    g_item_records[1].equip_class = W8_ITEM_EQUIP_CLASS_SHORT_WEAPON;
    g_item_records[2].equip_class = W8_ITEM_EQUIP_CLASS_THROWN_WEAPON;
    auto& character = characters[0];
    for (auto& item : character.EquippedItem) item.iItemNo = -1;
    character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo = 0;
    character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo = 2;
    character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo = 1;
    SetHandType(&character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    REQUIRE(character.Hand[1].uiHolds == 3);
    CommitWeaponSetSwap(0, {true, false}, false);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 1);
    REQUIRE(character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == 0);
    REQUIRE(character.Hand[0].uiHolds == 1);
    REQUIRE(character.Hand[1].uiHolds == 1);
}

TEST_CASE_METHOD(LevelWorldFixture,
                 "A failed alternate-weapon search leaves equipment and binding state untouched",
                 "[equipment]")
{
    std::array<W8Character, 8> characters{};
    std::array<W8PartySlotRow, 8> rows{};
    const ScopedRecords items(g_item_records, 2);
    auto& records = g_item_records;
    struct RestoreState {
        W8StatusBuffers buffers = g_status.buffers;
        unsigned char autoswap = g_settings.autoswap_weapons;
        std::unique_ptr<W8CombatState> combat = std::move(g_combat_state);
        ~RestoreState()
        {
            g_status.buffers = buffers;
            g_settings.autoswap_weapons = autoswap;
            g_combat_state = std::move(combat);
        }
    } restore;
    g_status.buffers.Char = characters.data();
    g_status.buffers.XChar = rows.data();
    g_settings.autoswap_weapons = true;
    g_combat_state = std::make_unique<W8CombatState>();
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
    std::array<W8PartySlotRow, 8> rows{};
    std::array<W8Character, 8> characters{};
    struct RestoreBuffers {
        W8StatusBuffers previous = g_status.buffers;
        ~RestoreBuffers() { g_status.buffers = previous; }
    } restore;
    g_status.buffers.XChar = rows.data();
    g_status.buffers.Char = characters.data();
    auto& row = rows[0];
    auto& character = characters[0];
    row.fOccupied = true;
    row.action = W8_ACTION_NONE;
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
    std::array<W8PartySlotRow, 8> rows{};
    std::array<W8Character, 8> characters{};
    struct RestoreState {
        W8StatusBuffers buffers = g_status.buffers;
        bool casting = gXStatus.fSpellCastMode;
        std::unique_ptr<W8SpellCastingView> view = std::move(gpSCSV);
        ~RestoreState()
        {
            gpSCSV = std::move(view);
            gXStatus.fSpellCastMode = casting;
            g_status.buffers = buffers;
        }
    } restore;
    g_status.buffers.XChar = rows.data();
    g_status.buffers.Char = characters.data();
    rows[0].fOccupied = true;
    characters[0].hp_current = 1;
    rows[0].action = W8_ACTION_CAST_SPELL;
    rows[0].action_detail0 = 5;
    rows[0].spell = {W8_ACTION_CAST_SPELL, 7};
    gpSCSV = std::make_unique<W8SpellCastingView>();
    gpSCSV->draft = rows[0].spell;
    gpSCSV->draft.detail = 9;
    gpSCSV->draft.detail_block.spell.power_level = 3;
    gXStatus.fSpellCastMode = true;
    W8CombatSlot target{W8_TARGET_KIND_CHARACTER, 0, -1, -1, {}, nullptr};
    CommitActionTarget(0, W8_TARGETING_CONTEXT_SHARED, target, false);
    const auto assessed = SelectCombatAction(0, W8_TARGETING_CONTEXT_SHARED);
    REQUIRE(assessed.detail == 9);
    REQUIRE(assessed.target == target);
    gpSCSV->draft.target.iType = W8_TARGET_KIND_NONE;
    REQUIRE(assessed.target == target);
    gpSCSV.reset();
    REQUIRE(rows[0].action_detail0 == 5);
    REQUIRE(rows[0].spell.detail == 7);
    REQUIRE(rows[0].spell.target.iType == W8_TARGET_KIND_NONE);
}

TEST_CASE("Trade confirmation rejects changed inventory and stock sources", "[trade]")
{
    std::array<W8PartySlotRow, 8> rows{};
    std::array<W8Character, 8> characters{};
    struct RestoreBuffers {
        W8StatusBuffers previous = g_status.buffers;
        ~RestoreBuffers() { g_status.buffers = previous; }
    } restore;
    g_status.buffers.XChar = rows.data();
    g_status.buffers.Char = characters.data();
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
        W8MainUiMode mode = g_settings.main_ui_mode;
        bool casting = gXStatus.fSpellCastMode;
        ~Restore()
        {
            g_settings.main_ui_mode = mode;
            gXStatus.fSpellCastMode = casting;
        }
    } restore;
    g_settings.main_ui_mode = W8_MAIN_UI_MODE_RADAR;
    gXStatus.fSpellCastMode = true;
    REQUIRE(GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_FORMATION);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_RADAR);
    g_settings.main_ui_mode = W8_MAIN_UI_MODE_PORTRAITS;
    gXStatus.fSpellCastMode = false;
    REQUIRE(GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS);
}

TEST_CASE("A refused purchase leaves stock, payment and a full destination unchanged", "[trade]")
{
    std::array<W8PartySlotRow, 8> rows{};
    std::array<W8Character, 8> characters{};
    struct Restore {
        W8GlobalStatus previous = g_status;
        ~Restore() { g_status = std::move(previous); }
    } restore;
    g_status.buffers = {0, characters.data(), rows.data()};
    const ScopedRecords items(g_item_records, 2);
    g_item_records[0].flags = W8_ITEM_FLAG_NO_DISCARD;
    g_item_records[0].value = 10;
    g_status.party_gold = 100;
    g_status.party_item_count = 500;
    for (auto& item : g_status.party_item_pool) item = {.iItemNo = 1};
    g_status.item_in_cursor = true;
    g_status.item_in_hand = {.iItemNo = 1};
    W8NpcDatabaseRecord record{};
    record.sell_price_factor = 1.0f;
    W8NpcState npc{};
    npc.record = &record;
    npc.items = {{0, {.iItemNo = 0}, 1}};
    const auto result = CompleteNpcItemPurchase(&npc, 0, 1, false);
    REQUIRE(result.outcome == W8TradeOutcome::Refused);
    REQUIRE(g_status.party_gold == 100);
    REQUIRE(g_status.party_item_count == 500);
    REQUIRE(g_status.party_item_pool[0].iItemNo == 1);
    REQUIRE(g_status.item_in_hand.iItemNo == 1);
    REQUIRE(npc.items.size() == 1);
    REQUIRE(npc.items[0].quantity == 1);
}

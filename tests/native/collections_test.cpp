#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator> // IWYU pragma: keep - public header for std::istreambuf_iterator.
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <memory>
#include <span>
#include <unordered_map>

#include "wiz8/xstatus.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/npc_items.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "temporary_directory.h"
#include "scoped_asset_roots.h"
#include "fact_state.h"
#include "input.h"
#include "layouts/item_instance.h"
#include "layouts/npc_state.h"
#include "local_code/Widget.h"
#include "mgs_commands.h"
#include "regions.h"
#include "sight_state.h"


struct Entry : W8Widget {
    std::vector<int>& destroyed;
    int id;
    Entry* erase_also;
    Entry(Controls& owner, std::vector<int>& destroyed, int id, Entry* erase_also = nullptr)
        : W8Widget(&owner, -1u, 0, 0, 0, 0), destroyed(destroyed), id(id), erase_also(erase_also) {}
    ~Entry() override
    {
        destroyed.push_back(id);
        if (erase_also) {
            auto sibling = erase_also->m_pPanel->TakeControl(erase_also);
        }
    }
};

static bool ownershipAndCallbacks()
{
    std::vector<int> destroyed;
    Controls owner;
    new Entry(owner, destroyed, 1);
    auto* second = new Entry(owner, destroyed, 2);
    new Entry(owner, destroyed, 3, second);
    owner.DestroyAllControls();
    REQUIRE(owner.ControlCount() == 0);
    REQUIRE((destroyed == std::vector<int>{3, 2, 1}));

    auto* child = new Entry(owner, destroyed, 4);
    auto detached = owner.TakeControl(child);
    REQUIRE(detached.get() == child);
    REQUIRE(child->m_pPanel == nullptr);
    REQUIRE(owner.ControlCount() == 0);
    REQUIRE(destroyed.size() == 3);
    Controls destination;
    destination.AdoptControl(std::move(detached));
    REQUIRE(!detached);
    REQUIRE(destination.ControlAt(0) == child);
    REQUIRE(child->m_pPanel == &destination);
    destination.DestroyAllControls();
    REQUIRE(destroyed.back() == 4);
    {
        Controls automatic_owner;
        new Entry(automatic_owner, destroyed, 5);
        new Entry(automatic_owner, destroyed, 6);
    }
    REQUIRE((destroyed == std::vector<int>{3, 2, 1, 4, 6, 5}));
    return true;
}

// Exercise real region bindings rather than manually filling a panel vector.
struct RecordingWidget : W8Widget {
    std::vector<int>& events;
    int id;
    std::function<void()> on_event;
    RecordingWidget(Controls& owner, std::vector<int>& events, int id, unsigned region = -1u)
        : W8Widget(&owner, region, 0, 0, 20, 20), events(events), id(id) {}
    void OnLeftButtonDown(int) override { Fire(); }
    void OnLeftButtonUp(int) override { Fire(); }
    void OnRightButtonDown(int) override { Fire(); }
    void OnRightButtonUp(int) override { Fire(); }
    void OnMouseEnter(int) override { Fire(); }
    void OnMouseLeave(int) override { Fire(); }
    void Fire()
    {
        events.push_back(id);
        auto action = on_event;
        if (action) action(); // May delete this object.
    }
};

TEST_CASE("widget_region_lifetimes")
{
    // Destroy every set this case creates instead of shrinking the global
    // tables: the scan order and free lists still refer to those slots.
    struct DestroyCreatedSets {
        unsigned set_count = g_region_sets.size();
        ~DestroyCreatedSets()
        {
            for (unsigned set = g_region_sets.size(); set-- > set_count;) DestroyRegionSet(set);
        }
    } cleanup;
    std::vector<int> events;
    auto first_panel = std::make_unique<Controls>();
    first_panel->CreateRegionSet();
    auto* first = new RecordingWidget(*first_panel, events, 1);
    auto* second = new RecordingWidget(*first_panel, events, 2);
    auto* third = new RecordingWidget(*first_panel, events, 3);
    auto third_region = third->m_region;
    delete first;
    REQUIRE(first_panel->ControlCount() == 2);
    InputEvent event{};
    event.type = InputEventType::LeftButtonDown;
    REQUIRE(DispatchControlRegionEvent(&event, &g_regions[third_region]) == 1);
    REQUIRE(events.back() == 3);

    Controls second_panel;
    second_panel.CreateRegionSet();
    third->SetPanel(second_panel);
    third->SetPanel(second_panel);
    REQUIRE(second_panel.ControlCount() == 1);
    third_region = third->m_region; // Reparenting moved the region into second_panel's set.
    REQUIRE(std::ranges::contains(g_region_sets[second_panel.RegionSet()].Regions(),
                                  unsigned(third_region)));
    REQUIRE(first_panel->ControlCount() == 1);
    const auto second_region = second->m_region;
    auto detached_second = first_panel->TakeControl(second);
    REQUIRE(second->m_pPanel == nullptr);
    REQUIRE(g_regions[second_region].widget == nullptr);
    first_panel.reset();
    detached_second.reset(); // Detached widgets do not retain a dead panel pointer.
    REQUIRE(DispatchControlRegionEvent(&event, &g_regions[third_region]) == 1);
    REQUIRE(events.back() == 3);

    // A failed derived constructor must remove the base-class registration.
    struct FailingWidget : W8Widget {
        explicit FailingWidget(Controls& owner) : W8Widget(&owner, -1u, 0, 0, 10, 10)
        { throw std::runtime_error("incomplete widget"); }
    };
    Controls failing_panel;
    failing_panel.CreateRegionSet();
    const auto live_before_failure = LiveDynamicRegionCount();
    REQUIRE_THROWS_AS(new FailingWidget(failing_panel), std::runtime_error);
    REQUIRE(failing_panel.ControlCount() == 0);
    REQUIRE(g_region_sets[failing_panel.RegionSet()].Regions().empty());
    REQUIRE(LiveDynamicRegionCount() == live_before_failure);

    // An old widget may outlive a rebuilt panel reusing the same region slots.
    Controls rebuilt_panel;
    rebuilt_panel.CreateRegionSet();
    auto* old = new RecordingWidget(rebuilt_panel, events, 4);
    const auto reused_region = old->m_region;
    rebuilt_panel.CreateRegionSet();
    auto* replacement = new RecordingWidget(rebuilt_panel, events, 5);
    REQUIRE(replacement->m_region == reused_region);
    replacement->SetActive(true);
    delete old;
    REQUIRE(g_regions[reused_region].widget == replacement);
    REQUIRE(!(g_regions[reused_region].flags & W8_REGION_INPUT_DISABLED));
    REQUIRE(DispatchControlRegionEvent(&event, &g_regions[reused_region]) == 1);
    REQUIRE(events.back() == 5);

    // Post-callback latch writes must not corrupt a replacement's state.
    for (auto reason : {InputEventType::LeftButtonDown, InputEventType::LeftButtonUp, InputEventType::RightButtonDown, InputEventType::RightButtonUp}) {
        auto* victim = replacement;
        victim->on_event = [&, victim] {
            delete victim;
            rebuilt_panel.CreateRegionSet();
            replacement = new RecordingWidget(rebuilt_panel, events, 6);
            g_regions[reused_region].flags = W8_REGION_RECTANGLE;
        };
        event.type = reason;
        g_regions[reused_region].flags |= W8_REGION_LEFT_BUTTON_HELD | W8_REGION_RIGHT_BUTTON_HELD;
        REQUIRE(DispatchControlRegionEvent(&event, &g_regions[reused_region]) == 1);
        REQUIRE(g_regions[reused_region].flags == W8_REGION_RECTANGLE);
    }
    // Destroying a captured/hovered widget also releases those global views.
    g_captured_region_index = g_current_region_index = g_hover_region_index = reused_region;
    delete replacement;
    REQUIRE((g_captured_region_index == 0 && g_current_region_index == 0 && g_hover_region_index == 0));
    REQUIRE(rebuilt_panel.ControlCount() == 0);
    REQUIRE(DispatchControlRegionEvent(&event, &g_regions[reused_region]) == 0);

    Controls hover_panel;
    hover_panel.CreateRegionSet();
    auto* outgoing = new RecordingWidget(hover_panel, events, 7);
    auto* incoming = new RecordingWidget(hover_panel, events, 8);
    incoming->SetBounds(40, 0, 60, 20);
    hover_panel.SetEnabled(true);
    hover_panel.EnableRegionSet(true);
    PushButtonSoundScheme(0, true);
    const auto outgoing_region = outgoing->m_region;
    REQUIRE(UpdateRegionMousePosition(5, 5) == static_cast<unsigned>(outgoing_region));
    outgoing->on_event = [&] { delete incoming; incoming = nullptr; };
    PushButtonSoundScheme(0, true);
    REQUIRE(UpdateRegionMousePosition(45, 5) == 0);
    REQUIRE(incoming == nullptr);
    REQUIRE(events.back() == 7); // No enter dispatched to the deleted destination.

    // A leave callback can rebuild its own region. Cleanup belongs to the
    // old binding and must leave the replacement's transition flags alone.
    outgoing->on_event = [&] {
        delete outgoing;
        hover_panel.CreateRegionSet();
        outgoing = new RecordingWidget(hover_panel, events, 9);
        outgoing->SetActive(true);
        g_regions[outgoing_region].flags |= W8_REGION_MOUSE_ENTER;
    };
    g_hover_region_index = g_current_region_index = outgoing_region;
    REQUIRE(UpdateRegionMousePosition(100, 100) == 0);
    REQUIRE(g_regions[outgoing_region].widget == outgoing);
    REQUIRE(g_regions[outgoing_region].flags & W8_REGION_MOUSE_ENTER);
    REQUIRE(g_hover_region_index == 0);
    hover_panel.EnableRegionSet(false);
}

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
        g_master_functions.push_back(laterMaster);
    }
}

static void removingMaster(int command)
{
    masterCalls.push_back(20 + command);
    g_remove_current_master_function = true;
}

static bool masterFunctionMutation()
{
    g_master_functions = {firstMaster, removingMaster};
    RunMasterFunctions();
    REQUIRE((masterCalls == std::vector<int>{10, 20}));
    REQUIRE((g_master_functions == std::vector<W8MasterFunction>{firstMaster, laterMaster}));
    masterCalls.clear();
    SaveMasterFunctions();
    REQUIRE((masterCalls == std::vector<int>{9, 29}));
    g_master_functions.clear();
    return true;
}

static bool groupMemberLookup()
{
    W8MonsterGroup group{};
    REQUIRE((group.MemberIds().empty() && group.MemberLocationId(0) == -1));
    std::vector<int> members{101, 202};
    group.monsters = &members;
    REQUIRE(group.MemberIds().size() == 2);
    REQUIRE((group.MemberLocationId(0) == 101 && group.MemberLocationId(1) == 202));
    REQUIRE((group.MemberLocationId(2) == -1 && group.MemberLocationId(-1) == -1));
    members.erase(members.begin());
    REQUIRE(group.MemberLocationId(0) == 202);
    return true;
}

static bool gameplayCollectionLifetime()
{
    W8MonsterInfo monster{};
    W8MonsterGroup group{};
    REQUIRE((gXStatus.monsters.empty() && GetMonsterListEntry(0) == nullptr));
    gXStatus.monsters.push_back(&monster);
    gXStatus.unborn_monsters.push_back(&monster);
    gXStatus.encounter_groups.push_back(&group);
    REQUIRE(GetMonsterListEntry(0) == &monster);
    REQUIRE((GetMonsterListEntry(-1) == nullptr && GetMonsterListEntry(1) == nullptr));
    REQUIRE((GetUnbornMonsterEntry(0) == &monster && GetUnbornMonsterEntry(1) == nullptr));
    REQUIRE((GetMonsterGroupEncounter(0) == &group && GetMonsterGroupEncounter(1) == nullptr));
    // Reset releases vector storage and leaves immediately reusable collections.
    gXStatus = {};
    REQUIRE((GetMonsterListEntry(0) == nullptr && GetUnbornMonsterEntry(0) == nullptr));
    REQUIRE(GetMonsterGroupEncounter(0) == nullptr);
    gXStatus.monsters.push_back(&monster);
    REQUIRE(GetMonsterListEntry(0) == &monster);
    gXStatus.monsters.clear();
    return true;
}

static bool npcStockOrdering()
{
    std::array<W8ItemDatabaseRecord, 2> records{};
    records[0].unidentified_name_index = 20;
    records[1].unidentified_name_index = 10;
    auto* previous_records = g_item_records;
    g_item_records = records.data();

    auto* first = new W8NpcItemEntry{};
    auto* second = new W8NpcItemEntry{};
    auto* third = new W8NpcItemEntry{};
    first->item.iItemNo = third->item.iItemNo = 0;
    second->item.iItemNo = 1;
    first->available_at = 100;
    third->available_at = 200;
    std::vector<W8NpcItemEntry*> items{first, second, third};
    W8NpcState npc{};
    npc.items = &items;
    SortNpcItems(&npc);
    REQUIRE((items == std::vector<W8NpcItemEntry*>{second, first, third}));
    REQUIRE((first->available_at == 100 && third->available_at == 200));
    REQUIRE((GetNpcItemAt(&npc, -1) == nullptr && GetNpcItemAt(&npc, 3) == nullptr));
    REQUIRE(GetNpcItemAt(&npc, 0) == second);
    delete first;
    delete second;
    delete third;
    g_item_records = previous_records;
    return true;
}

static bool keyboardBindingReplacement()
{
    ScopedTemporaryDirectory temporary{"wiz8-keyboard"};
    const auto& root = temporary.path;
    const auto path = root / "bindings.bin";
    const std::array<MGSKeyBinding, 3> bindings{{
        {'A', 0, 1, W8_MGS_COMMAND_CANCEL},
        {'B', 0, 1, W8_MGS_COMMAND_MOVE_FORWARD},
        {'C', 0, 1, W8_MGS_COMMAND_CANCEL},
    }};
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        const int count = bindings.size();
        file->write(&count, sizeof(count));
        file->write(bindings.data(), sizeof(bindings));
    }
    MGSKeyboard keyboard;
    REQUIRE(keyboard.GetBinding(0) == nullptr);
    {
        auto file = wiz8::open_host_file(path);
        REQUIRE(keyboard.Load(*file, true));
    }
    // A duplicate command replaces its record and moves to the end of the
    // ordered bindings. The command index must refer to that replacement.
    REQUIRE(keyboard.FindBinding(W8_MGS_COMMAND_MOVE_FORWARD) == 0);
    REQUIRE(keyboard.FindBinding(W8_MGS_COMMAND_CANCEL) == 1);
    REQUIRE((keyboard.GetBinding(0)->key == 'B' && keyboard.GetBinding(1)->key == 'C'));
    REQUIRE(keyboard.GetBinding(-1) == nullptr);
    REQUIRE(keyboard.GetBinding(2) == keyboard.GetBinding(0));
    GetInputState().keys['A'] = true;
    REQUIRE(!keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    GetInputState().keys['A'] = false;
    GetInputState().keys['C'] = true;
    REQUIRE(keyboard.IsCommandPressed(W8_MGS_COMMAND_CANCEL));
    GetInputState().keys['C'] = false;
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
    ScopedAssetRoots roots{{wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user),
                            {"", "", ""}}};
    const auto input_path = root / "journal.bin", output_path = root / "journal-copy.bin";
    const int format = 1, count = 3;
    const std::array<W8JournalEntry, count> expected{{{1, static_cast<W8FactId>(10), 0},
                                                      {5, static_cast<W8FactId>(20), 7},
                                                      {9, static_cast<W8FactId>(30), -1}}};
    static_assert(sizeof(W8JournalEntry) == 12);
    {
        auto file = wiz8::open_host_file(input_path, wiz8::OpenMode::replace);
        file->write(&format, sizeof(format));
        file->write(&count, sizeof(count));
        file->write(expected.data(), sizeof(expected));
    }
    {
        auto file = wiz8::open_host_file(input_path);
        LoadJournalEntries(*file);
    }
    REQUIRE(g_fact_journal_entries.size() == count);
    for (int i = 0; i < count; ++i) {
        const auto& actual = g_fact_journal_entries[i];
        REQUIRE((actual.level == expected[i].level && actual.fact == expected[i].fact &&
              actual.alternate_text == expected[i].alternate_text));
    }
    {
        auto file = wiz8::open_host_file(output_path, wiz8::OpenMode::replace);
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
        auto file = wiz8::open_host_file(input_path, wiz8::OpenMode::replace);
        const int empty = 0;
        file->write(&format, sizeof(format));
        file->write(&empty, sizeof(empty));
    }
    {
        auto file = wiz8::open_host_file(input_path);
        LoadJournalEntries(*file);
    }
    REQUIRE(g_fact_journal_entries.empty());
    return true;
}

static bool visibilityRecordLifetime()
{
    W8MonsterInfo observer = {}, target = {};
    observer.location_id = 100;
    observer.fActive = true;
    target.location_id = 200;
    target.fActive = true;
    REQUIRE(!FindMonToMonVisibility(&observer, &target));
    observer.monster_visibility = new W8MonsterVisibility;
    auto& records = *observer.monster_visibility;
    auto& row = records[target.location_id];
    row.about_location_id = target.location_id;
    row.sight_state = W8_SIGHT_SEEN;
    auto* original = FindMonToMonVisibility(&observer, &target);
    REQUIRE(original == &row);
    for (int id = 1000; id < 2000; ++id) records[id].about_location_id = id;
    REQUIRE(FindMonToMonVisibility(&observer, &target) == original);
    REQUIRE(original->sight_state == W8_SIGHT_SEEN);
    gXStatus.monsters = {&observer, &target};
    ReleaseMonToMonVisibilityList(&target);
    REQUIRE(!FindMonToMonVisibility(&observer, &target));
    REQUIRE(records.size() == 1000);
    ReleaseMonToMonVisibilityList(&observer);
    REQUIRE(!observer.monster_visibility);
    ReleaseMonToMonVisibilityList(&observer);
    gXStatus.monsters.clear();
    return true;
}

TEST_CASE("collections_test")
{
    REQUIRE(ownershipAndCallbacks());
    REQUIRE(gameplayCollectionLifetime());
    REQUIRE(masterFunctionMutation());
    REQUIRE(groupMemberLookup());
    REQUIRE(npcStockOrdering());
    REQUIRE(keyboardBindingReplacement());
    REQUIRE(journalSerialization());
    REQUIRE(visibilityRecordLifetime());
}

TEST_CASE("region_storage_grows_without_aliasing_sets")
{
    std::vector<unsigned int> created_sets;
    struct ReleaseSets {
        std::vector<unsigned int>& sets;
        ~ReleaseSets()
        {
            for (auto it = sets.rbegin(); it != sets.rend(); ++it) DestroyRegionSet(*it);
        }
    } release{created_sets};
    const auto first = created_sets.emplace_back(CreateRegionSet());
    const auto second = created_sets.emplace_back(CreateRegionSet());
    const auto a = AddRegionToSet(first);
    const auto b = AddRegionToSet(second);
    const auto c = AddRegionToSet(first);
    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
    REQUIRE(g_region_sets[first].Regions().size() == 2);
    REQUIRE(g_region_sets[second].Regions().size() == 1);
    g_region_sets[first].SetInputEnabled(false);
    REQUIRE(g_regions[a].flags & W8_REGION_INPUT_DISABLED);
    REQUIRE(g_regions[c].flags & W8_REGION_INPUT_DISABLED);
    REQUIRE(!(g_regions[b].flags & W8_REGION_INPUT_DISABLED));
    g_region_sets[first].SetInputEnabled(true);
    REQUIRE(!(g_regions[c].flags & W8_REGION_INPUT_DISABLED));
    ResetRegionSet(first);
    REQUIRE(g_region_sets[first].Regions().empty());
    REQUIRE(AddRegionToSet(first) == a);
    REQUIRE(AddRegionToSet(first) == c);

    auto* region = &g_regions[a];
    auto* set = &g_region_sets[first];
    for (int i = 0; i < 1600; ++i) AddRegionToSet(first);
    for (int i = 0; i < 320; ++i) created_sets.push_back(CreateRegionSet());
    REQUIRE(&g_regions[a] == region);
    REQUIRE(&g_region_sets[first] == set);
    REQUIRE(g_regions.size() > 1500);
    REQUIRE(g_region_sets.size() > 300);
    REQUIRE(g_region_sets[second].Regions()[0] == b);
}

TEST_CASE("SDL region rectangles keep inclusive edges and circle hit tests")
{
    const auto size = g_regions.size();
    struct RestoreCatalog {
        size_t size;
        ~RestoreCatalog() { g_regions.resize(size); }
    } restore{size};
    auto& region = g_regions.emplace_back();
    region.flags = W8_REGION_RECTANGLE;
    region.x1 = 10;
    region.y1 = 20;
    region.x2 = 30;
    region.y2 = 40;
    for (unsigned short x : {10, 20, 30})
        for (unsigned short y : {20, 30, 40}) REQUIRE(g_regions[size].ContainsPoint(x, y));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(9, 30));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(31, 30));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(20, 19));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(20, 41));
    region.x2 = region.x1;
    region.y2 = region.y1;
    REQUIRE(g_regions[size].ContainsPoint(10, 20));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(11, 20));
    region.x2 = 9;
    REQUIRE_FALSE(g_regions[size].ContainsPoint(10, 20));
    region.x1 = region.y1 = 32767;
    region.x2 = region.y2 = 32767;
    REQUIRE(g_regions[size].ContainsPoint(32767, 32767));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(0, 0));
    region.x1 = region.y1 = -1;
    region.x2 = region.y2 = -1;
    REQUIRE_FALSE(g_regions[size].ContainsPoint(65535, 65535));
    region.flags = W8_REGION_CIRCLE;
    region.x1 = region.y1 = 20;
    region.x2 = 5;
    REQUIRE(g_regions[size].ContainsPoint(23, 24));
    REQUIRE_FALSE(g_regions[size].ContainsPoint(24, 24));
}

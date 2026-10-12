#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <SDL3/SDL_rect.h>
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
#include "wiz8/local_code/TextControl.h"
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
    Entry(std::vector<int>& destroyed, int id, Entry* erase_also = nullptr)
        : W8Widget(-1u, 0, 0, 0, 0), destroyed(destroyed), id(id), erase_also(erase_also) {}
    ~Entry() override
    {
        destroyed.push_back(id);
        if (erase_also) {
            erase_also->m_pPanel->RemoveControl(erase_also);
        }
    }
};

static bool ownershipAndCallbacks()
{
    std::vector<int> destroyed;
    Controls owner;
    owner.EmplaceControl<Entry>(destroyed, 1);
    auto* second = owner.EmplaceControl<Entry>(destroyed, 2);
    owner.EmplaceControl<Entry>(destroyed, 3, second);
    owner.DestroyAllControls();
    REQUIRE(owner.ControlCount() == 0);
    REQUIRE((destroyed == std::vector<int>{3, 2, 1}));

    auto* child = owner.EmplaceControl<Entry>(destroyed, 4);
    auto detached = owner.TakeControl(child);
    REQUIRE(detached.get() == child);
    REQUIRE(child->m_pPanel == nullptr);
    REQUIRE(owner.ControlCount() == 0);
    REQUIRE(destroyed.size() == 3);
    Controls destination;
    destination.AddControl(std::move(detached));
    REQUIRE(!detached);
    REQUIRE(destination.ControlAt(0) == child);
    REQUIRE(child->m_pPanel == &destination);
    destination.DestroyAllControls();
    REQUIRE(destroyed.back() == 4);
    {
        Controls automatic_owner;
        automatic_owner.EmplaceControl<Entry>(destroyed, 5);
        automatic_owner.EmplaceControl<Entry>(destroyed, 6);
    }
    REQUIRE((destroyed == std::vector<int>{3, 2, 1, 4, 6, 5}));
    return true;
}

TEST_CASE("panel_adopts_and_transfers_unattached_widgets")
{
    Controls source;
    source.SetBounds({100, 200, 100, 100});
    source.CreateRegionSet();

    auto pending = std::make_unique<W8Widget>(-1u, 1, 2, 20, 30);
    auto* widget = pending.get();
    REQUIRE(widget->m_pPanel == nullptr);
    REQUIRE(source.ControlCount() == 0);
    source.AddControl(std::move(pending));
    REQUIRE(pending == nullptr);
    REQUIRE(source.ControlAt(0) == widget);
    REQUIRE(widget->m_pPanel == &source);
    const auto original_region = widget->m_region;
    REQUIRE(original_region >= 0);
    REQUIRE(g_regions[original_region].handler == widget);
    REQUIRE(g_regions[original_region].x1 == 101);
    REQUIRE(g_regions[original_region].y1 == 202);

    Controls target;
    target.SetBounds({300, 400, 100, 100});
    target.CreateRegionSet();
    auto detached = source.TakeControl(widget);
    REQUIRE(source.ControlCount() == 0);
    REQUIRE(detached.get() == widget);
    REQUIRE(widget->m_pPanel == nullptr);
    REQUIRE(g_regions[original_region].handler == nullptr);
    target.AddControl(std::move(detached));
    REQUIRE(detached == nullptr);
    REQUIRE(target.ControlAt(0) == widget);
    REQUIRE(widget->m_pPanel == &target);
    REQUIRE(g_regions[widget->m_region].handler == widget);
    REQUIRE(g_regions[widget->m_region].x1 == 301);
    REQUIRE(g_regions[widget->m_region].y1 == 402);

    // Text layout is panel-relative: it is resolved on adoption and follows
    // reparenting and panel moves, as the hit region does. Centred 10px text
    // in the 15px-wide rectangle 3..18 starts 2px in.
    auto pending_text = std::make_unique<W8TextControl>(
        -1u, 3, 4, 18, 20);
    auto* text = pending_text.get();
    source.AddControl(std::move(pending_text));
    REQUIRE(text->m_textBuffer.GetHorizontalPosition(10) == 105);
    REQUIRE(g_regions[text->m_region].handler == text);
    target.AddControl(source.TakeControl(text));
    REQUIRE(text->m_textBuffer.GetHorizontalPosition(10) == 305);
    REQUIRE(g_regions[text->m_region].x1 == 303);
    target.SetBounds({500, 400, 100, 100});
    REQUIRE(text->m_textBuffer.GetHorizontalPosition(10) == 505);
    REQUIRE(g_regions[text->m_region].x1 == 503);
    REQUIRE(g_regions[widget->m_region].x1 == 501);
    // An explicit text rectangle is panel-relative too.
    text->UpdateTextBounds(20, 4, 35, 20);
    target.SetBounds({300, 400, 100, 100});
    REQUIRE(text->m_textBuffer.GetHorizontalPosition(10) == 322);
    target.RemoveControl(text);

    auto* another = target.EmplaceControl<W8Widget>();
    REQUIRE(target.ControlCount() == 2);
    REQUIRE(another->m_pPanel == &target);
    target.RemoveControl(another);
    REQUIRE(target.ControlCount() == 1);
    target.DestroyAllControls();
    REQUIRE(target.ControlCount() == 0);
}

TEST_CASE("panels_nest_as_widgets")
{
    Controls outer;
    outer.SetBounds({100, 100, 200, 200});
    auto* inner = outer.EmplaceControl<Controls>();
    inner->SetBounds({10, 20, 50, 50});
    inner->CreateRegionSet();
    auto* leaf = inner->EmplaceControl<W8Widget>(-1u, 1, 2, 11, 12);
    REQUIRE(leaf->ScreenBounds().x == 111);
    REQUIRE(g_regions[leaf->m_region].y1 == 122);

    outer.SetBounds({300, 100, 200, 200});
    REQUIRE(leaf->ScreenBounds().x == 311);
    REQUIRE(g_regions[leaf->m_region].x1 == 311);

    outer.SetActive(true);
    REQUIRE(inner->m_active);
    REQUIRE(leaf->m_active);
    REQUIRE(g_regions[leaf->m_region].InputEnabled());

    // A panel without a set of its own is never a hit target; its children
    // register in the set of the panel it joins, and leave it with the panel.
    outer.CreateRegionSet();
    auto row = std::make_unique<Controls>();
    row->SetBounds({5, 5, 50, 20});
    auto* hotspot = row->EmplaceControl<W8Widget>(-1u, 0, 0, 10, 10);
    REQUIRE(hotspot->m_region == -1);
    auto* attached_row = row.get();
    outer.AddControl(std::move(row));
    REQUIRE(attached_row->m_region == -1);
    REQUIRE(std::ranges::contains(g_region_sets[outer.RegionSet()].Regions(),
                                  unsigned(hotspot->m_region)));
    REQUIRE(g_regions[hotspot->m_region].x1 == 305);
    const auto hotspot_region = hotspot->m_region;
    auto detached_row = outer.TakeControl(attached_row);
    REQUIRE(hotspot->m_region == -1);
    REQUIRE(g_regions[hotspot_region].handler == nullptr);
}

TEST_CASE("active_panels_attach_detach_and_migrate_nested_regions")
{
    Controls outer;
    outer.SetBounds({100, 100, 100, 100});
    outer.CreateRegionSet();
    outer.EnableRegionSet(true);
    outer.SetActive(true);
    auto* inner = outer.EmplaceControl<Controls>();
    inner->SetBounds({10, 20, 50, 50});
    auto* leaf = inner->EmplaceControl<W8Widget>(-1u, 1, 2, 11, 12);
    REQUIRE(leaf->m_active);
    REQUIRE(FindRegionAtPoint(115, 125) == unsigned(leaf->m_region));
    const auto inherited_region = leaf->m_region;

    inner->CreateRegionSet();
    inner->EnableRegionSet(true);
    REQUIRE(!std::ranges::contains(g_region_sets[outer.RegionSet()].Regions(),
                                   unsigned(leaf->m_region)));
    REQUIRE(std::ranges::contains(g_region_sets[inner->RegionSet()].Regions(),
                                  unsigned(leaf->m_region)));
    REQUIRE(FindRegionAtPoint(115, 125) == unsigned(leaf->m_region));
    const auto live = LiveDynamicRegionCount();
    inner->CreateRegionSet();
    REQUIRE(LiveDynamicRegionCount() == live);
    REQUIRE(g_regions[leaf->m_region].owner == leaf);
    REQUIRE(g_regions[leaf->m_region].InputEnabled());

    // Independent sets must not leave detached subtrees in hit testing or capture.
    g_ui.captured = g_ui.current = g_ui.hover = leaf->m_region;
    auto detached = outer.TakeControl(inner);
    REQUIRE(leaf->m_region == -1);
    REQUIRE(FindRegionAtPoint(115, 125) == 0);
    REQUIRE((g_ui.captured == 0 && g_ui.current == 0 && g_ui.hover == 0));
    REQUIRE(g_regions[inherited_region].owner == nullptr);

    Controls destination;
    destination.SetBounds({200, 100, 100, 100});
    destination.SetActive(true);
    destination.AddControl(std::move(detached));
    REQUIRE(leaf->m_active);
    REQUIRE(FindRegionAtPoint(215, 125) == unsigned(leaf->m_region));
    REQUIRE(FindRegionAtPoint(115, 125) == 0);
}

TEST_CASE("circular_hit_areas_follow_panel_moves_and_reparenting")
{
    Controls source;
    source.SetActive(true);
    source.SetBounds({100, 100, 100, 100});
    source.CreateRegionSet();
    auto* pip = source.EmplaceControl<W8TextControl>(-1u, 10, 20, 30, 40);
    pip->SetCircularHitArea({20, 30}, 8);
    REQUIRE(g_regions[pip->m_region].ContainsPoint(120, 130));
    REQUIRE_FALSE(g_regions[pip->m_region].ContainsPoint(129, 130));
    source.SetBounds({200, 200, 100, 100});
    REQUIRE(g_regions[pip->m_region].ContainsPoint(220, 230));
    REQUIRE_FALSE(g_regions[pip->m_region].ContainsPoint(120, 130));

    Controls destination;
    destination.SetBounds({300, 100, 100, 100});
    destination.CreateRegionSet();
    destination.SetActive(true);
    destination.AddControl(source.TakeControl(pip));
    REQUIRE(g_regions[pip->m_region].ContainsPoint(320, 130));
    REQUIRE_FALSE(g_regions[pip->m_region].ContainsPoint(329, 130));
    pip->SetBounds({20, 30, 20, 20});
    REQUIRE(g_regions[pip->m_region].ContainsPoint(330, 140));
}

TEST_CASE("failed_panel_attachment_releases_prepared_descendant_regions")
{
    struct FailingLayout : Controls {
        bool& destroyed_unattached;
        explicit FailingLayout(bool& destroyed) : destroyed_unattached(destroyed) {}
        ~FailingLayout() override { destroyed_unattached = m_pPanel == nullptr; }
        void OnGeometryChanged() override
        {
            Controls::OnGeometryChanged();
            if (m_pPanel) throw std::runtime_error("layout failed after region binding");
        }
    };
    Controls owner;
    owner.CreateRegionSet();
    const auto live = LiveDynamicRegionCount();
    bool destroyed_unattached = false;
    auto pending = std::make_unique<FailingLayout>(destroyed_unattached);
    pending->EmplaceControl<W8Widget>();
    REQUIRE_THROWS_AS(owner.AddControl(std::move(pending)), std::runtime_error);
    REQUIRE(destroyed_unattached);
    REQUIRE(owner.ControlCount() == 0);
    REQUIRE(g_region_sets[owner.RegionSet()].Regions().empty());
    REQUIRE(LiveDynamicRegionCount() == live);
}

namespace {
int g_template_region_calls = 0;
unsigned char RecordTemplateRegionInput(const InputEvent*, W8Region*, unsigned short)
{
    ++g_template_region_calls;
    return 1;
}
} // namespace

// Regression: binding a widget to a screen-template region must leave the
// template's callback routing input (it carries screen behaviour such as
// formation drag-and-drop); the widget still positions and activates it.
TEST_CASE("template_regions_keep_their_callback_when_a_widget_binds_them")
{
    static constexpr std::array<RegionTemplate, 1> templates{{
        {W8_REGION_RECTANGLE, 0, 0, 0, 0, RecordTemplateRegionInput, 0, false, -1},
    }};
    OwnedRegionSet regions(templates);
    const auto index = regions.index(0);
    Controls panel;
    panel.SetBounds({100, 100, 100, 100});
    auto* widget = panel.EmplaceControl<W8Widget>(index, 1, 2, 11, 12);
    REQUIRE(g_regions[index].x1 == 101);
    panel.SetActive(true);
    REQUIRE(g_regions[index].InputEnabled());

    regions.Enable();
    REQUIRE(UpdateRegionMousePosition(105, 105) == index);
    g_template_region_calls = 0;
    InputEvent event{};
    event.type = InputEventType::LeftButtonDown;
    event.position = {105, 105};
    REQUIRE(DispatchRegionInput(&event) == 1);
    REQUIRE(g_template_region_calls == 1);

    panel.RemoveControl(widget);
    REQUIRE(!g_regions[index].InputEnabled());
    REQUIRE(g_regions[index].owner == nullptr);
    REQUIRE(g_regions[index].handler != nullptr);
}

// Exercise real region bindings rather than manually filling a panel vector.
struct RecordingWidget : W8Widget {
    std::vector<int>& events;
    int id;
    std::function<void()> on_event;
    RecordingWidget(std::vector<int>& events, int id, unsigned region = -1u)
        : W8Widget(region, 0, 0, 20, 20), events(events), id(id) {}
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

namespace {
int DispatchWidgetRegion(const InputEvent& event, W8Region& region)
{
    return region.handler ? region.handler->HandleRegionInput(region, event) : 0;
}
} // namespace

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
    auto* first = first_panel->EmplaceControl<RecordingWidget>(events, 1);
    auto* second = first_panel->EmplaceControl<RecordingWidget>(events, 2);
    auto* third = first_panel->EmplaceControl<RecordingWidget>(events, 3);
    auto third_region = third->m_region;
    first_panel->RemoveControl(first);
    REQUIRE(first_panel->ControlCount() == 2);
    InputEvent event{};
    event.type = InputEventType::LeftButtonDown;
    REQUIRE(DispatchWidgetRegion(event, g_regions[third_region]) == 1);
    REQUIRE(events.back() == 3);

    Controls second_panel;
    second_panel.CreateRegionSet();
    second_panel.AddControl(first_panel->TakeControl(third));
    REQUIRE(second_panel.ControlCount() == 1);
    third_region = third->m_region; // Reparenting moved the region into second_panel's set.
    REQUIRE(std::ranges::contains(g_region_sets[second_panel.RegionSet()].Regions(),
                                  unsigned(third_region)));
    REQUIRE(first_panel->ControlCount() == 1);
    const auto second_region = second->m_region;
    auto detached_second = first_panel->TakeControl(second);
    REQUIRE(second->m_pPanel == nullptr);
    REQUIRE(g_regions[second_region].handler == nullptr);
    first_panel.reset();
    detached_second.reset(); // Detached widgets do not retain a dead panel pointer.
    REQUIRE(DispatchWidgetRegion(event, g_regions[third_region]) == 1);
    REQUIRE(events.back() == 3);

    // A failed derived constructor must never register or enter the owner.
    struct FailingWidget : W8Widget {
        FailingWidget() : W8Widget(-1u, 0, 0, 10, 10)
        {
            throw std::runtime_error("incomplete widget");
        }
    };
    Controls failing_panel;
    failing_panel.CreateRegionSet();
    const auto live_before_failure = LiveDynamicRegionCount();
    REQUIRE_THROWS_AS(failing_panel.EmplaceControl<FailingWidget>(), std::runtime_error);
    REQUIRE(failing_panel.ControlCount() == 0);
    REQUIRE(g_region_sets[failing_panel.RegionSet()].Regions().empty());
    REQUIRE(LiveDynamicRegionCount() == live_before_failure);

    // An old widget may outlive a rebuilt panel reusing the same region slots.
    Controls rebuilt_panel;
    rebuilt_panel.CreateRegionSet();
    auto* old = rebuilt_panel.EmplaceControl<RecordingWidget>(events, 4);
    const auto reused_region = old->m_region;
    ResetRegionSet(rebuilt_panel.RegionSet());
    auto* replacement = rebuilt_panel.EmplaceControl<RecordingWidget>(events, 5);
    REQUIRE(replacement->m_region == reused_region);
    replacement->SetActive(true);
    rebuilt_panel.RemoveControl(old);
    REQUIRE(g_regions[reused_region].handler == replacement);
    REQUIRE(!(g_regions[reused_region].flags & W8_REGION_INPUT_DISABLED));
    REQUIRE(DispatchWidgetRegion(event, g_regions[reused_region]) == 1);
    REQUIRE(events.back() == 5);

    // Post-callback latch writes must not corrupt a replacement's state.
    for (auto reason : {InputEventType::LeftButtonDown, InputEventType::LeftButtonUp, InputEventType::RightButtonDown, InputEventType::RightButtonUp}) {
        auto* victim = replacement;
        victim->on_event = [&, victim] {
            rebuilt_panel.RemoveControl(victim);
            rebuilt_panel.CreateRegionSet();
            replacement = rebuilt_panel.EmplaceControl<RecordingWidget>(events, 6);
            g_regions[reused_region].flags = W8_REGION_RECTANGLE;
        };
        event.type = reason;
        g_regions[reused_region].flags |= W8_REGION_LEFT_BUTTON_HELD | W8_REGION_RIGHT_BUTTON_HELD;
        REQUIRE(DispatchWidgetRegion(event, g_regions[reused_region]) == 1);
        REQUIRE(g_regions[reused_region].flags == W8_REGION_RECTANGLE);
    }
    // Destroying a captured/hovered widget also releases those global views.
    g_ui.captured = g_ui.current = g_ui.hover = reused_region;
    rebuilt_panel.RemoveControl(replacement);
    REQUIRE((g_ui.captured == 0 && g_ui.current == 0 && g_ui.hover == 0));
    REQUIRE(rebuilt_panel.ControlCount() == 0);
    REQUIRE(DispatchWidgetRegion(event, g_regions[reused_region]) == 0);

    Controls hover_panel;
    hover_panel.CreateRegionSet();
    auto* outgoing = hover_panel.EmplaceControl<RecordingWidget>(events, 7);
    auto* incoming = hover_panel.EmplaceControl<RecordingWidget>(events, 8);
    incoming->SetBounds({40, 0, 20, 20});
    hover_panel.SetActive(true);
    hover_panel.EnableRegionSet(true);
    PushButtonSoundScheme(0, true);
    const auto outgoing_region = outgoing->m_region;
    REQUIRE(UpdateRegionMousePosition(5, 5) == static_cast<unsigned>(outgoing_region));
    outgoing->on_event = [&] { hover_panel.RemoveControl(incoming); incoming = nullptr; };
    PushButtonSoundScheme(0, true);
    REQUIRE(UpdateRegionMousePosition(45, 5) == 0);
    REQUIRE(incoming == nullptr);
    REQUIRE(events.back() == 7); // No enter dispatched to the deleted destination.

    // A leave callback can rebuild its own region. Cleanup belongs to the
    // old binding and must leave the replacement's transition flags alone.
    outgoing->on_event = [&] {
        hover_panel.RemoveControl(outgoing);
        hover_panel.CreateRegionSet();
        outgoing = hover_panel.EmplaceControl<RecordingWidget>(events, 9);
        outgoing->SetActive(true);
        g_regions[outgoing_region].flags |= W8_REGION_MOUSE_ENTER;
    };
    g_ui.hover = g_ui.current = outgoing_region;
    REQUIRE(UpdateRegionMousePosition(100, 100) == 0);
    REQUIRE(g_regions[outgoing_region].handler == outgoing);
    REQUIRE(g_regions[outgoing_region].flags & W8_REGION_MOUSE_ENTER);
    REQUIRE(g_ui.hover == 0);
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

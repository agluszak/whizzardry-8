#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include "wiz8/compat/surfaces.h"
#include <utility>
#include <stdexcept>
#include <limits>
#include "wiz8/item_video_object_vector.h"
#include "wiz8/layouts/item_tables.h"
#include <variant>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/xstatus.h"
#include "wiz8/video_object_catalog.h"
#include "image_assets.h"
#include "Button System.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/local_code/ButtonSound.h"
#include "Font.h"
#include "input.h"
#include "wiz8/regions.h"
#include "imgfmt.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"
#include "wiz8/text_input.h"
#include "Types.h"
#include "application.h"
#include "himage.h"
#include "layouts/main_game_screen.h"
#include "layouts/screen_state.h"
#include "local_code/RangeControl.h"
#include "local_screens/RCSStatsPage.h"
#include "vobject.h"
#include "vsurface.h"


extern GUI_BUTTON* gpAnchoredButton;

namespace {
std::vector<int>* scrollbar_position_sink = nullptr;
void RecordScrollPosition(W8DialogScrollBar*, int position)
{
    if (scrollbar_position_sink) scrollbar_position_sink->push_back(position);
}

class ScrollPositionCapture {
public:
    explicit ScrollPositionCapture(std::vector<int>& positions)
        : previous_(scrollbar_position_sink)
    {
        scrollbar_position_sink = &positions;
    }

    ~ScrollPositionCapture() { scrollbar_position_sink = previous_; }

private:
    std::vector<int>* previous_;
};

std::vector<int> screen_leaves;
int screen_entries, screen_frames;
unsigned char enter() { ++screen_entries; return 1; }
unsigned char leave(int discard) { screen_leaves.push_back(discard); return 1; }
void frame() { ++screen_frames; }

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count,
                          bool app_data, unsigned width, unsigned height);

void screen_stack()
{
    W8CharacterEventQueue queue;
    gXStatus.character_event_queue = &queue;
    const auto saved_intro = g_screen_handlers[W8_SCREEN_INTRO];
    const auto saved_options = g_screen_handlers[W8_SCREEN_OPTIONS];
    for (auto id : {W8_SCREEN_INTRO, W8_SCREEN_OPTIONS})
        g_screen_handlers[id] = {nullptr, enter, frame, leave, nullptr};
    g_current_screen_state = {};
    g_current_screen_state.id = W8_SCREEN_NONE;
    g_pending_screen_state.id = W8_SCREEN_NONE;
    g_screen_return_requested = false;
    REQUIRE(GetPendingScreenState() == W8_SCREEN_NONE);
    int payload;
    for (int index = 0; index < 40; ++index) {
        g_pending_screen_state = {};
        g_pending_screen_state.id = index % 2 ? W8_SCREEN_OPTIONS : W8_SCREEN_INTRO;
        g_pending_screen_state.parameter = index;
        g_pending_screen_state.parameter_3 = &payload;
        g_pending_screen_state.name = "screen " + std::to_string(index);
        REQUIRE(GetPendingScreenState() == g_pending_screen_state.id);
        GameLoop();
        REQUIRE(g_current_screen_state.parameter == index);
        REQUIRE(g_screen_return_stack.size() == static_cast<size_t>(index));
    }
    REQUIRE((screen_entries == 40 && screen_frames == 40));
    REQUIRE((screen_leaves.size() == 39 && screen_leaves.back() == 0));
    REQUIRE(GetPendingScreenState() == W8_SCREEN_INTRO);
    for (int index = 38; index >= 0; --index) {
        RequestScreenTransition();
        GameLoop();
        REQUIRE(g_current_screen_state.parameter == index);
        REQUIRE(g_current_screen_state.parameter_3 == &payload);
        REQUIRE(g_current_screen_state.name == std::string("screen ") + std::to_string(index));
        REQUIRE(g_screen_return_stack.size() == static_cast<size_t>(index));
        REQUIRE(screen_leaves.back() == 1);
    }
    g_screen_return_stack.push_back(g_current_screen_state);
    g_pending_screen_state = g_current_screen_state;
    g_pending_screen_state.id = W8_SCREEN_OPTIONS;
    g_pending_screen_state.parameter = 99;
    RequestScreenTransition();
    GameLoop();
    REQUIRE((g_current_screen_state.parameter == 99 && g_screen_return_stack.size() == 1));
    RequestScreenTransition();
    GameLoop();
    REQUIRE((g_current_screen_state.parameter == 0 && g_screen_return_stack.empty()));
    gfProgramIsRunning = true;
    RequestScreenTransition();
    GameLoop();
    REQUIRE((!gfProgramIsRunning && g_current_screen_state.id == W8_SCREEN_NONE));
    g_screen_return_requested = false;
    gXStatus.character_event_queue = nullptr;
    g_screen_handlers[W8_SCREEN_INTRO] = saved_intro;
    g_screen_handlers[W8_SCREEN_OPTIONS] = saved_options;
}

void camp_effects()
{
    ScopedTemporaryDirectory directory{"wiz8-camp-effects"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    const auto catalog_path = GetCatalogVideoObjectPath(0x86);
    const auto frame_count = static_cast<unsigned>(
        std::max(12, GetCatalogVideoObjectYOffset(0x86) + 12));
    button_image_fixture(assets, std::string(catalog_path), frame_count, false, 16, 8);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            g_camp_screen = nullptr;
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};
    {
        auto state = std::make_unique<W8CampScreenState>();
        REQUIRE((state->effect_list.empty() && state->stats_range == nullptr));
        g_camp_screen = state.get();
        W8CampStatsRange range;
        state->stats_range = &range;
        state->effect_list = {
            {false, false, true, false, 0, 0, 0, 3, 10}, {false, false, false, true, 0, 1, 0, 3, 9},
            {true, false, true, false, 2, 2, 0, 9999, 3}, {false, false, true, true, 0, 3, 0, 3, 2}};
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        state->effect_scroll = 100;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 24 && state->effect_scroll == 7));
        REQUIRE((state->effect_first_visible == 0 && state->effect_last_visible == 3));
        REQUIRE((range.m_range->m_enabled && range.m_range->m_value == 7));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 14 && state->effect_scroll == 0));
        REQUIRE((!range.m_range->m_enabled && !state->effect_list[1].visible));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_DETRIMENTAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 13 && state->effect_list[3].visible));
        state->effect_items_only = true;
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 4 && state->effect_first_visible == 2));
        W8Character character{};
        for (auto& condition : character.uiCondition) condition = 1;
        for (auto& enchantment : character.enchantments) enchantment.turns = 2;
        g_review_character = &character;
        state->effect_items_only = false;
        RebuildCampEffectList();
        REQUIRE(state->effect_list.size() == 28);
        REQUIRE((state->effect_detrimental_count == 20 && state->effect_beneficial_count == 8));
        REQUIRE((state->effect_list.front().index == 19 && state->effect_list.back().index == 0));
        REQUIRE((state->effect_visible_lines == 64 && range.m_range->m_enabled));
        character = {};
        RebuildCampEffectList();
        REQUIRE((state->effect_list.empty() && state->effect_visible_lines == 0));
        REQUIRE((state->effect_beneficial_count == 0 && !range.m_range->m_enabled));
        g_review_character = nullptr;
    }
}

int clicks = 0;
int click_reason = 0;
GUI_BUTTON* clicked_button = nullptr;
void click(GUI_BUTTON* button, std::int32_t reason)
{
    ++clicks;
    click_reason = reason;
    clicked_button = button;
}

std::uint64_t next_mouse_timestamp = 1;
InputAtom mouse_atom(std::uint16_t event, std::uint16_t x, std::uint16_t y, bool left = false,
                     bool right = false, std::uint8_t click_count = 0)
{
    InputAtom input{};
    input.uiTimeStamp = next_mouse_timestamp++;
    input.usEvent = event;
    input.uiParam = (static_cast<std::uint32_t>(y) << 16) | x;
    input.mouseButtons = (left ? SDL_BUTTON_LMASK : 0) | (right ? SDL_BUTTON_RMASK : 0);
    input.clicks = click_count;
    return input;
}
void dispatch_mouse(std::uint16_t event, std::uint16_t x, std::uint16_t y, bool left = false,
                    bool right = false, std::uint8_t click_count = 0)
{
    const auto input = mouse_atom(event, x, y, left, right, click_count);
    if (event == MOUSE_POS) UpdateRegionMousePosition(x, y);
    DispatchRegionInput(&input);
}

unsigned int new_test_region_set()
{
    const auto set = CreateRegionSet();
    RegionSetEnable(set);
    return set;
}

struct RegionStateSnapshot {
    std::vector<unsigned int> set_enabled;
    std::vector<unsigned int> region_flags;
    unsigned int current = g_current_region_index;
    unsigned int captured = g_captured_region_index;
    unsigned int hover = g_hover_region_index;
    int help_delay = g_region_help_delay;
    bool help_force_enabled = g_region_help_force_enabled;

    RegionStateSnapshot()
    {
        for (const auto& set : g_region_sets) set_enabled.push_back(set.enabled);
        for (const auto& region : g_regions) region_flags.push_back(region.flags);
    }

    ~RegionStateSnapshot()
    {
        for (std::size_t index = 0; index < g_region_sets.size(); ++index) {
            if (index < set_enabled.size()) {
                g_region_sets[index].enabled = set_enabled[index];
            } else {
                g_region_sets[index].enabled = g_region_sets[index].persistent ? 1 : 0;
            }
        }
        for (std::size_t index = 0;
             index < region_flags.size() && index < g_regions.size(); ++index) {
            g_regions[index].flags = region_flags[index];
        }
        g_current_region_index = current;
        g_captured_region_index = captured;
        g_hover_region_index = hover;
        g_region_help_delay = help_delay;
        g_region_help_force_enabled = help_force_enabled;
    }
};

unsigned int add_test_region(unsigned int set, unsigned short id, int x1, int y1, int x2, int y2,
                             int priority, W8RegionCallback callback)
{
    const auto index = AddRegionToSet(set, priority);
    g_regions[index].flags = W8_REGION_RECTANGLE;
    SetRegionBounds(index, static_cast<unsigned short>(x1), static_cast<unsigned short>(y1),
                    static_cast<unsigned short>(x2), static_cast<unsigned short>(y2));
    SetRegionCallback(index, callback, id);
    return index;
}

std::vector<std::pair<int, int>> region_events;
unsigned char record_region_event(const InputAtom* event, W8Region* region)
{
    region_events.emplace_back(region->callback_id, event->usEvent);
    return 1;
}
unsigned int panel_capture_index = 0;
unsigned char capture_panel_event(const InputAtom* event, W8Region* region)
{
    record_region_event(event, region);
    if (event->usEvent == LEFT_BUTTON_DOWN) g_captured_region_index = panel_capture_index;
    if (event->usEvent == LEFT_BUTTON_UP && g_captured_region_index == panel_capture_index)
        g_captured_region_index = 0;
    return 1;
}
unsigned char hover_region(const InputAtom*, W8Region* region)
{
    if (region->flags & W8_REGION_MOUSE_LEAVE)
        region_events.emplace_back(region->callback_id, BUTTON_REASON_LOST_MOUSE);
    else if (region->flags & W8_REGION_MOUSE_ENTER)
        region_events.emplace_back(region->callback_id, BUTTON_REASON_GAIN_MOUSE);
    else
        region_events.emplace_back(region->callback_id, BUTTON_REASON_MOVE);
    return 1;
}

unsigned int nested_region_set = 0;
unsigned int nested_region_index = 0;
bool remove_nested_region = false;
std::vector<int> nested_region_events;
unsigned char nested_region_callback(const InputAtom* event, W8Region*)
{
    if (event->usEvent == LEFT_BUTTON_DOWN) {
        nested_region_events.push_back(LEFT_BUTTON_DOWN);
        g_captured_region_index = nested_region_index;
        const auto up = mouse_atom(LEFT_BUTTON_UP, 100, 100);
        DispatchRegionInput(&up);
    } else if (event->usEvent == LEFT_BUTTON_UP) {
        nested_region_events.push_back(LEFT_BUTTON_UP);
        if (remove_nested_region) {
            remove_nested_region = false;
            RemoveRegionFromSet(nested_region_set, nested_region_index);
        }
    }
    return 1;
}
void region_nested_input()
{
    nested_region_set = new_test_region_set();
    nested_region_index =
        add_test_region(nested_region_set, 1, 0, 0, 40, 40, 0, nested_region_callback);
    nested_region_events.clear();
    remove_nested_region = true;
    {
        ExclusiveRegionSet scope(nested_region_set);
        dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10);
    }
    REQUIRE((nested_region_events == std::vector<int>{LEFT_BUTTON_DOWN, LEFT_BUTTON_UP}));
    REQUIRE(g_captured_region_index == 0);

    const auto region_count = g_regions.size();
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        const auto index = AddRegionToSet(nested_region_set, 0);
        REQUIRE(index == nested_region_index);
        RemoveRegionFromSet(nested_region_set, index);
    }
    REQUIRE(g_regions.size() == region_count);
}
void nested_hover_transitions()
{
    const auto set = new_test_region_set();
    add_test_region(set, 1, 0, 0, 10, 10, 0, hover_region);
    add_test_region(set, 2, 20, 0, 30, 10, 0, hover_region);
    region_events.clear();
    {
        ExclusiveRegionSet scope(set);
        UpdateRegionMousePosition(5, 5);
        region_events.clear();
        UpdateRegionMousePosition(25, 5);
    }
    REQUIRE(region_events.size() >= 2);
    REQUIRE((region_events[0] == std::pair<int, int>{1, BUTTON_REASON_LOST_MOUSE}));
    REQUIRE((region_events[1] == std::pair<int, int>{2, BUTTON_REASON_GAIN_MOUSE}));
}
void release_outside_controls()
{
    Font font{SGPVObject{}};
    auto left = CreateTextButton("left", font, 0, 0, {}, 0, 0, 10, 10, BUTTON_TOGGLE,
                                 BUTTON_PRIORITY_NORMAL, nullptr, click);
    auto right = CreateTextButton("right", font, 0, 0, {}, 20, 0, 10, 10, BUTTON_TOGGLE,
                                  BUTTON_PRIORITY_NORMAL, nullptr, click);
    REQUIRE((left && right));
    {
        ExclusiveRegionSet scope(ButtonRegionSetIndex());
        dispatch_mouse(LEFT_BUTTON_DOWN, 5, 5);
        REQUIRE(g_captured_region_index == left->region_index);
        dispatch_mouse(LEFT_BUTTON_UP, 100, 100);
        REQUIRE(g_captured_region_index == 0);
        dispatch_mouse(LEFT_BUTTON_DOWN, 25, 5);
        REQUIRE(clicked_button == right.get());
    }
}

void modal_scope_persists_across_frames()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = new_test_region_set();
    const auto region = add_test_region(set, 1, 0, 0, 20, 20, 0, hover_region);
    region_events.clear();

    std::optional<ExclusiveRegionSet> scope;
    scope.emplace(set);
    UpdateRegionMousePosition(5, 5);
    UpdateRegionMousePosition(5, 5);

    REQUIRE(g_hover_region_index == region);
    REQUIRE(std::count_if(region_events.begin(), region_events.end(), [](const auto& event) {
                return event.second == BUTTON_REASON_GAIN_MOUSE;
            }) == 1);
}

void modal_scope_preserves_capture_across_frames()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    Font font{SGPVObject{}};
    const auto button_set = ButtonRegionSetIndex();
    auto button = CreateTextButton("captured", font, 0, 0, {}, 10, 10, 20, 20, BUTTON_TOGGLE,
                                   BUTTON_PRIORITY_NORMAL, nullptr, click);
    REQUIRE(button);

    clicked_button = nullptr;
    click_reason = 0;
    std::optional<ExclusiveRegionSet> scope;
    scope.emplace(button_set);
    dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15);
    REQUIRE(g_captured_region_index == button->region_index);
    UpdateRegionMousePosition(100, 100);
    REQUIRE(g_captured_region_index == button->region_index);
    dispatch_mouse(LEFT_BUTTON_UP, 100, 100);

    REQUIRE(clicked_button == button.get());
    REQUIRE((click_reason & BUTTON_REASON_LBUTTON_UP) != 0);
    REQUIRE(g_captured_region_index == 0);
}

void exclusive_scopes_destroy_out_of_order()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto outer_set = new_test_region_set();
    const auto outer_region = add_test_region(outer_set, 1, 0, 0, 10, 10, 0, record_region_event);
    const auto inner_set = new_test_region_set();
    const auto inner_region = add_test_region(inner_set, 2, 20, 0, 30, 10, 0, record_region_event);
    const auto fallback_set = new_test_region_set();
    const auto fallback_region =
        add_test_region(fallback_set, 3, 40, 0, 50, 10, 0, record_region_event);

    std::optional<ExclusiveRegionSet> outer_scope;
    std::optional<ExclusiveRegionSet> inner_scope;
    outer_scope.emplace(outer_set);
    UpdateRegionMousePosition(5, 5);
    REQUIRE(g_hover_region_index == outer_region);
    inner_scope.emplace(inner_set);
    outer_scope.reset();
    UpdateRegionMousePosition(25, 5);
    REQUIRE(g_hover_region_index == inner_region);
    inner_scope.reset();
    REQUIRE(g_hover_region_index == 0);
    UpdateRegionMousePosition(45, 5);
    REQUIRE(g_hover_region_index == fallback_region);
}

void reset_regions_preserves_hidden_buttons()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    Font font{SGPVObject{}};
    const auto button_set = ButtonRegionSetIndex();
    auto button = CreateTextButton("hidden", font, 0, 0, {}, 70, 70, 20, 20, BUTTON_TOGGLE,
                                   BUTTON_PRIORITY_NORMAL, nullptr, click);
    REQUIRE(button);
    const auto region_index = button->region_index;
    HideButton(button);
    REQUIRE((g_regions[region_index].flags & W8_REGION_INPUT_DISABLED) != 0);

    ResetRegions();

    REQUIRE(g_region_sets[button_set].enabled == 1);
    REQUIRE((g_regions[region_index].flags & W8_REGION_INPUT_DISABLED) != 0);
    UpdateRegionMousePosition(75, 75);
    REQUIRE(g_hover_region_index != region_index);
}

void later_region_set_precedes_button_set()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    Font font{SGPVObject{}};
    const auto button_set = ButtonRegionSetIndex();
    auto button = CreateTextButton("button", font, 0, 0, {}, 100, 100, 20, 20, BUTTON_TOGGLE,
                                   BUTTON_PRIORITY_NORMAL, nullptr, click);
    REQUIRE(button);
    const auto screen_set = new_test_region_set();
    const auto screen_region =
        add_test_region(screen_set, 7, 100, 100, 120, 120, 0, record_region_event);
    REQUIRE(screen_set > button_set);
    clicked_button = nullptr;
    region_events.clear();

    UpdateRegionMousePosition(105, 105);
    REQUIRE(g_hover_region_index == screen_region);
    dispatch_mouse(LEFT_BUTTON_DOWN, 105, 105);

    REQUIRE(std::find(region_events.begin(), region_events.end(),
                      std::pair<int, int>{7, LEFT_BUTTON_DOWN}) != region_events.end());
    REQUIRE(clicked_button != button.get());
}

struct TestOwnedRegionHandler final : W8RegionHandler {
    int HandleRegionInput(W8Region&, const InputAtom&) override { return 1; }
};

void owned_region_move_and_teardown()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = new_test_region_set();
    TestOwnedRegionHandler handler;
    ExclusiveRegionSet scope(set);

    OwnedRegion owned(set, 0, handler);
    owned.region().flags = W8_REGION_RECTANGLE;
    SetRegionBounds(owned.index(), 10, 10, 30, 30);
    const auto index = owned.index();
    UpdateRegionMousePosition(15, 15);
    REQUIRE(g_hover_region_index == index);
    dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
    REQUIRE(g_captured_region_index == index);

    {
        OwnedRegion moved(std::move(owned));
        REQUIRE_FALSE(owned);
        owned.reset();
        REQUIRE(g_regions[index].handler == &handler);
        REQUIRE(g_captured_region_index == index);
        REQUIRE(g_hover_region_index == index);
    }
    REQUIRE(g_regions[index].handler == nullptr);
    REQUIRE(g_captured_region_index == 0);
    REQUIRE(g_hover_region_index == 0);
}
GUIButton nested_outer, nested_inner, removable_button;
void nested_click(GUI_BUTTON* button, std::int32_t reason)
{
    if (button == nested_inner.get()) {
        nested_outer.reset();
        return;
    }
    REQUIRE(button == nested_outer.get());
    QuickButtonCallbackMButn(nested_inner.get(), reason);
    REQUIRE(!nested_outer);
    nested_inner.reset();
}
void remove_button_on_callback(GUI_BUTTON*, std::int32_t)
{
    removable_button.reset();
    REQUIRE(!removable_button);
}
void region_registration_and_callback_lifetimes()
{
    Font font{SGPVObject{}};
    const auto set = new_test_region_set();
    const auto first = add_test_region(set, 1, 0, 0, 40, 40, 0, record_region_event);
    const auto latest = add_test_region(set, 2, 0, 0, 40, 40, 0, record_region_event);
    {
        ExclusiveRegionSet scope(set);
        region_events.clear();
        dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10);
    }
    REQUIRE(region_events.front().first == 1);
    RemoveRegionFromSet(set, first);
    region_events.clear();
    {
        ExclusiveRegionSet scope(set);
        dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10);
    }
    REQUIRE(region_events.front().first == 2);
    RemoveRegionFromSet(set, latest);

    nested_outer = CreateTextButton("nested", font, 0, 0, {}, 0, 0, 40, 20, BUTTON_TOGGLE,
                                    BUTTON_PRIORITY_NORMAL, nullptr, nested_click);
    nested_inner = CreateTextButton("nested", font, 0, 0, {}, 0, 20, 40, 20, BUTTON_TOGGLE,
                                    BUTTON_PRIORITY_NORMAL, nullptr, nested_click);
    REQUIRE((nested_outer && nested_inner));
    QuickButtonCallbackMButn(nested_outer.get(), BUTTON_REASON_LBUTTON_DWN);
    REQUIRE((!nested_outer && !nested_inner));

    removable_button = CreateTextButton("remove", font, 0, 0, {}, 0, 0, 40, 20, BUTTON_TOGGLE,
                                        BUTTON_PRIORITY_NORMAL, remove_button_on_callback, nullptr);
    REQUIRE(removable_button);
    auto weak = std::weak_ptr<GUI_BUTTON>(removable_button);
    {
        ExclusiveRegionSet scope(ButtonRegionSetIndex());
        UpdateRegionMousePosition(10, 10);
    }
    REQUIRE(weak.expired());
}
void buttons_and_regions()
{
    Font font{SGPVObject{}};
    const auto panel_set = new_test_region_set();
    const auto panel = add_test_region(panel_set, 7, 0, 0, 40, 40, 0, capture_panel_event);
    panel_capture_index = panel;
    const auto button_set = ButtonRegionSetIndex();
    auto low = CreateTextButton("low", font, 0, 0, {}, 0, 0, 40, 40, BUTTON_TOGGLE,
                                BUTTON_PRIORITY_LOW, nullptr, click);
    auto high = CreateTextButton("high", font, 0, 0, {}, 0, 0, 20, 20, BUTTON_TOGGLE,
                                 BUTTON_PRIORITY_HIGH, nullptr, click);
    REQUIRE((low && high));
    REQUIRE(g_regions[high->region_index].button == high.get());
    {
        ExclusiveRegionSet panel_scope(panel_set);
        UpdateRegionMousePosition(10, 10);
        REQUIRE(g_hover_region_index == panel);
        region_events.clear();
        dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10);
        REQUIRE(region_events.front().first == 7);
        REQUIRE(g_captured_region_index == panel);
        {
            ExclusiveRegionSet button_scope(button_set);
            dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10);
            REQUIRE(clicked_button == high.get());
            REQUIRE(g_captured_region_index == high->region_index);
            REQUIRE(std::count_if(region_events.begin(), region_events.end(), [](const auto& event) {
                        return event.second == LEFT_BUTTON_DOWN;
                    }) == 1);
        }
        REQUIRE(g_captured_region_index == panel);
        REQUIRE(g_hover_region_index == panel);
        dispatch_mouse(LEFT_BUTTON_UP, 100, 100);
        REQUIRE(g_captured_region_index == 0);
    }

    high->uiFlags = BUTTON_ENABLED | BUTTON_CLICK_CALLBACK;
    QuickButtonCallbackMButn(high.get(), BUTTON_REASON_LBUTTON_DWN);
    REQUIRE((clicks > 0 && click_reason == BUTTON_REASON_LBUTTON_DWN));
    high->uiFlags &= ~BUTTON_ENABLED;
    const auto click_count = clicks;
    QuickButtonCallbackMButn(high.get(), BUTTON_REASON_LBUTTON_DWN);
    REQUIRE(clicks == click_count);
    high->uiFlags |= BUTTON_ALLOW_DISABLED_CALLBACK;
    QuickButtonCallbackMButn(high.get(), BUTTON_REASON_LBUTTON_DWN);
    REQUIRE(clicks == click_count + 1);

    auto double_click = CreateTextButton("double", font, 0, 0, {}, 50, 0, 20, 20,
                                         BUTTON_TOGGLE, BUTTON_PRIORITY_NORMAL, nullptr, click);
    REQUIRE(double_click);
    {
        ExclusiveRegionSet scope(button_set);
        auto first_down = mouse_atom(LEFT_BUTTON_DOWN, 55, 5, true, false, 1);
        first_down.uiTimeStamp = 1000;
        DispatchRegionInput(&first_down);
        auto first_up = mouse_atom(LEFT_BUTTON_UP, 55, 5);
        first_up.uiTimeStamp = 1050;
        DispatchRegionInput(&first_up);
        auto second_down = mouse_atom(LEFT_BUTTON_DOWN, 55, 5, true, false, 2);
        second_down.uiTimeStamp = 1200;
        DispatchRegionInput(&second_down);
        REQUIRE((click_reason & BUTTON_REASON_LBUTTON_DOUBLECLICK) != 0);
        auto second_up = mouse_atom(LEFT_BUTTON_UP, 55, 5);
        second_up.uiTimeStamp = 1250;
        DispatchRegionInput(&second_up);
        const auto reason_after_up = click_reason;
        auto double_atom = mouse_atom(LEFT_BUTTON_DBL_CLK, 55, 5, false, false, 2);
        double_atom.uiTimeStamp = 1251;
        DispatchRegionInput(&double_atom);
        REQUIRE(click_reason == reason_after_up);
    }

    char label[] = "owned button";
    SpecifyButtonText(high, label);
    label[0] = 'x';
    REQUIRE(high->string[0] == 'o');
    char tooltip[] = "tooltip";
    SetButtonHelpText(high, tooltip);
    tooltip[0] = 'x';
    REQUIRE(g_regions[high->region_index].help_text == "tooltip");
    SetButtonHelpText(high, g_regions[high->region_index].help_text);
    REQUIRE(g_regions[high->region_index].help_text == "tooltip");
    SetButtonHelpText(high, {});
    REQUIRE(g_regions[high->region_index].help_text.empty());

    const auto region_count = g_regions.size();
    const auto high_region_index = high->region_index;
    const auto high_generation = g_regions[high_region_index].binding_generation;
    high.reset();
    REQUIRE(g_regions[high_region_index].button == nullptr);
    REQUIRE(g_regions[high_region_index].binding_generation > high_generation);
    low.reset();
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        auto button = CreateTextButton("reuse", font, 0, 0, {}, 0, 0, 40, 20, BUTTON_TOGGLE,
                                       BUTTON_PRIORITY_NORMAL, nullptr, click);
        REQUIRE(button);
        button.reset();
    }
    REQUIRE(g_regions.size() <= region_count + 1);
}

void image_and_sprite_ownership()
{
    SpriteSheet image;
    image.palette[1] = {255, 0, 0, 0};
    image.frames.resize(1);
    image.frames[0].indices = CreateCpuStorage(1, 1, SDL_PIXELFORMAT_RGB565);
    REQUIRE(image.frames[0].indices);
    REQUIRE(SDL_FillSurfaceRect(image.frames[0].indices.get(), nullptr, 1));
    image.frames[0].usWidth = image.frames[0].usHeight = 1;

    std::array<std::unique_ptr<SGPVObject>, 64> objects{};
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i] = std::make_unique<SGPVObject>(image);
        REQUIRE(objects[i]);
        REQUIRE((objects[i]->sprites.size() == 1 && objects[i]->sprites[0].indices->w == 1 && objects[i]->sprites[0].indices->h == 1));
        REQUIRE(objects[i]->sprites[0].indices->pixels != image.frames[0].indices->pixels);
        REQUIRE(objects[i]->pPaletteEntry.data() != image.palette.data());
        (void)CreateObjectPaletteTables(*objects[i], HVOBJECT_GLOW_GREEN);
        REQUIRE((objects[i]->pShades[0] && objects[i]->pShades[15]));
        REQUIRE(objects[i]->currentPalette() == objects[i]->palette);
        REQUIRE(SetObjectShade(*objects[i], 6));
        auto retained = objects[i]->palette;
        objects[i]->setPalette(image.palette);
        REQUIRE(objects[i]->palette == retained);
        REQUIRE(objects[i]->currentPalette() == objects[i]->palette);
        REQUIRE(SetObjectShade(*objects[i], 4));
        (void)CreateObjectPaletteTables(*objects[i], HVOBJECT_GLOW_RED);
        REQUIRE(objects[i]->currentPalette() == objects[i]->palette);
    }
    REQUIRE(SDL_FillSurfaceRect(image.frames[0].indices.get(), nullptr, 2));
    image.frames[0].usWidth = 2;
    REQUIRE((objects[0]->sprites[0].paletteIndexAt(0, 0) == 1 && objects[0]->sprites[0].usWidth == 1));
    const auto* owned_pixels = image.frames[0].indices->pixels;
    SGPVObject moved(std::move(image));
    REQUIRE(moved.sprites[0].indices->pixels == owned_pixels);
    REQUIRE(moved.sprites[0].paletteIndexAt(0, 0) == 2);
    image = {};
    REQUIRE(image.frames.empty());
    for (unsigned index : {0u, 31u, 63u}) {
        objects[index].reset();
    }
    auto& surviving = *objects[32];
    REQUIRE((surviving.sprites[0].paletteIndexAt(0, 0) == 1 && surviving.sprites[0].usWidth == 1));
    std::array<std::unique_ptr<SGPVSurface>, 64> surfaces{};
    for (auto& surface : surfaces) {
        surface = CreateVideoSurface(8, 8, 16);
        REQUIRE(surface);
    }
    auto& surviving_surface = *surfaces[32];
    for (unsigned index : {0u, 31u, 63u})
        surfaces[index].reset();
    REQUIRE((surviving_surface.ownedSurface && surviving_surface.width() == 8));

}

void font_resource_ownership()
{
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        auto translation = CreateEnglishTransTable();
        REQUIRE(translation.size() == 252);
        ConfigureFontRendering(translation);
        translation[1] = '?';
        REQUIRE(GetIndex('B') == 1);
        translation = {};
        REQUIRE(GetIndex('B') == 1);
        ConfigureFontRendering(CreateEnglishTransTable());
        REQUIRE(GetIndex('Z') == 25);
        ResetFontRendering();
        ResetFontRendering();
    }
}

void text_input_ownership()
{
    Font font{SGPVObject{}};
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        InitTextInputMode(font);
        const auto first = AddTextInputField(0, 0, 80, 20, BUTTON_PRIORITY_NORMAL,
                                             "alpha", 32, 0, 0);
        const auto middle = AddTextInputField(0, 20, 80, 20, BUTTON_PRIORITY_NORMAL,
                                              "beta", 32, 0, 0);
        const auto last = AddTextInputField(0, 40, 80, 20, BUTTON_PRIORITY_NORMAL,
                                            "gamma", 32, 0, 0);
        REQUIRE((first == 0 && middle == 1 && last == 2));
        std::string text;
        text = GetTextFromField(first);
        REQUIRE((text[0] == 'a' && GetTextInputFieldByteLength(first) == 5));
        if (repeat == 0) {
            const auto field_region = std::find_if(
                g_regions.begin(), g_regions.end(),
                [](const W8Region& region) { return region.text_field != nullptr; });
            REQUIRE(field_region != g_regions.end());
            const auto region_index =
                static_cast<unsigned int>(field_region - g_regions.begin());
            ExclusiveRegionSet scope(ButtonRegionSetIndex());
            dispatch_mouse(LEFT_BUTTON_DOWN, 5, 5);
            REQUIRE(g_captured_region_index == region_index);
            dispatch_mouse(LEFT_BUTTON_UP, 100, 100);
            REQUIRE(g_captured_region_index == 0);
        }
        RemoveTextInputField(middle);
        REQUIRE(GetTextInputFieldByteLength(last) == 5);
        InitTextInputMode(font);
        REQUIRE(AddTextInputField(0, 0, 80, 20, BUTTON_PRIORITY_NORMAL,
                                "inner", 32, 0, 0) == 0);
        KillTextInputMode();
        text = GetTextFromField(first);
        REQUIRE((text[0] == 'a' && GetTextInputFieldByteLength(last) == 5));
        REQUIRE(AddTextInputField(0, 60, 80, 20, BUTTON_PRIORITY_NORMAL,
                                "delta", 32, 0, 0) == 3);
        RemoveTextInputField(first);
        RemoveTextInputField(3);
        RemoveTextInputField(last);
        KillTextInputMode();
        REQUIRE(GetActiveTextInputField() == -1);
        InitTextInputMode(font);
        KillTextInputMode();
    }
}

void formatted_strings()
{
    const auto first = fmt::format("{} {:d}", "first", 7);
    const auto second = fmt::format("{} {:d}", "second", 9);
    REQUIRE((first == "first 7" && second == "second 9"));
    const std::string long_text(1024, 'x');
    REQUIRE(fmt::format("{}", long_text) == long_text);
    REQUIRE(fmt::format("{:d}", 42) == "42");
    REQUIRE(fmt::sprintf("%ls %hs", "é", "中") == "é 中");
    REQUIRE(std::string("100% literal") == "100% literal");
}

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count = 9,
                          bool app_data = false, unsigned width = 1, unsigned height = 1)
{
    REQUIRE((width > 0 && width < 128 && height > 0));
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto path = root / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    const auto write = [&file](const void* data, size_t size) {
        file.write(static_cast<const char*>(data), size);
        REQUIRE(file.good());
    };
    STCIHeader header{};
    memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
    const unsigned image_size = (width + 2) * height;
    header.uiOriginalSize = count * width * height;
    header.uiStoredSize = count * image_size;
    header.fFlags = STCI_INDEXED | STCI_ETRLE_COMPRESSED;
    header.usWidth = width;
    header.usHeight = height;
    header.Indexed.uiNumberOfColours = 256;
    header.Indexed.usNumberOfSubImages = count;
    header.ubDepth = 8;
    header.uiAppDataSize = app_data ? 4 : 0;
    write(&header, STCI_HEADER_SIZE);
    std::array<STCIPaletteElement, 256> palette{};
    palette[1].ubRed = 255;
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    std::vector<STCISubImage> regions(count);
    for (unsigned index = 0; index < count; ++index) {
        regions[index].uiDataOffset = index * image_size;
        regions[index].uiDataLength = image_size;
        regions[index].usWidth = width;
        regions[index].usHeight = height;
    }
    write(regions.data(), regions.size() * sizeof(STCISubImage));
    std::vector<std::uint8_t> pixels(width + 2, 1);
    pixels.front() = width;
    pixels.back() = 0;
    for (unsigned index = 0; index < count; ++index)
        for (unsigned row = 0; row < height; ++row)
            write(pixels.data(), pixels.size());
    if (app_data) {
        const std::uint8_t bytes[]{7, 8, 9, 10};
        write(bytes, sizeof(bytes));
    }
}

void generic_button_images()
{
    RegionStateSnapshot saved_regions;
    ResetRegions();
    const std::filesystem::path root = make_temporary_directory("wiz8-button-images");
    const auto assets = root / "assets";
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_OFF);
    button_image_fixture(assets, "button.sti");
    button_image_fixture(assets, "short.sti", 8);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (root / "user").string(), {"", "", ""}});

    button_image_fixture(assets, "metadata.sti", 3, true);
    {
        auto image = LoadImage("metadata.sti");
        REQUIRE(image);
        const auto* sheet = std::get_if<SpriteSheet>(&*image);
        REQUIRE(sheet);
        REQUIRE(sheet->frames.size() == 3);
        REQUIRE(sheet->frames[2].paletteIndexAt(0, 0) == 1);
        REQUIRE(sheet->palette[1].r == 255);

        const auto pixel_offset = STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE + 3 * STCI_SUBIMAGE_SIZE;
        for (std::uintmax_t cut : {0u, STCI_HEADER_SIZE - 1u,
             STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE - 1u,
             pixel_offset - 1u, pixel_offset + 8u, pixel_offset + 12u}) {
            button_image_fixture(assets, "metadata.sti", 3, true);
            std::filesystem::resize_file(assets / "metadata.sti", cut);
            REQUIRE(!LoadImage("metadata.sti"));
        }
    }
    std::string long_image_path;
    for (int i = 0; i < 8; ++i)
        long_image_path += "long-component-for-a-native-image-path/";
    long_image_path += "button.sti";
    REQUIRE(long_image_path.size() > 260);
    button_image_fixture(assets, long_image_path, 3);
    {
        auto sprite = LoadVideoObject(long_image_path);
        REQUIRE((sprite && sprite->sprites.size() == 3));
        REQUIRE((sprite->sprites[2].usWidth == 1 && sprite->sprites[2].paletteIndexAt(0, 0) == 1));
        REQUIRE(sprite->pPaletteEntry[1].r == 255);
    }
    for (bool corrupt_length : {false, true}) {
        button_image_fixture(assets, "bad-extent.sti", 3);
        {
            std::fstream file(assets / "bad-extent.sti", std::ios::in | std::ios::out | std::ios::binary);
            STCISubImage region{};
            region.uiDataOffset = corrupt_length ? 1 : 0xffffffffu;
            region.uiDataLength = corrupt_length ? 9 : 3;
            region.usWidth = region.usHeight = 1;
            file.seekp(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
            file.write(reinterpret_cast<const char*>(&region), STCI_SUBIMAGE_SIZE);
            REQUIRE(file.good());
        }
        REQUIRE(!LoadImage("bad-extent.sti"));
    }
    for (bool corrupt_app_data : {false, true}) {
        button_image_fixture(assets, "bad-size.sti", 3, true);
        {
            std::fstream file(assets / "bad-size.sti", std::ios::in | std::ios::out | std::ios::binary);
            STCIHeader header{};
            file.read(reinterpret_cast<char*>(&header), STCI_HEADER_SIZE);
            if (corrupt_app_data)
                header.uiAppDataSize = 0xffffffffu;
            else
                header.uiStoredSize = 0xffffffffu;
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&header), STCI_HEADER_SIZE);
            REQUIRE(file.good());
        }
        REQUIRE(!LoadImage("bad-size.sti"));
    }
    // A button retains its graphics after the loader's reference and the
    // image manager are released; the last button releases the resource.
    auto graphics = ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4);
    REQUIRE(graphics);
    REQUIRE((graphics->MaxWidth == 1 && graphics->MaxHeight == 1));
    std::weak_ptr<const ButtonGraphics> lifetime = graphics;
    auto first_button = QuickCreateButton(graphics, 0, 0, BUTTON_NO_TOGGLE,
                                          BUTTON_PRIORITY_NORMAL, nullptr, nullptr);
    auto second_button = QuickCreateButton(graphics, 2, 0, BUTTON_NO_TOGGLE,
                                           BUTTON_PRIORITY_NORMAL, nullptr, nullptr);
    REQUIRE((first_button && second_button));
    // Give each frame a distinct color and verify actual blits for both
    // button kinds, including checkbox-only mouse-down highlights.
    auto& sprite = graphics->object();
    for (unsigned frame = 0; frame < 5; ++frame) {
        REQUIRE(SDL_FillSurfaceRect(sprite.sprites[frame].indices.get(), nullptr, frame + 1));
        sprite.palette[frame + 1] = 0x1000 + frame;
    }
    auto target_owner = CreateVideoSurface(8, 8, 16);
    REQUIRE(target_owner);
    auto& target = *target_owner;
    const auto old_target = ButtonDestBuffer;
    const auto old_hilights = gfRenderHilights;
    const auto old_left_down = gfLeftButtonState;
    const auto old_hover_region = g_hover_region_index;
    auto& first_region = g_regions[first_button->region_index];
    const auto old_region_flags = first_region.flags;
    ButtonDestBuffer = &target;
    gfRenderHilights = true;
    for (bool checkbox : {false, true}) {
        for (bool enabled : {false, true}) {
            for (bool pressed : {false, true}) {
                for (bool hover : {false, true}) {
                    for (bool mouse_down : {false, true}) {
                        first_button->uiFlags = (checkbox ? BUTTON_CHECKBOX : BUTTON_QUICK) |
                            (enabled ? BUTTON_ENABLED : 0) | (pressed ? BUTTON_CLICKED_ON : 0);
                        g_hover_region_index =
                            hover ? first_button->region_index : 0;
                        gfLeftButtonState = mouse_down;
                        if (mouse_down)
                            first_region.flags |= W8_REGION_LEFT_BUTTON_HELD;
                        else
                            first_region.flags &= ~W8_REGION_LEFT_BUTTON_HELD;
                        DrawButton(first_button);
                        auto* pixels = static_cast<std::uint16_t*>(target.cpu().surface->pixels);
                        REQUIRE(pixels);
                        const auto color = pixels[0];
                        const bool highlight = hover && (!checkbox || mouse_down);
                        const unsigned expected_frame = !enabled ? 0 :
                            pressed ? (highlight ? 4 : 3) : (highlight ? 2 : 1);
                        REQUIRE(color == 0x1000 + expected_frame);
                    }
                }
            }
        }
    }
    ButtonDestBuffer = old_target;
    gfRenderHilights = old_hilights;
    gfLeftButtonState = old_left_down;
    g_hover_region_index = old_hover_region;
    first_region.flags = old_region_flags;
    target_owner.reset();
    graphics.reset();
    ShutdownButtonImageManager();
    REQUIRE(!lifetime.expired());
    first_button.reset();
    REQUIRE(second_button->quick_image->object().sprites[4].paletteIndexAt(0, 0) == 5);
    second_button.reset();
    REQUIRE(lifetime.expired());
    REQUIRE(RegisteredButtonCount() == 0);
    // Validate each of the five states, including invalid negative indices.
    for (int bad : {-2, 9, 65536}) {
        for (size_t state = 0; state < 5; ++state) {
            std::array<int, 5> frames{0, 1, 2, 3, 4};
            frames[state] = bad;
            REQUIRE(!ButtonGraphics::Load("button.sti", frames[0], frames[1], frames[2],
                                           frames[3], frames[4]));
        }
    }
    REQUIRE(!ButtonGraphics::Load("missing.sti", 0, 1, 2, 3, 4));
    REQUIRE(!ButtonGraphics::Load("button.sti", -1, -1, -1, -1, -1));
    REQUIRE(ButtonGraphics::Load("button.sti", -1, 0, -1, 1, -1));
    std::vector<ButtonImage> quick_images;
    for (unsigned index = 0; index < 300; ++index)
        quick_images.push_back(ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4));
    REQUIRE(std::all_of(quick_images.begin(), quick_images.end(),
                        [](const auto& item) { return item != nullptr; }));
    quick_images.clear();
    const auto handler_count = std::count_if(
        g_regions.begin(), g_regions.end(),
        [](const W8Region& region) { return region.handler != nullptr; });
    {
        auto dialog = std::make_unique<W8DialogButton>();
        REQUIRE(dialog->ConfigureIcon("button.sti", 0, 1, BUTTON_PRIORITY_NORMAL,
                                      W8_DIALOG_BUTTON_NO_TOOLTIP));
        dialog->SetPosition(12, 13);
        REQUIRE(!dialog->ConfigureIcon("missing.sti", 0, 1, BUTTON_PRIORITY_NORMAL,
                                       W8_DIALOG_BUTTON_NO_TOOLTIP));
        REQUIRE((dialog->GetX() == 12 && dialog->GetY() == 13 && dialog->GetWidth() == 1));
        REQUIRE(RegisteredButtonCount() == 0);
        REQUIRE(std::any_of(g_regions.begin(), g_regions.end(),
                            [&dialog](const W8Region& region) {
                                return region.handler == dialog.get();
                            }));
        REQUIRE(std::count_if(g_regions.begin(), g_regions.end(),
                              [](const W8Region& region) {
                                  return region.handler != nullptr;
                              }) == handler_count + 1);
    }
    REQUIRE(RegisteredButtonCount() == 0);
    REQUIRE(std::count_if(g_regions.begin(), g_regions.end(),
                          [](const W8Region& region) { return region.handler != nullptr; }) ==
            handler_count);
    // A dialog's click callback can close it. The dispatcher must not read
    // the released dialog while completing the click (including its sound).
    auto closing_dialog = std::make_unique<W8DialogButton>();
    REQUIRE(closing_dialog->Configure("button.sti", 0, 1, 2, 3, 4,
        [&](W8DialogButton*) { closing_dialog.reset(); }, nullptr, false,
        BUTTON_PRIORITY_NORMAL, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr));
    closing_dialog->SetPosition(12, 13);
    PushButtonSoundScheme(0, true);
    {
        ExclusiveRegionSet scope(ButtonRegionSetIndex());
        dispatch_mouse(LEFT_BUTTON_DOWN, 12, 13, true, false, 1);
        dispatch_mouse(LEFT_BUTTON_UP, 12, 13, false, false, 1);
    }
    REQUIRE_FALSE(closing_dialog);
    REQUIRE(RegisteredButtonCount() == 0);

    REQUIRE(!InitializeButtonImageManager());
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_ON);
    REQUIRE(InitializeButtonImageManager());
    const std::string_view filename = "button.sti", missing = "missing.sti";
    const auto load = [](const std::array<std::string_view, 6>& names) {
        return LoadGenericButtonImages(names[2], names[0], names[3], names[1],
                                       names[4], names[5], 3, 4, 5);
    };
    for (size_t index = 0; index < 6; ++index) {
        std::array<std::string_view, 6> names;
        names.fill(filename);
        names[index] = missing;
        REQUIRE(!load(names));
    }
    REQUIRE(!load({std::string_view{}, filename, {}, {}, {}, {}}));
    REQUIRE(!load({filename, {}, {}, {}, {}, {}}));
    REQUIRE(!load({"short.sti", filename, {}, {}, {}, {}}));
    // Every border state needs all nine tiles, not only the off-normal state.
    for (size_t state = 0; state < 5; ++state) {
        std::array<std::string_view, 6> names;
        names.fill(filename);
        names[state] = "short.sti";
        REQUIRE(!load(names));
    }
    REQUIRE(!LoadGenericButtonImages({}, filename, {}, filename, {}, filename, -1, 0, 0));
    REQUIRE(!LoadGenericButtonImages({}, filename, {}, filename, {}, filename, 9, 0, 0));
    // A zero-sized first tile would cause division by zero while drawing borders.
    button_image_fixture(assets, "zero-tile.sti");
    {
        std::fstream file(assets / "zero-tile.sti", std::ios::in | std::ios::out | std::ios::binary);
        STCISubImage frame{};
        file.seekg(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
        file.read(reinterpret_cast<char*>(&frame), sizeof(frame));
        frame.usWidth = 0;
        file.seekp(STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE);
        file.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        REQUIRE(file.good());
    }
    REQUIRE(!load({filename, "zero-tile.sti", {}, {}, {}, {}}));
    auto image = load({filename, filename, filename, filename, filename, filename});
    REQUIRE(image);
    REQUIRE((image->frames[2] && image->frames[3] && image->frames[4]));
    REQUIRE((image->frames[5] && image->background_frame == 3));
    REQUIRE((image->offset_x == 4 && image->offset_y == 5));
    REQUIRE(image->fill_color == image->frames[0]->palette[1]);
    auto another_owner = image;
    image.reset();
    REQUIRE(another_owner->frames[0]);
    another_owner.reset();

    // There is no fixed image slot limit. Lifetime is owned by the image.
    std::vector<GenericButtonImage> images;
    for (int index = 0; index < 45; ++index)
        images.push_back(load({filename, filename, {}, {}, {}, {}}));
    REQUIRE(std::all_of(images.begin(), images.end(), [](const auto& item) { return item != nullptr; }));
    images.clear();

    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        ConfigureFontRendering(CreateEnglishTransTable());
        Font primary;
        REQUIRE(primary.load(filename));
        auto& font = primary.glyphs;
        REQUIRE((font.sprites.size() == 9));
        Palette16 colours{};
        colours[1] = 0x1234;
        primary.setPalette(colours);
        colours[1] = 0;
        REQUIRE(primary.palette()[1] == 0x1234);
        SetFont(primary);
        {
            FontRenderScope outer;
            FontRenderScope inner;
            primary.clear();
            REQUIRE(g_font_render_state.font == nullptr);
        }
        REQUIRE(g_font_render_state.font == nullptr);
        ResetFontRendering();
    }
    // Font lifetime is independent of rendering setup and has no slot limit.
    std::vector<Font> many_fonts;
    for (unsigned index = 0; index < 32; ++index) {
        many_fonts.emplace_back();
        REQUIRE(many_fonts.back().load(filename));
    }
    ResetFontRendering();
    REQUIRE(many_fonts.back().height() > 0);
    many_fonts.clear();
    // Nested drawing restores each caller's state, not a single shared slot.
    {
        Font first, second;
        SetFont(first);
        g_font_render_state.clip = {1, 2, 3, 4};
        {
            FontRenderScope outer;
            SetFont(second);
            g_font_render_state.clip = {5, 6, 7, 8};
            {
                FontRenderScope inner;
                SetFont(first);
                g_font_render_state.clip = {9, 10, 11, 12};
            }
            REQUIRE(g_font_render_state.font == &second);
            REQUIRE(g_font_render_state.clip.x == 5);
        }
        REQUIRE(g_font_render_state.font == &first);
        REQUIRE(g_font_render_state.clip.x == 1);
    }
    // Failed loads retain selection; moves/reloads invalidate saved selections.
    {
        Font source, moved;
        REQUIRE(source.height() == 0);
        REQUIRE(source.textWidth("empty") == 0);
        REQUIRE(source.load(filename));
        const auto height = source.height();
        SetFont(source);
        {
            FontRenderScope scope;
            REQUIRE_FALSE(source.load("missing-font.sti"));
            REQUIRE(source.height() == height);
            REQUIRE(g_font_render_state.font == &source);
            Font temporary(std::move(source));
            REQUIRE(temporary.height() == height);
            REQUIRE(source.height() == 0);
            REQUIRE(g_font_render_state.font == nullptr);
            moved = std::move(temporary);
        }
        REQUIRE(g_font_render_state.font == nullptr);
        SetFont(moved);
        {
            FontRenderScope scope;
            REQUIRE(moved.load(filename));
            REQUIRE(g_font_render_state.font == nullptr);
        }
        REQUIRE(g_font_render_state.font == nullptr);
        SetFont(moved);
        {
            FontRenderScope scope;
            source = std::move(moved);
            REQUIRE(source.height() == height);
            REQUIRE(moved.height() == 0);
        }
        REQUIRE(g_font_render_state.font == nullptr);
    }
    {
        FontRenderScope outer;
        {
            Font source;
            SetFont(source);
            FontRenderScope inner;
            source.clear();
        }
    }
    REQUIRE(g_font_render_state.font == nullptr);
    ConfigureFontRendering(std::vector<std::uint16_t>{'?', 0x00e9, 0x4e2d, 'e', 0x0301, 'a', 'b', ' ', 'c'});
    Font primary;
    REQUIRE(primary.load(filename));
    REQUIRE(primary.textWidth("é") > 0);
    REQUIRE(primary.codepointPrefixWidth("é中", 1) == primary.textWidth("é"));
    REQUIRE(primary.codepointPrefixWidth("é中", 2) == primary.textWidth("é中"));
    // Render borrowed substrings without depending on a trailing NUL or
    // accidentally consuming the next glyph in the backing string.
    SetFont(primary);
    g_font_render_state.clip = {0, 0, 64, 32};
    std::vector<std::uint16_t> expected_pixels(64 * 32), view_pixels(64 * 32);
    DrawFontTextToBuffer(reinterpret_cast<std::uint8_t*>(expected_pixels.data()), 64 * 2, primary, 0, 0, "é");
    const std::string backing = "é中";
    DrawFontTextToBuffer(reinterpret_cast<std::uint8_t*>(view_pixels.data()), 64 * 2, primary, 0, 0,
                         std::string_view(backing).substr(0, 2));
    REQUIRE(view_pixels == expected_pixels);
    REQUIRE(
        std::any_of(view_pixels.begin(), view_pixels.end(), [](auto pixel) { return pixel != 0; }));
    // Explicit-buffer rendering must use its font argument, even when the
    // current surface font has a different palette.
    Font secondary;
    REQUIRE(secondary.load(filename));
    Palette16 alternate;
    alternate.fill(0x1234);
    secondary.setPalette(alternate);
    std::fill(expected_pixels.begin(), expected_pixels.end(), 0);
    std::fill(view_pixels.begin(), view_pixels.end(), 0);
    SetFont(secondary);
    DrawFontTextToBuffer(reinterpret_cast<std::uint8_t*>(expected_pixels.data()), 128, secondary, 0, 0, "é中");
    SetFont(primary);
    DrawFontTextToBuffer(reinterpret_cast<std::uint8_t*>(view_pixels.data()), 128, secondary, 0, 0, "é中");
    REQUIRE(view_pixels == expected_pixels);
    // Rebuilding word ranges preserves UTF-8 byte boundaries and discards
    // old hover state, including when a line becomes empty.
    auto level = std::make_unique<W8LevelRuntimeBlock>();
    auto* previous_level = g_level_block;
    g_level_block = level.get();
    level->text_box_font = &primary;
    W8MessageStorageRecord words{};
    words.text = "  é 中  ";
    words.RebuildEntries();
    REQUIRE(words.entries.size() == 2);
    REQUIRE((words.entries[0].start == 2 && words.entries[0].end == 3));
    REQUIRE((words.entries[1].start == 5 && words.entries[1].end == 7));
    REQUIRE(words.entries[1].x_start == primary.textWidth("  é "));
    words.entries[0].keyword = W8_NOTICE_WORD_SELECTED;
    words.text = "中";
    words.RebuildEntries();
    REQUIRE((words.entries.size() == 1 && words.entries[0].keyword == W8_NOTICE_WORD_NORMAL));
    words.text.clear();
    words.RebuildEntries();
    REQUIRE(words.entries.empty());
    g_level_block = previous_level;
    REQUIRE(g_font12point1.load(filename));
    InitTextInputMode(g_font12point1);
    const auto field = AddTextInputField(0, 0, 80, 20, BUTTON_PRIORITY_NORMAL, "", 1000, 0x0f, 0);
    SetActiveField(field);
    const std::string clusters = "é👩‍👩‍👧‍👦中";
    SetInputFieldText(field, clusters);
    SetTextInputCursor(clusters.size());
    InputAtom backspace{};
    backspace.usEvent = KEY_DOWN;
    backspace.usParam = 8;
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field) == "é👩‍👩‍👧‍👦");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field) == "é");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field).empty());
    InputAtom committed{};
    committed.usEvent = KEY_DOWN;
    committed.usParam = 0x4e2d;
    committed.uiParam = NATIVE_TEXT_INPUT_FLAG;
    REQUIRE(HandleTextInput(&committed));
    REQUIRE(GetTextFromField(field) == "中");
    committed.usParam = 0x1f600;
    REQUIRE(HandleTextInput(&committed));
    REQUIRE(GetTextFromField(field) == "中😀");
    SetInputFieldText(field, "");
    const std::array<char32_t, 7> family = {0x1f469, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466};
    for (const auto scalar : family) {
        committed.usParam = scalar;
        REQUIRE(HandleTextInput(&committed));
    }
    REQUIRE(GetTextFromField(field) == "👩‍👩‍👧‍👦");
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE(GetTextFromField(field).empty());
    std::string long_text;
    for (unsigned n = 0; n < 100; ++n) long_text += "中";
    SetInputFieldText(field, long_text);
    SetTextInputCursor(long_text.size());
    REQUIRE(GetTextInputByteCursor() == 300);
    REQUIRE(GetTextInputFieldByteLength(field) == 300);
    REQUIRE(HandleTextInput(&backspace));
    REQUIRE((GetTextInputByteCursor() == 297 && GetTextFromField(field).size() == 297));
    KillTextInputMode();
    ResetFontRendering();
    g_font12point1.clear();
    ShutdownButtonImageManager();

    g_image_assets.clear();
    std::filesystem::remove_all(root);
}
}

TEST_CASE("Screen stack ownership", "[owner]")
{
    screen_stack();
}

TEST_CASE("Camp effect ownership", "[owner]")
{
    camp_effects();
}

TEST_CASE("Button and mouse region ownership", "[owner]")
{
    buttons_and_regions();
}

TEST_CASE("Callback reentrancy and region teardown", "[owner]")
{
    region_registration_and_callback_lifetimes();
    region_nested_input();
    nested_hover_transitions();
    release_outside_controls();
    owned_region_move_and_teardown();
    modal_scope_persists_across_frames();
    modal_scope_preserves_capture_across_frames();
    exclusive_scopes_destroy_out_of_order();
    reset_regions_preserves_hidden_buttons();
    later_region_set_precedes_button_set();
}

TEST_CASE("Sprite ownership", "[owner]")
{
    image_and_sprite_ownership();
}

TEST_CASE("Font resource ownership", "[owner]")
{
    font_resource_ownership();
}

TEST_CASE("Text input ownership", "[owner]")
{
    text_input_ownership();
}

TEST_CASE("Formatting contracts", "[owner]")
{
    formatted_strings();
}

TEST_CASE("Generic button image ownership", "[owner]")
{
    generic_button_images();
}

TEST_CASE("Dialog controls own interaction without legacy GUI buttons", "[owner]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    ExclusiveRegionSet button_scope(ButtonRegionSetIndex());
    Font font{SGPVObject{}};
    PushButtonSoundScheme(0, true);
    int activations = 0;
    {
        W8DialogButton button;
        REQUIRE(button.ConfigureTextButton("button", font, 0, 0, 10, 10, 40, 20,
            [&](W8DialogButton* control) {
                REQUIRE(control->GetUserData() == 42);
                ++activations;
            }, 42));
        REQUIRE(RegisteredButtonCount() == 0);
        gfLeftButtonState = false;
        gusMouseXPos = gusMouseYPos = 400;
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true, false, 1);
        REQUIRE((button.IsPressed() && activations == 0));
        REQUIRE((button.GetMousePosition().x == 15 && button.GetMousePosition().y == 15));
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15, false, false, 1);
        REQUIRE(!button.IsPressed());
        REQUIRE(activations == 1);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE(activations == 1);
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        dispatch_mouse(MOUSE_POS, 80, 80, true);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        button.SetEnabled(false);
        button.SetEnabled(true);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        button.SetVisible(false);
        button.SetVisible(true);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE(activations == 1);
        REQUIRE(button.IsConfigured());
        button.Resize(60, 30);
        button.SetPosition(20, 20);
        REQUIRE((button.GetBounds().w == 60 && button.GetBounds().h == 30));
        dispatch_mouse(LEFT_BUTTON_DOWN, 75, 45, true);
        REQUIRE(button.IsLeftButtonHeld());
        dispatch_mouse(LEFT_BUTTON_UP, 75, 45);
        REQUIRE(!button.IsLeftButtonHeld());
        REQUIRE(activations == 2);
        button.SetPosition(10, 10);
        button.Resize(40, 20);
        button.m_repeat_enabled = true;
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        dispatch_mouse(LEFT_BUTTON_REPEAT, 15, 15, true);
        dispatch_mouse(LEFT_BUTTON_REPEAT, 15, 15, true);
        REQUIRE(activations == 4);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 4));
        button.m_repeat_enabled = false;
        button.m_activation = DialogButtonActivation::Press;
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        REQUIRE(activations == 5);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE(activations == 5);
        button.Reset();
        REQUIRE((!button.IsEnabled() && !button.IsPressed() && button.GetWidth() == 0));
        dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
        dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
        REQUIRE(activations == 5);
    }

    auto closing = std::make_unique<W8DialogButton>();
    closing->ConfigureTextButton("close", font, 0, 0, 10, 10, 40, 20,
        [&](W8DialogButton*) { closing.reset(); }, 0);
    dispatch_mouse(LEFT_BUTTON_DOWN, 15, 15, true);
    dispatch_mouse(LEFT_BUTTON_UP, 15, 15);
    REQUIRE(!closing);
}

TEST_CASE("Modal confirmations and scrollbars dispatch through their native owners", "[owner]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    ExclusiveRegionSet button_scope(ButtonRegionSetIndex());
    const std::filesystem::path root = make_temporary_directory("wiz8-modal-controls");
    const auto assets = root / "assets";
    for (const auto* image : {"DialogBorder.STI", "DialogEdge.STI", "DialogConfirmation.STI"})
        button_image_fixture(assets / "Data" / "Dialogs", image);
    for (const auto* image : {"DialogInlay.STI", "DialogUpArrow.STI", "DialogDownArrow.STI",
                              "DialogSlideBar.STI", "DialogBackground_dark.STI"})
        button_image_fixture(assets / "Data" / "Dialogs", image);
    button_image_fixture(assets, "font.sti", 128, false, 1, 10);
    button_image_fixture(assets, "background.sti");
    button_image_fixture(assets, "arrows.sti", 12, false, 2, 3);
    button_image_fixture(assets, "track.sti", 1, false, 5, 100);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (root / "user").string(), {"", "", ""}});
    PushButtonSoundScheme(0, true);
    std::vector<int> positions;
    ScrollPositionCapture position_capture(positions);
    {
        W8DialogScrollBar bar;
        const W8DialogScrollBar::Resources resources{
            "arrows.sti", "track.sti", 0, RecordScrollPosition};
        REQUIRE(bar.CreateControls(&resources));
        REQUIRE(RegisteredButtonCount() == 0);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        gusMouseYPos = 400;
        dispatch_mouse(LEFT_BUTTON_DOWN, 101, 116, true);
        dispatch_mouse(LEFT_BUTTON_UP, 101, 116);
        REQUIRE(positions == std::vector<int>{17});
        dispatch_mouse(LEFT_BUTTON_DOWN, 101, 21, true);
        dispatch_mouse(LEFT_BUTTON_REPEAT, 101, 21, true);
        dispatch_mouse(LEFT_BUTTON_UP, 101, 21);
        REQUIRE(positions == std::vector<int>{17, 16, 15});
        bar.SetLayout(100, 20, 1, 0, 10, 30);
        dispatch_mouse(LEFT_BUTTON_DOWN, 101, 21, true);
        dispatch_mouse(LEFT_BUTTON_UP, 101, 21);
        REQUIRE(positions.size() == 3);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        dispatch_mouse(LEFT_BUTTON_DOWN, 101, 118, true);
        dispatch_mouse(LEFT_BUTTON_UP, 101, 118);
        REQUIRE(positions.back() == 6);
    }

    auto saved_frame = std::move(GetFrameBufferSurface());
    auto frame = CreateVideoSurface(640, 480, 16);
    REQUIRE(frame);
    GetFrameBufferSurface() = std::move(*frame);
    auto* old_target = std::exchange(ButtonDestBuffer, &GetFrameBufferSurface());
    Font font{SGPVObject{}};
    auto* old_font = std::exchange(g_dialog_interface_font, &font);
    class MessageDialog final : public W8MessageDialogBase {
    public:
        using W8MessageDialogBase::m_confirm_button;
        using W8MessageDialogBase::m_cancel_button;
    };
    for (const bool accept : {false, true}) {
        MessageDialog dialog;
        dialog.SetOrigin(10, 10);
        dialog.SetExtent(100, 80);
        dialog.SetBackground("background.sti", 0);
        dialog.SetMessage("", 0, 0, true, true, false, false, 100, 80);
        REQUIRE(dialog.CreateControls() == 0);
        REQUIRE(RegisteredButtonCount() == 0);
        auto& control = accept ? dialog.m_confirm_button : dialog.m_cancel_button;
        dispatch_mouse(LEFT_BUTTON_DOWN, control.GetX(), control.GetY(), true);
        REQUIRE(dialog.is_open);
        dispatch_mouse(LEFT_BUTTON_UP, control.GetX(), control.GetY());
        REQUIRE(!dialog.is_open);
        REQUIRE(dialog.accepted == accept);
    }

    auto glyphs = LoadVideoObject("font.sti");
    REQUIRE(glyphs);
    font = Font{std::move(*glyphs)};
    {
        W8ListBoxDialog list;
        list.SetOrigin(10, 10);
        list.SetExtent(100, 80);
        list.SetBackground("background.sti", 0);
        for (int index = 0; index < 20; ++index) list.m_lines.emplace_back("X");
        REQUIRE(list.CreateControls() == 0);
        list.Draw();
        REQUIRE(RegisteredButtonCount() == 0);
        gusMouseYPos = 400;
        dispatch_mouse(LEFT_BUTTON_DOWN, list.m_area_button.GetX() + 3,
                       list.m_area_button.GetY() + 21, true);
        dispatch_mouse(LEFT_BUTTON_UP, list.m_area_button.GetX() + 3,
                       list.m_area_button.GetY() + 21);
        REQUIRE(list.m_selected_line == 2);
        dispatch_mouse(LEFT_BUTTON_DOWN, list.m_down_button.GetX(), list.m_down_button.GetY(), true);
        dispatch_mouse(LEFT_BUTTON_REPEAT, list.m_down_button.GetX(), list.m_down_button.GetY(), true);
        dispatch_mouse(LEFT_BUTTON_UP, list.m_down_button.GetX(), list.m_down_button.GetY());
        REQUIRE(list.m_selected_line == 4);
        dispatch_mouse(LEFT_BUTTON_DOWN, list.m_third_text_button.GetX() + 1,
                       list.m_third_text_button.GetY() + 30, true);
        REQUIRE(list.m_selected_line == 15);
        dispatch_mouse(LEFT_BUTTON_REPEAT, list.m_third_text_button.GetX() + 1,
                       list.m_third_text_button.GetY() + 20, true);
        dispatch_mouse(LEFT_BUTTON_UP, list.m_third_text_button.GetX() + 1,
                       list.m_third_text_button.GetY() + 20);
        REQUIRE(list.m_selected_line == 10);
    }
    {
        W8ListBoxDialog short_list;
        short_list.SetOrigin(10, 10);
        short_list.SetExtent(100, 80);
        short_list.SetBackground("background.sti", 0);
        short_list.m_lines.emplace_back("X");
        REQUIRE(short_list.CreateControls() == 0);
        short_list.Draw();
        REQUIRE_FALSE(short_list.m_scrollable);
        for (const auto* button : {&short_list.m_up_button, &short_list.m_down_button,
                                   &short_list.m_slider_button,
                                   &short_list.m_third_text_button}) {
            const auto region = std::find_if(
                g_regions.begin(), g_regions.end(),
                [button](const W8Region& item) { return item.handler == button; });
            REQUIRE(region != g_regions.end());
            REQUIRE((region->flags & W8_REGION_INPUT_DISABLED) != 0);
            REQUIRE_FALSE(button->m_dirty);
        }
    }
    g_dialog_interface_font = old_font;
    ButtonDestBuffer = old_target;
    GetFrameBufferSurface() = std::move(saved_frame);
    REQUIRE(RegisteredButtonCount() == 0);
    g_image_assets.clear();
    std::filesystem::remove_all(root);
}

TEST_CASE("Button regions use SDL click counts", "[owner]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto button_set = ButtonRegionSetIndex();
    Font font{SGPVObject{}};
    auto button = CreateTextButton("clicks", font, 0, 0, {}, 0, 0, 40, 40, BUTTON_TOGGLE,
                                   BUTTON_PRIORITY_NORMAL, click, click);
    REQUIRE(button);
    ExclusiveRegionSet scope(button_set);
    for (const std::uint8_t click_count : {0, 1, 2, 3, 4}) {
        dispatch_mouse(LEFT_BUTTON_DOWN, 10, 10, true, false, click_count);
        REQUIRE((click_reason & BUTTON_REASON_LBUTTON_DWN) != 0);
        REQUIRE(((click_reason & BUTTON_REASON_LBUTTON_DOUBLECLICK) != 0) ==
                (click_count >= 2));
        dispatch_mouse(LEFT_BUTTON_UP, 10, 10);
    }
}

TEST_CASE("assert-plain", "[.death]")
{
    Assert(false);
    FAIL("Assert did not abort");
}

TEST_CASE("assert-message", "[.death]")
{
    Font font{SGPVObject{}};
    CreateTextButton("invalid", font, 0, 0, {}, -1, 0, 40, 20,
                     BUTTON_TOGGLE, BUTTON_PRIORITY_NORMAL, nullptr, nullptr);
    FAIL("CreateTextButton did not abort");
}

TEST_CASE("Image assets share normalized paths and retain resources across clearing", "[owner][image]")
{
    ScopedTemporaryDirectory directory{"wiz8-image-assets"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "Data/Items/X.sti", 1);
    button_image_fixture(assets, "Data/Items/late.sti", 1);

    const auto bitmap_path = assets / "Data" / "Items" / "screen.pcx";
    std::filesystem::create_directories(bitmap_path.parent_path());
    std::vector<std::uint8_t> pcx(128);
    pcx[0] = 10;
    pcx[1] = 5;
    pcx[2] = 1;
    pcx[3] = 8;
    pcx[65] = 1;
    pcx[66] = 1;
    pcx.push_back(1);
    pcx.push_back(12);
    pcx.resize(pcx.size() + 256 * 3);
    {
        std::ofstream file(bitmap_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(pcx.data()),
                   static_cast<std::streamsize>(pcx.size()));
        REQUIRE(file.good());
    }

    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};

    const auto original = g_image_assets.sprites("Data\\Items\\X.sti");
    REQUIRE(original);
    REQUIRE(g_image_assets.sprites("data/items/x.STI") == original);
    REQUIRE_FALSE(g_image_assets.bitmap("Data/Items/X.sti"));
    REQUIRE(g_image_assets.sprites("Data/Items/X.sti") == original);
    REQUIRE_FALSE(g_image_assets.sprites("Data/Items/missing.sti"));
    button_image_fixture(assets, "Data/Items/missing.sti", 1);
    const auto retried = g_image_assets.sprites("Data/Items/missing.sti");
    REQUIRE(retried);

    const auto bitmap = g_image_assets.bitmap("Data\\Items\\screen.pcx");
    REQUIRE(bitmap);
    REQUIRE(g_image_assets.bitmap("data/items/screen.PCX") == bitmap);
    REQUIRE_FALSE(g_image_assets.sprites("Data/Items/screen.pcx"));

    g_image_assets.clear();
    const auto reloaded = g_image_assets.sprites("Data/Items/X.sti");
    const auto bitmap_reloaded = g_image_assets.bitmap("Data/Items/screen.pcx");
    REQUIRE(reloaded);
    REQUIRE(bitmap_reloaded);
    REQUIRE(reloaded != original);
    REQUIRE(bitmap_reloaded != bitmap);
    REQUIRE(original->sprites.size() == 1);
    REQUIRE(bitmap->width() == 1);
}

TEST_CASE("Item image cache owns shared sprite sheets", "[owner][catalog]")
{
    ScopedTemporaryDirectory directory{"wiz8-item-cache"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "Data/Items/icon.sti", 1);
    std::vector<W8ItemDatabaseRecord> records(1005);
    for (auto& record : records)
        record.video_object_name = "icon.sti";
    struct Restore {
        W8ItemDatabaseRecord* records;
        w8_native::Roots roots;
        ~Restore()
        {
            g_item_video_objects.Clear();
            g_image_assets.clear();
            g_item_records = records;
            w8_native::configure_paths(roots);
        }
    } restore{g_item_records, previous_roots};

    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    g_item_records = records.data();
    g_item_video_objects.Initialize(records.size());
    REQUIRE_THROWS_AS(g_item_video_objects.Sprites(-1), std::out_of_range);
    REQUIRE_THROWS_AS(g_item_video_objects.Sprites(records.size()), std::out_of_range);
    records[0].video_object_name = "missing.sti";
    REQUIRE_THROWS_AS(g_item_video_objects.Sprites(0), std::runtime_error);
    REQUIRE(g_item_video_objects.LoadedImageCount() == 0);
    records[0].video_object_name.clear();
    records[0].unidentified_name_index = 145;
    REQUIRE_THROWS_AS(g_item_video_objects.Sprites(0), std::out_of_range);
    records[0].video_object_name = "icon.sti";
    records[0].unidentified_name_index = 0;

    for (std::size_t index = 0; index < records.size(); ++index) {
        auto& sprites = g_item_video_objects.Sprites(index);
        REQUIRE(sprites.sprites.size() == 1);
        REQUIRE(&g_item_video_objects.Sprites(index) == &sprites);
    }
    REQUIRE(g_item_video_objects.LoadedImageCount() == records.size());
    REQUIRE(&g_item_video_objects.Sprites(0) == &g_item_video_objects.Sprites(1));
    auto held = g_image_assets.sprites("Data\\Items\\icon.sti");
    REQUIRE(held);
    g_image_assets.clear();
    REQUIRE(held->sprites.size() == 1);
    REQUIRE(&g_item_video_objects.Sprites(0) == held.get());
    REQUIRE_THROWS_AS(GetCatalogVideoObject(-1, 0), std::out_of_range);
    REQUIRE_THROWS_AS(GetCatalogVideoObject(0, -1), std::out_of_range);
    REQUIRE_THROWS_AS(GetCatalogVideoObject(0x86, std::numeric_limits<int>::max()),
                      std::out_of_range);

    g_item_video_objects.Initialize(records.size());
    REQUIRE(g_item_video_objects.LoadedImageCount() == 0);
    g_image_assets.clear();
    g_item_video_objects.Clear();
    g_item_video_objects.Initialize(records.size());
    auto& reloaded = g_item_video_objects.Sprites(1004);
    REQUIRE(&reloaded != held.get());
    REQUIRE(g_item_video_objects.LoadedImageCount() == 1);
    g_item_video_objects.Clear();
    g_item_video_objects.Clear();
    REQUIRE(g_item_video_objects.LoadedImageCount() == 0);
    REQUIRE_THROWS_AS(g_item_video_objects.Sprites(0), std::out_of_range);
}

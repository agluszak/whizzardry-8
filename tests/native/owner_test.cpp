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
#include "Button System.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/ButtonUserData.h"
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
std::vector<int> screen_leaves;
int screen_entries, screen_frames;
unsigned char enter() { ++screen_entries; return 1; }
unsigned char leave(int discard) { screen_leaves.push_back(discard); return 1; }
void frame() { ++screen_frames; }

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
    SpriteSheet image;
    image.frames.resize(12);
    for (auto& frame : image.frames) {
        frame.usWidth = 16;
        frame.usHeight = 8;
        frame.pixels.assign(16 * 8, SpriteFrame::kTransparent);
    }
    auto object = std::make_unique<SGPVObject>(std::move(image));
    REQUIRE(object);
    const auto saved_slot = g_video_slots[0x86];
    auto saved_frame = std::move(g_video_frames[0]);
    g_video_slots[0x86] = {0, 0};
    g_video_frames[0].storage_kind = W8_VIDEO_STORAGE_OBJECT;
    g_video_frames[0].object = std::move(object);
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
    g_camp_screen = nullptr;
    g_video_slots[0x86] = saved_slot;
    g_video_frames[0] = std::move(saved_frame);
}

int clicks = 0;
int click_reason = 0;
GUI_BUTTON* clicked_button = nullptr;
void click(GUI_BUTTON* button, INT32 reason)
{
    ++clicks;
    click_reason = reason;
    clicked_button = button;
}

uint64_t next_mouse_timestamp = 1;
InputAtom mouse_atom(UINT16 event, unsigned short x, unsigned short y)
{
    InputAtom input{};
    input.uiTimeStamp = next_mouse_timestamp++;
    input.usEvent = event;
    input.uiParam = (static_cast<unsigned int>(y) << 16) | x;
    return input;
}
void dispatch_mouse(UINT16 event, unsigned short x, unsigned short y)
{
    const auto input = mouse_atom(event, x, y);
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

GUIButton nested_outer, nested_inner, removable_button;
void nested_click(GUI_BUTTON* button, INT32 reason)
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
void remove_button_on_callback(GUI_BUTTON*, INT32)
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
        auto first_down = mouse_atom(LEFT_BUTTON_DOWN, 55, 5);
        first_down.uiTimeStamp = 1000;
        DispatchRegionInput(&first_down);
        auto first_up = mouse_atom(LEFT_BUTTON_UP, 55, 5);
        first_up.uiTimeStamp = 1050;
        DispatchRegionInput(&first_up);
        auto second_down = mouse_atom(LEFT_BUTTON_DOWN, 55, 5);
        second_down.uiTimeStamp = 1200;
        DispatchRegionInput(&second_down);
        REQUIRE((click_reason & BUTTON_REASON_LBUTTON_DOUBLECLICK) != 0);
        auto second_up = mouse_atom(LEFT_BUTTON_UP, 55, 5);
        second_up.uiTimeStamp = 1250;
        DispatchRegionInput(&second_up);
        const auto reason_after_up = click_reason;
        auto double_atom = mouse_atom(LEFT_BUTTON_DBL_CLK, 55, 5);
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
    image.frames[0].pixels = {1};
    image.frames[0].usWidth = image.frames[0].usHeight = 1;

    std::array<std::unique_ptr<SGPVObject>, 64> objects{};
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i] = std::make_unique<SGPVObject>(image);
        REQUIRE(objects[i]);
        REQUIRE((objects[i]->sprites.size() == 1 && objects[i]->sprites[0].pixels.size() == 1));
        REQUIRE(objects[i]->sprites[0].pixels.data() != image.frames[0].pixels.data());
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
    image.frames[0].pixels[0] = 2;
    image.frames[0].usWidth = 2;
    REQUIRE((objects[0]->sprites[0].pixels[0] == 1 && objects[0]->sprites[0].usWidth == 1));
    const auto* owned_pixels = image.frames[0].pixels.data();
    SGPVObject moved(std::move(image));
    REQUIRE(moved.sprites[0].pixels.data() == owned_pixels);
    REQUIRE(moved.sprites[0].pixels[0] == 2);
    image = {};
    REQUIRE(image.frames.empty());
    for (unsigned index : {0u, 31u, 63u}) {
        objects[index].reset();
    }
    auto& surviving = *objects[32];
    REQUIRE((surviving.sprites[0].pixels[0] == 1 && surviving.sprites[0].usWidth == 1));
    std::array<std::unique_ptr<SGPVSurface>, 64> surfaces{};
    for (auto& surface : surfaces) {
        surface = CreateVideoSurface(8, 8, 16);
        REQUIRE(surface);
    }
    auto& surviving_surface = *surfaces[32];
    for (unsigned index : {0u, 31u, 63u})
        surfaces[index].reset();
    REQUIRE((surviving_surface.ownedSurface && surviving_surface.usWidth == 8));

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
                           bool app_data = false)
{
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
    header.uiOriginalSize = count;
    header.uiStoredSize = count * 3;
    header.fFlags = STCI_INDEXED | STCI_ETRLE_COMPRESSED;
    header.usWidth = header.usHeight = 1;
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
        regions[index].uiDataOffset = index * 3;
        regions[index].uiDataLength = 3;
        regions[index].usWidth = regions[index].usHeight = 1;
    }
    write(regions.data(), regions.size() * sizeof(STCISubImage));
    for (unsigned index = 0; index < count; ++index) {
        const UINT8 pixels[]{1, 1, 0};
        write(pixels, sizeof(pixels));
    }
    if (app_data) {
        const UINT8 bytes[]{7, 8, 9, 10};
        write(bytes, sizeof(bytes));
    }
}

void generic_button_images()
{
    const std::filesystem::path root = make_temporary_directory("wiz8-button-images");
    const auto assets = root / "assets";
    button_image_fixture(assets, DEFAULT_GENERIC_BUTTON_OFF);
    button_image_fixture(assets, "button.sti");
    button_image_fixture(assets, "short.sti", 8);
    w8_native::configure_paths({assets.string(), (root / "user").string(), {"", "", ""}});

    button_image_fixture(assets, "metadata.sti", 3, true);
    {
        auto image = LoadImage("metadata.sti");
        REQUIRE(image);
        const auto* sheet = std::get_if<SpriteSheet>(&*image);
        REQUIRE(sheet);
        REQUIRE(sheet->frames.size() == 3);
        REQUIRE(sheet->frames[2].pixels == std::vector<UINT16>{1});
        REQUIRE(sheet->palette[1].peRed == 255);

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
        REQUIRE((sprite->sprites[2].usWidth == 1 && sprite->sprites[2].pixels[0] == 1));
        REQUIRE(sprite->pPaletteEntry[1].peRed == 255);
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
        sprite.sprites[frame].pixels[0] = frame + 1;
        sprite.palette[frame + 1] = 0x1000 + frame;
    }
    auto target_owner = CreateVideoSurface(8, 8, 16);
    REQUIRE(target_owner);
    auto& target = *target_owner;
    const auto old_target = ButtonDestBuffer;
    const auto old_hilights = gfRenderHilights;
    const auto old_left_down = gfLeftButtonState;
    const auto old_hover_region = g_hover_region_index;
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
                        DrawButton(first_button);
                        auto* pixels = static_cast<UINT16*>(target.cpu().surface->pixels);
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
    target_owner.reset();
    graphics.reset();
    ShutdownButtonImageManager();
    REQUIRE(!lifetime.expired());
    first_button.reset();
    REQUIRE(second_button->quick_image->object().sprites[4].pixels[0] == 5);
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
    {
        auto dialog = std::make_unique<W8DialogButton>();
        REQUIRE(dialog->ConfigureIcon("button.sti", 0, 1, BUTTON_PRIORITY_NORMAL,
                                      W8_DIALOG_BUTTON_NO_TOOLTIP));
        dialog->SetPosition(12, 13);
        REQUIRE(!dialog->ConfigureIcon("missing.sti", 0, 1, BUTTON_PRIORITY_NORMAL,
                                       W8_DIALOG_BUTTON_NO_TOOLTIP));
        REQUIRE((dialog->GetX() == 12 && dialog->GetY() == 13 && dialog->GetWidth() == 1));
        REQUIRE(RegisteredButtonCount() == 1);
    }
    REQUIRE(RegisteredButtonCount() == 0);
    // A dialog's click callback can close it. The dispatcher must not read
    // the released dialog while completing the click (including its sound).
    auto* closing_dialog = new W8DialogButton;
    REQUIRE(closing_dialog->Configure("button.sti", 0, 1, 2, 3, 4,
        [](W8DialogButton* dialog) { delete dialog; }, nullptr, false,
        BUTTON_PRIORITY_NORMAL, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr));
    auto dispatch_button = std::make_shared<GUI_BUTTON>();
    SetButtonUserDataPointer(dispatch_button, closing_dialog);
    dispatch_button->uiFlags = BUTTON_CLICKED_ON;
    PushButtonSoundScheme(0, true);
    DialogButtonCallback(dispatch_button.get(), BUTTON_REASON_LBUTTON_UP);
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
    ConfigureFontRendering(std::vector<UINT16>{'?', 0x00e9, 0x4e2d, 'e', 0x0301, 'a', 'b', ' ', 'c'});
    Font primary;
    REQUIRE(primary.load(filename));
    REQUIRE(primary.textWidth("é") > 0);
    REQUIRE(primary.codepointPrefixWidth("é中", 1) == primary.textWidth("é"));
    REQUIRE(primary.codepointPrefixWidth("é中", 2) == primary.textWidth("é中"));
    // Render borrowed substrings without depending on a trailing NUL or
    // accidentally consuming the next glyph in the backing string.
    SetFont(primary);
    g_font_render_state.clip = {0, 0, 64, 32};
    std::vector<UINT16> expected_pixels(64 * 32), view_pixels(64 * 32);
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(expected_pixels.data()), 64 * 2, primary, 0, 0, "é");
    const std::string backing = "é中";
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(view_pixels.data()), 64 * 2, primary, 0, 0,
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
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(expected_pixels.data()), 128, secondary, 0, 0, "é中");
    SetFont(primary);
    DrawFontTextToBuffer(reinterpret_cast<UINT8*>(view_pixels.data()), 128, secondary, 0, 0, "é中");
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

TEST_CASE("Item image cache grows, reloads and rolls back failed loads", "[owner][catalog]")
{
    ScopedTemporaryDirectory directory{"wiz8-item-cache"};
    const auto assets = directory.path / "assets";
    button_image_fixture(assets, "Data/Items/icon.sti", 1);
    button_image_fixture(assets, "Data/Items/Dagger.sti", 1);
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    const auto slot_count = g_video_slots.size();
    const auto frame_count = g_video_frames.size();
    const auto fixed_tail = g_video_slots.back();
    std::vector<W8ItemDatabaseRecord> records(1005);
    for (auto& record : records)
        record.video_object_name = "icon.sti";
    struct Restore {
        W8ItemDatabaseRecord* previous = g_item_records;
        ~Restore() { g_item_video_objects.Clear(); g_item_records = previous; }
    } restore;
    g_item_records = records.data();
    g_item_video_objects.Initialize(records.size());
    REQUIRE_THROWS_AS(g_item_video_objects.GetOrCreateVideoObject(-1), std::out_of_range);
    REQUIRE_THROWS_AS(g_item_video_objects.GetOrCreateVideoObject(records.size()), std::out_of_range);
    records[0].video_object_name = "missing.sti";
    REQUIRE_THROWS_AS(g_item_video_objects.GetOrCreateVideoObject(0), std::runtime_error);
    REQUIRE(g_item_video_objects.LoadedImageCount() == 0);
    REQUIRE(g_video_slots.size() == slot_count);
    REQUIRE(g_video_frames.size() == frame_count);
    records[0].video_object_name.clear(); // Checked fallback table.
    records[0].unidentified_name_index = 145;
    REQUIRE_THROWS_AS(g_item_video_objects.GetOrCreateVideoObject(0), std::out_of_range);
    records[0].unidentified_name_index = 0;
    for (unsigned i = 0; i < records.size(); ++i) {
        const auto id = g_item_video_objects.GetOrCreateVideoObject(i);
        REQUIRE(id == slot_count + i);
        REQUIRE(GetCatalogVideoObject(id, 0).sprites.size() == 1);
        REQUIRE(g_item_video_objects.GetOrCreateVideoObject(i) == id);
    }
    REQUIRE(g_item_video_objects.LoadedImageCount() == records.size());
    REQUIRE(g_video_slots.size() == slot_count + records.size());
    REQUIRE(g_video_frames.size() == frame_count + records.size());
    REQUIRE(g_video_slots.at(slot_count - 1).first_frame == fixed_tail.first_frame);
    REQUIRE_THROWS_AS(GetCatalogVideoObject(slot_count, -1), std::out_of_range);
    REQUIRE_THROWS_AS(GetCatalogVideoObject(slot_count, std::numeric_limits<int>::max()),
                      std::out_of_range);
    ReleaseLoadedVideoFrames();
    REQUIRE_FALSE(g_video_frames.at(frame_count).object);
    REQUIRE(g_item_video_objects.GetOrCreateVideoObject(0) == slot_count);
    REQUIRE(g_video_frames.at(frame_count).object);
    // Reinitialization must release the prior images and remove their IDs.
    g_item_video_objects.Initialize(records.size());
    REQUIRE(g_item_video_objects.LoadedImageCount() == 0);
    REQUIRE(g_video_slots.size() == slot_count);
    REQUIRE(g_video_frames.size() == frame_count);
    REQUIRE(g_item_video_objects.GetOrCreateVideoObject(1004) == slot_count);
    g_item_video_objects.Clear();
    g_item_video_objects.Clear();
    REQUIRE(g_video_slots.size() == slot_count);
    REQUIRE(g_video_frames.size() == frame_count);
    REQUIRE_THROWS_AS(GetCatalogVideoObject(slot_count, 0), std::out_of_range);
}

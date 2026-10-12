#include "wiz8/dialog_code/DialogBase.h"
#include <deque>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include "image.h"
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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/xstatus.h"
#include "wiz8/catalog_artwork.h"
#include "image_assets.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/utility.h"
#include "wiz8/compat/compiler.h"
#include "wiz8/sr_api.h"
#include "wiz8/engine_code/Video2.h"
#include "Font.h"
#include "input.h"
#include "wiz8/regions.h"
#include "wiz8/ui_layer.h"
#include "imgfmt.h"
#include "temporary_directory.h"
#include "scoped_asset_roots.h"
#include "wiz8/asset_paths.h"
#include "wiz8/text_input.h"
#include "Types.h"
#include "application.h"
#include "himage.h"
#include "wiz8/surface_storage.h"
#include "layouts/main_game_screen.h"
#include "layouts/screen_state.h"
#include "local_code/RangeControl.h"
#include "local_screens/RCSStatsPage.h"


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

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count = 9,
                          bool app_data = false, unsigned width = 1, unsigned height = 1,
                          std::array<std::uint8_t, 5> frame_palette_indices = {},
                          short offset_x = 0, short offset_y = 0);

void initialize_test_framebuffer()
{
    ResetFontRendering();
    g_pixel_format = 9;
    Initialize16BitPixelFormatMasks();
    REQUIRE(InitializePrimarySurface());
}

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
    button_image_fixture(assets, "Data/Main Interface/main_scroll.sti", 12, false, 16, 8);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            g_camp_screen.reset();
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};
    {
        g_camp_screen = std::make_unique<W8CampScreenState>();
        auto* state = g_camp_screen.get();
        REQUIRE((state->effect_list.empty() && state->stats_range == nullptr));
        state->stats_range = std::make_unique<W8RangeControl>(0x264, 0xbe, 0x276, 0x1b5);
        auto& range = *state->stats_range;
        state->effect_list = {
            {false, false, true, false, 0, 0, 0, 3, 10}, {false, false, false, true, 0, 1, 0, 3, 9},
            {true, false, true, false, 2, 2, 0, 9999, 3}, {false, false, true, true, 0, 3, 0, 3, 2}};
        state->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        state->effect_scroll = 100;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 24 && state->effect_scroll == 7));
        REQUIRE((state->effect_first_visible == 0 && state->effect_last_visible == 3));
        REQUIRE((range.m_enabled && range.m_value == 7));
        state->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        FilterCampEffectList();
        REQUIRE((state->effect_visible_lines == 14 && state->effect_scroll == 0));
        REQUIRE((!range.m_enabled && !state->effect_list[1].visible));
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
        REQUIRE((state->effect_visible_lines == 64 && range.m_enabled));
        character = {};
        RebuildCampEffectList();
        REQUIRE((state->effect_list.empty() && state->effect_visible_lines == 0));
        REQUIRE((state->effect_beneficial_count == 0 && !range.m_enabled));
        g_review_character = nullptr;
    }
}

std::uint64_t next_mouse_timestamp = 1;
InputEvent mouse_atom(InputEventType event, std::uint16_t x, std::uint16_t y, bool left = false,
                     bool right = false, std::uint8_t click_count = 0)
{
    InputEvent input{};
    input.timestamp = next_mouse_timestamp++;
    input.type = event;
    input.position = {x, y};
    input.buttons = (left ? SDL_BUTTON_LMASK : 0) | (right ? SDL_BUTTON_RMASK : 0);
    input.clicks = click_count;
    return input;
}
void dispatch_mouse(InputEventType event, std::uint16_t x, std::uint16_t y, bool left = false,
                    bool right = false, std::uint8_t click_count = 0)
{
    const auto input = mouse_atom(event, x, y, left, right, click_count);
    if (event == InputEventType::MouseMove) UpdateRegionMousePosition(x, y);
    DispatchRegionInput(&input);
}

unsigned int new_test_region_set()
{
    const auto set = CreateRegionSet();
    g_region_sets[set].enabled = 1;
    return set;
}

struct RegionStateSnapshot {
    std::vector<unsigned int> set_enabled;
    std::vector<unsigned int> region_flags;
    unsigned int current = g_ui.current;
    unsigned int captured = g_ui.captured;
    unsigned int hover = g_ui.hover;
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
                g_region_sets[index].enabled = 0;
            }
        }
        for (std::size_t index = 0;
             index < region_flags.size() && index < g_regions.size(); ++index) {
            g_regions[index].flags = region_flags[index];
        }
        g_ui.current = current;
        g_ui.captured = captured;
        g_ui.hover = hover;
        g_region_help_delay = help_delay;
        g_region_help_force_enabled = help_force_enabled;
    }
};

std::deque<RegionFunctionHandler> test_region_handlers;

unsigned int add_test_region(unsigned int set, unsigned short id, int x1, int y1, int x2, int y2,
                             int priority, W8RegionCallback callback)
{
    const auto index = AddRegionToSet(set, priority);
    g_regions[index].flags = W8_REGION_RECTANGLE;
    g_regions[index].SetBounds(static_cast<unsigned short>(x1), static_cast<unsigned short>(y1),
                    static_cast<unsigned short>(x2), static_cast<unsigned short>(y2));
    g_regions[index].handler = &test_region_handlers.emplace_back(callback, id);
    ++g_regions[index].binding_generation;
    return index;
}

std::vector<std::pair<int, int>> region_events;
struct TestOwnedRegionHandler final : W8RegionHandler {
    std::vector<InputEventType> events;
    std::uint8_t clicks = 0;

    int HandleRegionInput(W8Region&, const InputEvent& input) override
    {
        events.push_back(input.type);
        clicks = input.clicks;
        return 1;
    }
};

unsigned char record_region_event(const InputEvent* event, W8Region* region, unsigned short id)
{
    region_events.emplace_back(id, static_cast<int>(event->type));
    return 1;
}
unsigned char hover_region(const InputEvent*, W8Region* region, unsigned short id)
{
    if (region->flags & W8_REGION_MOUSE_LEAVE)
        region_events.emplace_back(id, W8_REGION_MOUSE_LEAVE);
    else if (region->flags & W8_REGION_MOUSE_ENTER)
        region_events.emplace_back(id, W8_REGION_MOUSE_ENTER);
    else
        region_events.emplace_back(id, 0);
    return 1;
}

unsigned int nested_region_set = 0;
unsigned int nested_region_index = 0;
bool remove_nested_region = false;
std::vector<InputEventType> nested_region_events;
unsigned char nested_region_callback(const InputEvent* event, W8Region*, unsigned short)
{
    if (event->type == InputEventType::LeftButtonDown) {
        nested_region_events.push_back(InputEventType::LeftButtonDown);
        g_ui.captured = nested_region_index;
        const auto up = mouse_atom(InputEventType::LeftButtonUp, 100, 100);
        DispatchRegionInput(&up);
    } else if (event->type == InputEventType::LeftButtonUp) {
        nested_region_events.push_back(InputEventType::LeftButtonUp);
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
        dispatch_mouse(InputEventType::LeftButtonDown, 10, 10);
    }
    REQUIRE((nested_region_events == std::vector<InputEventType>{InputEventType::LeftButtonDown, InputEventType::LeftButtonUp}));
    REQUIRE(g_ui.captured == 0);

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
    REQUIRE((region_events[0] == std::pair<int, int>{1, W8_REGION_MOUSE_LEAVE}));
    REQUIRE((region_events[1] == std::pair<int, int>{2, W8_REGION_MOUSE_ENTER}));
}
void owned_region_captures_across_frames()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = ControlRegionSetIndex();
    TestOwnedRegionHandler handler;
    OwnedRegion region(set, REGION_PRIORITY_NORMAL, handler);
    region.region().flags = W8_REGION_RECTANGLE;
    region.region().SetBounds(10, 10, 30, 30);
    ExclusiveRegionSet scope(set);

    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(g_ui.captured == region.index());
    UpdateRegionMousePosition(100, 100);
    REQUIRE(g_ui.captured == region.index());
    dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);

    REQUIRE(g_ui.captured == 0);
    REQUIRE(std::find(handler.events.begin(), handler.events.end(),
                      InputEventType::LeftButtonDown) != handler.events.end());
    REQUIRE(std::find(handler.events.begin(), handler.events.end(),
                      InputEventType::LeftButtonUp) != handler.events.end());
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

    REQUIRE(g_ui.hover == region);
    REQUIRE(std::count_if(region_events.begin(), region_events.end(), [](const auto& event) {
                return event.second == W8_REGION_MOUSE_ENTER;
            }) == 1);
}

void modal_scope_preserves_capture_across_frames()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto button_set = ControlRegionSetIndex();
    TestOwnedRegionHandler handler;
    OwnedRegion region(button_set, REGION_PRIORITY_NORMAL, handler);
    region.region().flags = W8_REGION_RECTANGLE;
    region.region().SetBounds(10, 10, 30, 30);

    std::optional<ExclusiveRegionSet> scope;
    scope.emplace(button_set);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15);
    REQUIRE(g_ui.captured == region.index());
    UpdateRegionMousePosition(100, 100);
    REQUIRE(g_ui.captured == region.index());
    dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);

    REQUIRE(std::find(handler.events.begin(), handler.events.end(),
                      InputEventType::LeftButtonUp) != handler.events.end());
    REQUIRE(g_ui.captured == 0);
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
    REQUIRE(g_ui.hover == outer_region);
    inner_scope.emplace(inner_set);
    outer_scope.reset();
    UpdateRegionMousePosition(25, 5);
    REQUIRE(g_ui.hover == inner_region);
    inner_scope.reset();
    REQUIRE(g_ui.hover == 0);
    UpdateRegionMousePosition(45, 5);
    REQUIRE(g_ui.hover == fallback_region);
}

void reset_regions_preserves_hidden_buttons()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto button_set = ControlRegionSetIndex();
    TestOwnedRegionHandler handler;
    OwnedRegion region(button_set, REGION_PRIORITY_NORMAL, handler);
    region.region().flags = W8_REGION_RECTANGLE;
    region.region().SetBounds(70, 70, 90, 90);
    const auto region_index = region.index();
    g_regions[region_index].SetInputEnabled(false);
    REQUIRE((g_regions[region_index].flags & W8_REGION_INPUT_DISABLED) != 0);

    ResetRegions();

    REQUIRE(g_region_sets[button_set].enabled == 1);
    REQUIRE((g_regions[region_index].flags & W8_REGION_INPUT_DISABLED) != 0);
    UpdateRegionMousePosition(75, 75);
    REQUIRE(g_ui.hover != region_index);
}

void later_region_set_precedes_button_set()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto button_set = ControlRegionSetIndex();
    TestOwnedRegionHandler handler;
    OwnedRegion control(button_set, REGION_PRIORITY_NORMAL, handler);
    control.region().flags = W8_REGION_RECTANGLE;
    control.region().SetBounds(100, 100, 120, 120);
    const auto screen_set = new_test_region_set();
    const auto screen_region =
        add_test_region(screen_set, 7, 100, 100, 120, 120, 0, record_region_event);
    REQUIRE(screen_set > button_set);
    region_events.clear();

    UpdateRegionMousePosition(105, 105);
    REQUIRE(g_ui.hover == screen_region);
    dispatch_mouse(InputEventType::LeftButtonDown, 105, 105);

    REQUIRE(std::find(region_events.begin(), region_events.end(),
                      std::pair<int, int>{7, static_cast<int>(InputEventType::LeftButtonDown)}) != region_events.end());
    REQUIRE(handler.events.empty());
}

void owned_region_move_and_teardown()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = new_test_region_set();
    TestOwnedRegionHandler handler;
    ExclusiveRegionSet scope(set);

    OwnedRegion owned(set, 0, handler);
    owned.region().flags = W8_REGION_RECTANGLE;
    owned.region().SetBounds(10, 10, 30, 30);
    const auto index = owned.index();
    UpdateRegionMousePosition(15, 15);
    REQUIRE(g_ui.hover == index);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(g_ui.captured == index);

    {
        OwnedRegion moved(std::move(owned));
        REQUIRE_FALSE(owned);
        owned.reset();
        REQUIRE(g_regions[index].handler == &handler);
        REQUIRE(g_ui.captured == index);
        REQUIRE(g_ui.hover == index);
    }
    REQUIRE(g_regions[index].handler == nullptr);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(g_ui.hover == 0);
}
void owned_region_set_templates_and_teardown()
{
    constexpr int left_button_down = static_cast<int>(InputEventType::LeftButtonDown);
    RegionStateSnapshot saved_state;
    ResetRegions();

    constexpr std::array<RegionTemplate, 3> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 11, true, 101},
        {W8_REGION_RECTANGLE, 40, 10, 60, 30, record_region_event, 12, false, -1},
        {W8_REGION_RECTANGLE, 70, 10, 90, 30, record_region_event, 13, true, 103},
    }};

    const auto baseline_region_count = LiveDynamicRegionCount();
    unsigned int released_set_index;
    {
        std::optional<OwnedRegionSet> regions;
        regions.emplace(templates);
        released_set_index = regions->set();
        REQUIRE(regions->size() == templates.size());
        REQUIRE(g_region_sets[regions->set()].Regions().size() == templates.size());

        for (std::size_t index = 0; index < templates.size(); ++index) {
            const auto& region = (*regions)[index];
            const auto& entry = templates[index];
            REQUIRE(region.flags == entry.flags);
            REQUIRE(region.x1 == entry.x1);
            REQUIRE(region.y1 == entry.y1);
            REQUIRE(region.x2 == entry.x2);
            REQUIRE(region.y2 == entry.y2);
            REQUIRE((region.handler != nullptr) == (entry.callback != nullptr));
            REQUIRE(region.help_enabled == entry.help_enabled);
            REQUIRE(region.help_text_id == entry.help_text_id);
            REQUIRE(regions->index(index) < g_regions.size());
        }

        std::optional<ExclusiveRegionSet> scope;
        scope.emplace(regions->set());
        regions->Enable();
        region_events.clear();
        dispatch_mouse(InputEventType::LeftButtonDown, 45, 15, true);
        REQUIRE(g_ui.captured == regions->index(1));
        REQUIRE(std::find(region_events.begin(), region_events.end(),
                          std::pair<int, int>{12, left_button_down}) != region_events.end());
        dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);

        regions->Disable();
        region_events.clear();
        dispatch_mouse(InputEventType::LeftButtonDown, 45, 15, true);
        REQUIRE(region_events.empty());

        regions->Enable();
        UpdateRegionMousePosition(45, 15);
        REQUIRE(g_ui.hover == regions->index(1));
        dispatch_mouse(InputEventType::LeftButtonDown, 45, 15, true);
        REQUIRE(g_ui.captured == regions->index(1));
        regions.reset();
        REQUIRE(g_ui.hover == 0);
        REQUIRE(g_ui.captured == 0);
        scope.reset();
    }

    REQUIRE(LiveDynamicRegionCount() == baseline_region_count);
    {
        OwnedRegionSet reused_set(templates);
        REQUIRE(reused_set.set() == released_set_index);
    }
    REQUIRE(LiveDynamicRegionCount() == baseline_region_count);
}

void region_set_views_share_owned_regions()
{
    constexpr int left_button_down = static_cast<int>(InputEventType::LeftButtonDown);
    RegionStateSnapshot saved_state;
    ResetRegions();

    constexpr std::array<RegionTemplate, 3> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 21, false, -1},
        {W8_REGION_RECTANGLE, 40, 10, 60, 30, record_region_event, 22, false, -1},
        {W8_REGION_RECTANGLE, 70, 10, 90, 30, record_region_event, 23, false, -1},
    }};

    const auto baseline_region_count = LiveDynamicRegionCount();
    unsigned int released_view_set;
    {
        OwnedRegionSet source(templates);
        source.Disable();
        const auto source_index = source.index(0);
        const auto baseline_with_source = LiveDynamicRegionCount();
        {
            RegionSetView view(source, 0, 2);
            released_view_set = view.set();
            REQUIRE(g_region_sets[view.set()].Regions().size() == 2);
            REQUIRE(g_region_sets[view.set()].Regions()[0] == source.index(0));
            REQUIRE(g_region_sets[view.set()].Regions()[1] == source.index(1));

            view.Enable();
            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
            REQUIRE(g_ui.captured == source.index(0));
            REQUIRE(std::find(region_events.begin(), region_events.end(),
                              std::pair<int, int>{21, left_button_down}) != region_events.end());
            dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);

            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 45, 15, true);
            REQUIRE(g_ui.captured == source.index(1));
            REQUIRE(std::find(region_events.begin(), region_events.end(),
                              std::pair<int, int>{22, left_button_down}) != region_events.end());
            dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);

            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 75, 15, true);
            REQUIRE(std::none_of(region_events.begin(), region_events.end(),
                                 [](const auto& event) { return event.first == 23; }));
        }

        {
            RegionSetView view(source, 1, 2);
            REQUIRE(g_region_sets[view.set()].Regions().size() == 2);
            REQUIRE(g_region_sets[view.set()].Regions()[0] == source.index(1));
            REQUIRE(g_region_sets[view.set()].Regions()[1] == source.index(2));

            view.Enable();
            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 45, 15, true);
            REQUIRE(std::find(region_events.begin(), region_events.end(),
                              std::pair<int, int>{22, left_button_down}) != region_events.end());
            dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);

            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 75, 15, true);
            REQUIRE(std::find(region_events.begin(), region_events.end(),
                              std::pair<int, int>{23, left_button_down}) != region_events.end());
            dispatch_mouse(InputEventType::LeftButtonUp, 75, 15);

            region_events.clear();
            dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
            REQUIRE(std::none_of(region_events.begin(), region_events.end(),
                                 [](const auto& event) { return event.first == 21; }));
        }
        REQUIRE(LiveDynamicRegionCount() == baseline_with_source);

        REQUIRE(source.index(0) == source_index);
        REQUIRE(source[0].handler != nullptr);
        REQUIRE(LiveDynamicRegionCount() == baseline_with_source);

        source.Enable();
        region_events.clear();
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        REQUIRE(std::find(region_events.begin(), region_events.end(),
                          std::pair<int, int>{21, left_button_down}) != region_events.end());
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);

        {
            RegionSetView reused_view(source, 0, 2);
            REQUIRE(reused_view.set() == released_view_set);
        }
        REQUIRE(LiveDynamicRegionCount() == baseline_with_source);
    }

    REQUIRE(LiveDynamicRegionCount() == baseline_region_count);
}

void screen_region_set_lifecycle()
{
    RegionStateSnapshot saved_state;
    ResetRegions();

    constexpr std::array<RegionTemplate, 1> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 31, false, -1},
    }};

    const auto baseline_region_count = LiveDynamicRegionCount();
    ScreenRegionSet regions{templates};
    regions.Enable();
    regions.Disable();
    regions.EnableInput();
    regions.DisableInput();
    REQUIRE_FALSE(regions);

    regions.Create();
    REQUIRE(regions);
    const auto set = regions->set();
    const auto region_index = regions->index(0);
    REQUIRE(LiveDynamicRegionCount() == baseline_region_count + 1);

    regions.Create();
    REQUIRE(regions->set() == set);
    REQUIRE(regions->index(0) == region_index);

    regions.Disable();
    REQUIRE_FALSE(regions->IsEnabled());
    regions.Enable();
    REQUIRE(regions->IsEnabled());
    regions.Destroy();
    REQUIRE_FALSE(regions);
    REQUIRE(LiveDynamicRegionCount() == baseline_region_count);
}

void widget_owned_regions_release_with_widget()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto baseline_region_count = LiveDynamicRegionCount();
    const auto baseline_set_count = LiveRegionSetCount();
    {
        Controls panel;
        panel.CreateRegionSet();
        auto* widget = panel.EmplaceControl<W8Widget>();
        REQUIRE(widget->m_pPanel == &panel);
        REQUIRE(LiveDynamicRegionCount() == baseline_region_count + 1);
        REQUIRE(LiveRegionSetCount() == baseline_set_count + 1);
    }
    REQUIRE(LiveDynamicRegionCount() == baseline_region_count);
    REQUIRE(LiveRegionSetCount() == baseline_set_count);
}

void input_sink_capture_can_be_released()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    CaptureInputSink();
    const auto sink_index = g_ui.captured;
    REQUIRE(sink_index != 0);
    ReleaseInputSinkCapture();
    REQUIRE(g_ui.captured == 0);

    // The released sink must not keep swallowing clicks at its zero-sized origin.
    constexpr std::array<RegionTemplate, 1> origin_templates{{
        {W8_REGION_RECTANGLE, 0, 0, 5, 5, record_region_event, 42, false, -1},
    }};
    OwnedRegionSet origin{origin_templates};
    origin.Enable();
    REQUIRE(FindRegionAtPoint(0, 0) == origin.index(0));

    constexpr std::array<RegionTemplate, 1> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 41, false, -1},
    }};
    OwnedRegionSet other{templates};
    other.Enable();
    ActivateDialogRegion(other.index(0));
    REQUIRE(g_ui.captured == other.index(0));
    ReleaseInputSinkCapture();
    REQUIRE(g_ui.captured == other.index(0));
    ClearActiveRegionIfMatches(other.index(0));
}

void region_set_order_beats_creation_order()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    constexpr std::array<RegionTemplate, 1> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 51, false, -1},
    }};

    ScreenRegionSet later{templates, 0x20};
    ScreenRegionSet earlier{templates, 0x10};
    later.Create();
    earlier.Create();
    later.Enable();
    earlier.Enable();
    REQUIRE(FindRegionAtPoint(20, 20) == earlier->index(0));

    // Dynamic sets rank behind every retail-ordered set.
    earlier.Destroy();
    auto dynamic_first = std::make_unique<OwnedRegionSet>(templates);
    dynamic_first->Enable();
    REQUIRE(FindRegionAtPoint(20, 20) == later->index(0));
    later.Destroy();
    REQUIRE(FindRegionAtPoint(20, 20) == dynamic_first->index(0));

    // Dynamic sets keep creation order even when a released slot is reused.
    OwnedRegionSet dynamic_second{templates};
    dynamic_second.Enable();
    REQUIRE(FindRegionAtPoint(20, 20) == dynamic_first->index(0));
    const auto recycled_set = dynamic_first->set();
    dynamic_first.reset();
    OwnedRegionSet dynamic_third{templates};
    dynamic_third.Enable();
    REQUIRE(dynamic_third.set() == recycled_set);
    REQUIRE(FindRegionAtPoint(20, 20) == dynamic_second.index(0));
}

void stale_exclusive_scope_blocks_reused_set_slot()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    constexpr std::array<RegionTemplate, 1> templates{{
        {W8_REGION_RECTANGLE, 10, 10, 30, 30, record_region_event, 52, false, -1},
    }};
    auto modal = std::make_unique<OwnedRegionSet>(templates);
    modal->Enable();
    ExclusiveRegionSet scope(modal->set());
    REQUIRE(FindRegionAtPoint(20, 20) == modal->index(0));

    const auto modal_set = modal->set();
    modal.reset();
    OwnedRegionSet replacement{templates};
    replacement.Enable();
    REQUIRE(replacement.set() == modal_set);
    REQUIRE(FindRegionAtPoint(20, 20) == 0);
}

void region_registration_and_callback_lifetimes()
{
    const auto set = new_test_region_set();
    const auto first = add_test_region(set, 1, 0, 0, 40, 40, 0, record_region_event);
    const auto latest = add_test_region(set, 2, 0, 0, 40, 40, 0, record_region_event);
    {
        ExclusiveRegionSet scope(set);
        region_events.clear();
        dispatch_mouse(InputEventType::LeftButtonDown, 10, 10);
    }
    REQUIRE(region_events.front().first == 1);
    RemoveRegionFromSet(set, first);
    region_events.clear();
    {
        ExclusiveRegionSet scope(set);
        dispatch_mouse(InputEventType::LeftButtonDown, 10, 10);
    }
    REQUIRE(region_events.front().first == 2);
    RemoveRegionFromSet(set, latest);
}
void owned_regions_honor_priority()
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = ControlRegionSetIndex();
    TestOwnedRegionHandler low_handler;
    TestOwnedRegionHandler high_handler;
    OwnedRegion low(set, REGION_PRIORITY_LOW, low_handler);
    OwnedRegion high(set, REGION_PRIORITY_HIGH, high_handler);
    low.region().flags = high.region().flags = W8_REGION_RECTANGLE;
    low.region().SetBounds(0, 0, 40, 40);
    high.region().SetBounds(0, 0, 20, 20);

    ExclusiveRegionSet scope(set);
    dispatch_mouse(InputEventType::MouseMove, 10, 10);
    REQUIRE(g_ui.hover == high.index());
    dispatch_mouse(InputEventType::LeftButtonDown, 10, 10, true);
    REQUIRE(g_ui.captured == high.index());
    REQUIRE(high_handler.events.back() == InputEventType::LeftButtonDown);
    REQUIRE(low_handler.events.empty());
    dispatch_mouse(InputEventType::LeftButtonUp, 10, 10);
    REQUIRE(g_ui.captured == 0);
}

void image_and_sprite_ownership()
{
    SpriteSheet image;
    image.palette[1] = {255, 0, 0, 0};
    image.colors = Create16BPPPalette(image.palette);
    image.frames.resize(1);
    image.frames[0].indices = {1};
    image.frames[0].width = image.frames[0].height = 1;

    std::array<std::unique_ptr<SpriteSheet>, 64> objects{};
    for (unsigned i = 0; i < objects.size(); ++i) {
        objects[i] = std::make_unique<SpriteSheet>(image);
        REQUIRE(objects[i]);
        REQUIRE((objects[i]->frames.size() == 1 && objects[i]->frames[0].indices.size() == 1));
        REQUIRE(objects[i]->frames[0].indices.data() != image.frames[0].indices.data());
        REQUIRE(objects[i]->palette.data() != image.palette.data());
        REQUIRE(objects[i]->colors.data() != image.colors.data());
    }
    image.frames[0].indices[0] = 2;
    image.frames[0].width = 2;
    REQUIRE((objects[0]->frames[0].paletteIndexAt(0, 0) == 1 &&
             objects[0]->frames[0].width == 1));
    const auto* owned_pixels = image.frames[0].indices.data();
    SpriteSheet moved(std::move(image));
    REQUIRE(moved.frames[0].indices.data() == owned_pixels);
    REQUIRE(moved.frames[0].paletteIndexAt(0, 0) == 2);
    image = {};
    REQUIRE(image.frames.empty());
    for (unsigned index : {0u, 31u, 63u}) {
        objects[index].reset();
    }
    auto& surviving = *objects[32];
    REQUIRE((surviving.frames[0].paletteIndexAt(0, 0) == 1 &&
             surviving.frames[0].width == 1));
    std::vector<SDLSurfaceOwner> surfaces;
    surfaces.reserve(64);
    for (unsigned index = 0; index < 64; ++index) {
        surfaces.push_back(CreateCpuStorage(8, 8, SDL_PIXELFORMAT_RGB565));
        REQUIRE(surfaces.back());
    }
    auto& surviving_surface = *surfaces[32];
    for (unsigned index : {0u, 31u, 63u})
        surfaces[index].reset();
    REQUIRE(surviving_surface.w == 8);

}

void font_resource_ownership()
{
    initialize_test_framebuffer();
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
    Font font{};
    for (unsigned repeat = 0; repeat < 32; ++repeat) {
        TextFieldGroup outer(font);
        const auto first = outer.Add(0, 0, 80, 20, REGION_PRIORITY_NORMAL, "alpha", 32, 0, false);
        const auto middle = outer.Add(0, 20, 80, 20, REGION_PRIORITY_NORMAL, "beta", 32, 0, false);
        const auto last = outer.Add(0, 40, 80, 20, REGION_PRIORITY_NORMAL, "gamma", 32, 0, false);
        REQUIRE((first == 0 && middle == 1 && last == 2));
        REQUIRE((outer.Text(first)[0] == 'a' && outer.ByteLength(first) == 5));
        if (repeat == 0) {
            const auto field_region = std::find_if(
                g_regions.begin(), g_regions.end(),
                [](const W8Region& region) {
                    return region.text_field && region.y1 == 0;
                });
            REQUIRE(field_region != g_regions.end());
            const auto region_index =
                static_cast<unsigned int>(field_region - g_regions.begin());
            {
                ExclusiveRegionSet scope(ControlRegionSetIndex());
                dispatch_mouse(InputEventType::LeftButtonDown, 5, 5);
                REQUIRE(g_ui.captured == region_index);
                dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);
                // Retail fields include their right edge despite SDL paint extents.
                dispatch_mouse(InputEventType::LeftButtonDown, 80, 10);
                REQUIRE(g_ui.captured == region_index);
                dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);
                REQUIRE(g_ui.captured == 0);
            }
            // A field wins over its hosting panel, whose set is scanned before the control set.
            constexpr std::array<RegionTemplate, 1> panel_templates{{
                {W8_REGION_RECTANGLE, 0, 0, 640, 480, nullptr, 0, false, -1},
            }};
            OwnedRegionSet panel(panel_templates);
            panel.Enable();
            panel.EnableInput();
            dispatch_mouse(InputEventType::LeftButtonDown, 5, 5);
            REQUIRE(g_ui.captured == region_index);
            dispatch_mouse(InputEventType::LeftButtonUp, 100, 100);
            REQUIRE(g_ui.captured == 0);
        }
        outer.Remove(middle);
        REQUIRE(outer.ByteLength(last) == 5);
        {
            TextFieldGroup inner(font);
            REQUIRE(TextFieldGroup::Focused() == &inner);
            REQUIRE(inner.Add(0, 0, 80, 20, REGION_PRIORITY_NORMAL, "inner", 32, 0, false) == 0);
        }
        REQUIRE(TextFieldGroup::Focused() == &outer);
        REQUIRE((outer.Text(first)[0] == 'a' && outer.ByteLength(last) == 5));
        REQUIRE(outer.Add(0, 60, 80, 20, REGION_PRIORITY_NORMAL, "delta", 32, 0, false) == 3);
        outer.Remove(first);
        outer.Remove(3);
        outer.Remove(last);
        REQUIRE(outer.FocusedId() == -1);
    }
    REQUIRE(TextFieldGroup::Focused() == nullptr);
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

void button_image_fixture(const std::filesystem::path& root, std::string name, unsigned count,
                          bool app_data, unsigned width, unsigned height,
                          std::array<std::uint8_t, 5> frame_palette_indices,
                          short offset_x, short offset_y)
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
    if (frame_palette_indices[0]) {
        const std::array<SDL_Color, 5> colors{{
            {255, 0, 0, SDL_ALPHA_OPAQUE},
            {0, 255, 0, SDL_ALPHA_OPAQUE},
            {0, 0, 255, SDL_ALPHA_OPAQUE},
            {255, 255, 255, SDL_ALPHA_OPAQUE},
            {255, 255, 0, SDL_ALPHA_OPAQUE},
        }};
        for (std::size_t index = 0; index < colors.size(); ++index) {
            auto& entry = palette[frame_palette_indices[index]];
            entry.ubRed = colors[index].r;
            entry.ubGreen = colors[index].g;
            entry.ubBlue = colors[index].b;
        }
    } else {
        palette[1].ubRed = 255;
    }
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    std::vector<STCISubImage> regions(count);
    for (unsigned index = 0; index < count; ++index) {
        regions[index].uiDataOffset = index * image_size;
        regions[index].uiDataLength = image_size;
        regions[index].sOffsetX = offset_x;
        regions[index].sOffsetY = offset_y;
        regions[index].usWidth = width;
        regions[index].usHeight = height;
    }
    write(regions.data(), regions.size() * sizeof(STCISubImage));
    std::vector<std::uint8_t> pixels(width + 2, 1);
    pixels.front() = width;
    pixels.back() = 0;
    for (unsigned index = 0; index < count; ++index) {
        if (frame_palette_indices[0])
            pixels[1] = frame_palette_indices[index % frame_palette_indices.size()];
        for (unsigned row = 0; row < height; ++row)
            write(pixels.data(), pixels.size());
    }
    if (app_data) {
        const std::uint8_t bytes[]{7, 8, 9, 10};
        write(bytes, sizeof(bytes));
    }
}

void bitmap_image_fixture(const std::filesystem::path& root, std::string name)
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
    header.uiOriginalSize = 2;
    header.uiStoredSize = 2;
    header.fFlags = STCI_INDEXED;
    header.usWidth = 2;
    header.usHeight = 1;
    header.Indexed.uiNumberOfColours = 256;
    header.ubDepth = 8;
    write(&header, STCI_HEADER_SIZE);
    std::array<STCIPaletteElement, 256> palette{};
    palette[1].ubRed = 255;
    write(palette.data(), STCI_8BIT_PALETTE_SIZE);
    const std::uint8_t pixels[]{0, 1};
    write(pixels, sizeof(pixels));
}

std::uint16_t surface_pixel(const SDL_Surface& surface, int x, int y)
{
    const auto* row = reinterpret_cast<const std::uint16_t*>(
        static_cast<const std::uint8_t*>(surface.pixels) + y * surface.pitch);
    return row[x];
}

class CatalogArtworkAssets {
public:
    CatalogArtworkAssets()
        : previous_roots_(w8_native::path_roots())
    {
        const auto assets = directory_.path / "assets";
        button_image_fixture(assets, "Data/Main Interface/main_scroll.sti", 12, false, 4, 2, {}, 2, 3);
        bitmap_image_fixture(assets, "Data/Portraits/Large/Lhummf.sti");
        g_image_assets.clear();
        w8_native::configure_paths({assets.string(), (directory_.path / "user").string(), {}});
    }

    ~CatalogArtworkAssets()
    {
        g_image_assets.clear();
        w8_native::configure_paths(previous_roots_);
    }

private:
    ScopedTemporaryDirectory directory_{"wiz8-catalog-artwork"};
    w8_native::Roots previous_roots_;
};

void generic_button_images()
{
    initialize_test_framebuffer();
    RegionStateSnapshot saved_regions;
    ResetRegions();
    ScopedTemporaryDirectory temporary{"wiz8-button-images"};
    const auto& root = temporary.path;
    const auto assets = root / "assets";
    button_image_fixture(assets, std::string(DEFAULT_GENERIC_BUTTON_OFF));
    button_image_fixture(assets, "button.sti", 9, false, 1, 1, {1, 2, 3, 4, 5});
    button_image_fixture(assets, "short.sti", 8);
    g_image_assets.clear();
    ScopedAssetRoots scoped_roots{{assets.string(), (root / "user").string(), {"", "", ""}}};

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
        for (std::uintmax_t cut : {std::size_t{0}, STCI_HEADER_SIZE - 1u,
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
        auto sprite = g_image_assets.sprites(long_image_path);
        REQUIRE((sprite && sprite->frames.size() == 3));
        REQUIRE((sprite->frames[2].width == 1 && sprite->frames[2].paletteIndexAt(0, 0) == 1));
        REQUIRE(sprite->palette[1].r == 255);
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
    // Appearance instances retain their graphics independently of the loader
    // and image manager.
    auto graphics = ButtonGraphics::Load("button.sti", 0, 1, 2, 3, 4);
    REQUIRE(graphics);
    REQUIRE((graphics->MaxWidth == 1 && graphics->MaxHeight == 1));
    std::weak_ptr<const ButtonGraphics> lifetime = graphics;
    ButtonAppearance first_appearance;
    ButtonAppearance second_appearance;
    first_appearance.quick_image = second_appearance.quick_image = graphics;
    const auto& frame_colors = graphics->object().colors;
    auto target_owner = CreateCpuStorage(8, 8, SDL_PIXELFORMAT_RGB565);
    REQUIRE(target_owner);
    auto& target = *target_owner;
    for (bool enabled : {false, true}) {
        for (bool pressed : {false, true}) {
            for (bool hover : {false, true}) {
                DrawButtonAppearance(target, first_appearance, {0, 0, 8, 8}, {enabled, pressed, hover});
                const auto* pixels = static_cast<const std::uint16_t*>(target.pixels);
                const unsigned frame = !enabled ? 0 : pressed ? (hover ? 4 : 3) : (hover ? 2 : 1);
                REQUIRE(pixels[0] == frame_colors[frame + 1]);
            }
        }
    }
    // Menu artwork preserves blank states and its enlarged-hover positioning.
    auto menu_sprites = std::make_shared<SpriteSheet>(graphics->object());
    menu_sprites->frames[2].width = 3;
    menu_sprites->frames[2].indices.assign(3, 3);
    ButtonAppearance menu{.quick_image = ButtonGraphics::Load(menu_sprites, 0, 1, 2, -1, -1)};
    SDL_FillSurfaceRect(&target, nullptr, 0);
    DrawButtonAppearance(target, menu, {1, 1, 1, 1}, {.hovered = true, .menu_artwork = true});
    const auto* menu_pixels = static_cast<const std::uint16_t*>(target.pixels);
    REQUIRE(menu_pixels[target.pitch / 2 + 1] == 0);
    REQUIRE(menu_pixels[target.pitch / 2 + 2] == frame_colors[3]);
    SDL_FillSurfaceRect(&target, nullptr, 0);
    DrawButtonAppearance(target, menu, {1, 1, 1, 1}, {.pressed = true, .menu_artwork = true});
    REQUIRE(menu_pixels[target.pitch / 2 + 1] == 0);
    // Ordinary buttons paint to the supplied canvas, including forced repaints.
    {
        UiLayer layer;
        W8ButtonWidget button(layer);
        REQUIRE(button.ConfigureIcon("button.sti", 0, 1, REGION_PRIORITY_NORMAL,
                                      W8_DIALOG_BUTTON_NO_TOOLTIP));
        button.SetPosition(2, 3);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.Draw(target);
        auto* pixels = static_cast<std::uint16_t*>(target.pixels);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);
        SDL_FillSurfaceRect(&target, nullptr, 0);
        button.m_dirty = true;
        button.Draw(target);
        REQUIRE(pixels[3 * (target.pitch / 2) + 2] == frame_colors[2]);

    }
    target_owner.reset();
    graphics.reset();
    ShutdownButtonImageManager();
    REQUIRE(!lifetime.expired());
    first_appearance.quick_image.reset();
    REQUIRE(!lifetime.expired());
    second_appearance.quick_image.reset();
    REQUIRE(lifetime.expired());

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
    const auto widget_count = std::count_if(
        g_regions.begin(), g_regions.end(),
        [](const W8Region& region) { return region.handler != nullptr; });
    {
        auto dialog = std::make_unique<W8ButtonWidget>();
        REQUIRE(dialog->ConfigureIcon("button.sti", 0, 1, REGION_PRIORITY_NORMAL,
                                      W8_DIALOG_BUTTON_NO_TOOLTIP));
        dialog->SetPosition(12, 13);
        REQUIRE(!dialog->ConfigureIcon("missing.sti", 0, 1, REGION_PRIORITY_NORMAL,
                                       W8_DIALOG_BUTTON_NO_TOOLTIP));
        REQUIRE((dialog->Bounds().x == 12 && dialog->Bounds().y == 13 && dialog->Bounds().w == 1));
        REQUIRE(std::any_of(g_regions.begin(), g_regions.end(),
                            [&dialog](const W8Region& region) {
                                return region.handler && region.x1 == dialog->Bounds().x &&
                                       region.y1 == dialog->Bounds().y;
                            }));
        REQUIRE(std::count_if(g_regions.begin(), g_regions.end(),
                              [](const W8Region& region) {
                                  return region.handler != nullptr;
                              }) == widget_count + 1);
    }
    REQUIRE(std::count_if(g_regions.begin(), g_regions.end(),
                          [](const W8Region& region) { return region.handler != nullptr; }) ==
            widget_count);
    // A dialog's click callback can close it. The dispatcher must not read
    // the released dialog while completing the click (including its sound).
    auto closing_dialog = std::make_unique<W8ButtonWidget>();
    REQUIRE(closing_dialog->Configure("button.sti", 0, 1, 2, 3, 4,
        [&](W8ButtonWidget*) { AfterUiDispatch([&] { closing_dialog.reset(); }); }, nullptr, false,
        REGION_PRIORITY_NORMAL, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr));
    closing_dialog->SetPosition(12, 13);
    PushButtonSoundScheme(0, true);
    {
        ExclusiveRegionSet scope(ControlRegionSetIndex());
        dispatch_mouse(InputEventType::LeftButtonDown, 12, 13, true, false, 1);
        dispatch_mouse(InputEventType::LeftButtonUp, 12, 13, false, false, 1);
    }
    REQUIRE_FALSE(closing_dialog);

    REQUIRE(!InitializeButtonImageManager());
    button_image_fixture(assets, std::string(DEFAULT_GENERIC_BUTTON_ON));
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
    const auto background_pixel = image->frames[0]->frames[8].paletteIndexAt(0, 0);
    REQUIRE(background_pixel);
    REQUIRE(image->fill_color == image->frames[0]->colors[*background_pixel]);
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
        REQUIRE((font->frames.size() == 9));
        Palette16 colours{};
        colours[1] = 0x1234;
        primary.setPalette(colours);
        colours[1] = 0;
        REQUIRE(primary.palette()[1] == 0x1234);
        REQUIRE(primary.createShades(FONT_GLOW_GREEN));
        REQUIRE(primary.setShade(15));
        REQUIRE(primary.currentPalette()[1] != primary.palette()[1]);
        REQUIRE(primary.setShade(Font::kNormalShade));
        REQUIRE(primary.currentPalette()[1] == 0x1234);
        Font secondary;
        REQUIRE(secondary.load(filename));
        REQUIRE(secondary.createShades(FONT_GLOW_RED));
        REQUIRE(secondary.shades[0] != primary.shades[0]);
        primary.clear();
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
    // Failed loads retain resources; moves preserve their ownership.
    // Font's move operations explicitly leave the source empty, so checking its
    // moved-from state is part of the contract rather than an accidental use.
    {
        Font source, moved;
        REQUIRE(source.height() == 0);
        REQUIRE(source.textWidth("empty") == 0);
        REQUIRE(source.load(filename));
        const auto height = source.height();
        REQUIRE_FALSE(source.load("missing-font.sti"));
        REQUIRE(source.height() == height);
        Font temporary(std::move(source));
        REQUIRE(temporary.height() == height);
        REQUIRE(source.height() == 0); // NOLINT(bugprone-use-after-move)
        moved = std::move(temporary);
        REQUIRE(moved.height() == height);
        REQUIRE(temporary.height() == 0); // NOLINT(bugprone-use-after-move)
        REQUIRE(moved.load(filename));
        source = std::move(moved);
        REQUIRE(source.height() == height);
        REQUIRE(moved.height() == 0); // NOLINT(bugprone-use-after-move)
    }
    ConfigureFontRendering(std::vector<std::uint16_t>{'?', 0x00e9, 0x4e2d, 'e', 0x0301, 'a', 'b', ' ', 'c'});
    Font primary;
    REQUIRE(primary.load(filename));
    REQUIRE(primary.textWidth("é") > 0);
    REQUIRE(primary.codepointPrefixWidth("é中", 1) == primary.textWidth("é"));
    REQUIRE(primary.codepointPrefixWidth("é中", 2) == primary.textWidth("é中"));
    // Render borrowed substrings without depending on a trailing NUL or
    // accidentally consuming the next glyph in the backing string.
    auto expected_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    auto view_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(expected_surface);
    REQUIRE(view_surface);
    REQUIRE(SDL_FillSurfaceRect(expected_surface.get(), nullptr, 0));
    REQUIRE(SDL_FillSurfaceRect(view_surface.get(), nullptr, 0));
    DrawText(*expected_surface, primary, {0, 0}, "é");
    const std::string backing = "é中";
    DrawText(*view_surface, primary, {0, 0}, std::string_view(backing).substr(0, 2));
    const auto surface_pixels = [](const SDL_Surface& surface) {
        std::vector<std::uint16_t> pixels(static_cast<std::size_t>(surface.w * surface.h));
        const auto* bytes = static_cast<const std::uint8_t*>(surface.pixels);
        for (int y = 0; y < surface.h; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(bytes + y * surface.pitch);
            std::copy_n(row, surface.w, pixels.begin() + y * surface.w);
        }
        return pixels;
    };
    auto expected_pixels = surface_pixels(*expected_surface);
    auto view_pixels = surface_pixels(*view_surface);
    REQUIRE(view_pixels == expected_pixels);
    REQUIRE(
        std::any_of(view_pixels.begin(), view_pixels.end(), [](auto pixel) { return pixel != 0; }));
    auto clipped_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(clipped_surface);
    REQUIRE(SDL_FillSurfaceRect(clipped_surface.get(), nullptr, 0));
    const auto first_drawn = std::find_if(
        expected_pixels.begin(), expected_pixels.end(), [](auto pixel) { return pixel != 0; });
    REQUIRE(first_drawn != expected_pixels.end());
    const int first_drawn_x = static_cast<int>(first_drawn - expected_pixels.begin()) % 64;
    const SDL_Rect text_clip{first_drawn_x + 1, 0, 64 - first_drawn_x - 1, 32};
    {
        ScopedClipRect clipping(*clipped_surface, text_clip);
        DrawText(*clipped_surface, primary, {0, 0}, "é");
    }
    const auto clipped_pixels = surface_pixels(*clipped_surface);
    REQUIRE(clipped_pixels[first_drawn - expected_pixels.begin()] == 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = text_clip.x; x < 64; ++x) {
            REQUIRE(clipped_pixels[static_cast<std::size_t>(y * 64 + x)] ==
                    expected_pixels[static_cast<std::size_t>(y * 64 + x)]);
        }
    }

    auto wrapped_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(wrapped_surface);
    REQUIRE(primary.height() < 32);
    REQUIRE(SDL_FillSurfaceRect(wrapped_surface.get(), nullptr, 0));
    const int glyph_width = primary.textWidth("é");
    const SDL_Rect wrap_clip{0, 0, glyph_width, 32};
    {
        ScopedClipRect clipping(*wrapped_surface, wrap_clip);
        DrawText(*wrapped_surface, primary, {0, 0}, "éé", {.wrap = true});
    }
    const auto wrapped_pixels = surface_pixels(*wrapped_surface);
    REQUIRE(std::any_of(wrapped_pixels.begin() + primary.height() * 64, wrapped_pixels.end(),
                        [](auto pixel) { return pixel != 0; }));

    auto mono_surface = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_RGB565);
    REQUIRE(mono_surface);
    REQUIRE(SDL_FillSurfaceRect(mono_surface.get(), nullptr, 0));
    DrawText(*mono_surface, primary, {0, 0}, "é",
             {.mono = true, .colors = {.foreground = 0xf81f, .background = 0, .shadow = 0x07e0}});
    const auto mono_pixels = surface_pixels(*mono_surface);
    REQUIRE(std::find(mono_pixels.begin(), mono_pixels.end(), 0xf81f) != mono_pixels.end());

    initialize_test_framebuffer();
    ConfigureFontRendering(CreateEnglishTransTable());
    TakeUiUpload();
    const SDL_Point invalidation_at{16, 16};
    const int invalidated_width = primary.textWidth("A");
    const int invalidated_height = primary.height();
    REQUIRE(invalidated_width > 0);
    REQUIRE(invalidated_height > 0);
    DrawTextAndInvalidate(GetFrameBufferSurface(), primary, invalidation_at, "A");
    const SDL_Rect drawn{invalidation_at.x, invalidation_at.y, invalidated_width,
                         invalidated_height};
    const auto upload = TakeUiUpload();
    SDL_Rect covered;
    REQUIRE(SDL_GetRectIntersection(&upload, &drawn, &covered));
    REQUIRE(SDL_RectsEqual(&covered, &drawn));

    // Explicit-buffer rendering must use its font argument, even when the
    // current surface font has a different palette.
    Font secondary;
    REQUIRE(secondary.load(filename));
    Palette16 alternate;
    alternate.fill(0x1234);
    secondary.setPalette(alternate);
    REQUIRE(SDL_FillSurfaceRect(expected_surface.get(), nullptr, 0));
    REQUIRE(SDL_FillSurfaceRect(view_surface.get(), nullptr, 0));
    DrawText(*expected_surface, secondary, {0, 0}, "é中");
    DrawText(*view_surface, secondary, {0, 0}, "é中");
    expected_pixels = surface_pixels(*expected_surface);
    view_pixels = surface_pixels(*view_surface);
    REQUIRE(view_pixels == expected_pixels);
    // Rebuilding word ranges preserves UTF-8 byte boundaries and discards
    // old hover state, including when a line becomes empty.
    auto previous_level = std::exchange(g_level_block, std::make_unique<W8LevelRuntimeBlock>());
    g_level_block->text_box_font = &primary;
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
    g_level_block = std::move(previous_level);
    REQUIRE(g_font12point1.load(filename));
    {
    TextFieldGroup fields(g_font12point1);
    const auto field = fields.Add(0, 0, 80, 20, REGION_PRIORITY_NORMAL, "", 1000, 0x0f, false);
    fields.Focus(field);
    const std::string clusters = "é👩‍👩‍👧‍👦中";
    fields.SetText(field, clusters);
    fields.SetCursor(clusters.size());
    InputEvent backspace{};
    backspace.type = InputEventType::KeyDown;
    backspace.key = 8;
    REQUIRE(fields.HandleInput(backspace));
    REQUIRE(fields.Text(field) == "é👩‍👩‍👧‍👦");
    REQUIRE(fields.HandleInput(backspace));
    REQUIRE(fields.Text(field) == "é");
    REQUIRE(fields.HandleInput(backspace));
    REQUIRE(fields.Text(field).empty());
    InputEvent committed{};
    committed.type = InputEventType::Text;
    committed.text = 0x4e2d;
    REQUIRE(fields.HandleInput(committed));
    REQUIRE(fields.Text(field) == "中");
    committed.text = 0x1f600;
    REQUIRE(fields.HandleInput(committed));
    REQUIRE(fields.Text(field) == "中😀");
    fields.SetText(field, "");
    const std::array<char32_t, 7> family = {0x1f469, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466};
    for (const auto scalar : family) {
        committed.text = scalar;
        REQUIRE(fields.HandleInput(committed));
    }
    REQUIRE(fields.Text(field) == "👩‍👩‍👧‍👦");
    REQUIRE(fields.HandleInput(backspace));
    REQUIRE(fields.Text(field).empty());
    std::string long_text;
    for (unsigned n = 0; n < 100; ++n) long_text += "中";
    fields.SetText(field, long_text);
    fields.SetCursor(long_text.size());
    REQUIRE(fields.Cursor() == 300);
    REQUIRE(fields.ByteLength(field) == 300);
    REQUIRE(fields.HandleInput(backspace));
    REQUIRE((fields.Cursor() == 297 && fields.Text(field).size() == 297));
    }
    ResetFontRendering();
    g_font12point1.clear();
    ShutdownButtonImageManager();

    g_image_assets.clear();
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

TEST_CASE("Owned control region priority", "[owner]")
{
    owned_regions_honor_priority();
}

TEST_CASE("Input layers preserve backend ordering and shape boundaries", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    std::vector<int> hits;
    const auto record = [&](int id) {
        return [&, id](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Wheel) hits.push_back(id);
            return UiResult::Handled;
        };
    };
    UiLayer later(20);
    later.add(SDL_Rect{10, 10, 11, 11}, record(1), 100);
    UiLayer earlier(10);
    auto& first = earlier.add(SDL_Rect{10, 10, 11, 11}, record(2), 5);
    auto& tie = earlier.add(SDL_Rect{10, 10, 11, 11}, record(3), 5);
    auto& circle = earlier.add(UiCircle{{15, 15}, 5}, record(4), 6);
    dispatch_mouse(InputEventType::MouseWheel, 20, 15); // Circumference included.
    dispatch_mouse(InputEventType::MouseWheel, 20, 20); // Rectangle's last pixel.
    dispatch_mouse(InputEventType::MouseWheel, 21, 20); // Outside SDL extent.
    REQUIRE(hits == std::vector<int>{4, 2});
    first.setEnabled(false);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    earlier.remove(tie);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    circle.setShape(SDL_Rect{20, 20, 1, 1});
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    earlier.setEnabled(false);
    dispatch_mouse(InputEventType::MouseWheel, 20, 20);
    REQUIRE(hits == std::vector<int>{4, 2, 3, 1, 4, 1});

    earlier.setEnabled(true);
    first.setEnabled(true);
    circle.setEnabled(false);
    UiLayer same_order(10);
    same_order.add(SDL_Rect{10, 10, 11, 11}, record(5), 1000);
    dispatch_mouse(InputEventType::MouseWheel, 15, 15);
    REQUIRE(hits.back() == 2); // Equal layer orders use creation order.
}

TEST_CASE("Input layers keep capture outside and release it for gameplay drops", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    std::vector<UiPointerEvent> events;
    int drops = 0;
    HitTarget* drag_source = nullptr;
    UiLayer layer;
    auto& source = layer.add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        events.push_back(event);
        if (drag_source && event.action == UiPointerAction::Leave)
            drag_source->releaseCapture();
        return UiResult::Handled;
    });
    layer.add(SDL_Rect{40, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up) ++drops;
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true, false, 2);
    REQUIRE(events.back().clicks == 2);
    REQUIRE(events.back().local.x == 5);
    REQUIRE(events.back().local.y == 5);
    UpdateRegionMousePosition(45, 15);
    REQUIRE(events.back().action == UiPointerAction::Leave);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(events.back().action == UiPointerAction::Up);
    REQUIRE_FALSE(events.back().inside);
    REQUIRE(events.back().captured_buttons == SDL_BUTTON_LMASK);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(drops == 0);

    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    const auto source_index = g_ui.captured;
    drag_source = &source;
    UpdateRegionMousePosition(45, 15);
    REQUIRE(g_ui.captured == 0);
    REQUIRE((g_regions[source_index].flags & W8_REGION_LEFT_BUTTON_HELD) == 0);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(drops == 1);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    source.setEnabled(false);
    REQUIRE(g_ui.captured == 0);
    dispatch_mouse(InputEventType::LeftButtonUp, 45, 15);
    REQUIRE(drops == 2);
}

TEST_CASE("Text fields paint only into their supplied surface", "[owner][ui-layer]")
{
    initialize_test_framebuffer();
    auto& framebuffer = GetFrameBufferSurface();
    SDL_FillSurfaceRect(&framebuffer, nullptr, 0x1234);
    auto before = CopyCpuStorage(framebuffer);
    auto target = CreateCpuStorage(100, 50, SDL_PIXELFORMAT_RGB565);
    REQUIRE(before);
    REQUIRE(target);
    SDL_FillSurfaceRect(target.get(), nullptr, 0xffff);
    Font font{};
    TextFieldGroup fields(font);
    const auto field = fields.Add(0, 0, 80, 20, 0, "", 32, 0, false);
    fields.Focus(field);
    fields.ClearFocus();
    fields.RenderAll(*target);
    REQUIRE(*static_cast<std::uint16_t*>(target->pixels) == 0);
    REQUIRE(std::memcmp(framebuffer.pixels, before->pixels,
                        framebuffer.pitch * framebuffer.h) == 0);
}

TEST_CASE("Modal layers isolate editors and restore focus after deferred close", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    Font font{};
    TextFieldGroup outer(font);
    const int outer_field = outer.Add(0, 0, 80, 20, 0, "outer", 32, 0xf, false);
    outer.Focus(outer_field);
    outer.SetCursor(2);
    struct Editor {
        UiLayer input{UiLayerMode::Modal};
        TextFieldGroup fields;
        explicit Editor(Font& font) : fields(input, font)
        {
            fields.Focus(fields.Add(0, 30, 80, 20, 0, "inner", 32, 0xf, false));
        }
    };
    auto editor = std::make_unique<Editor>(font);
    REQUIRE(TextFieldGroup::Focused() == &editor->fields);
    REQUIRE_FALSE(outer.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'x'}));
    REQUIRE(editor->fields.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'é'}));
    REQUIRE(editor->fields.Text(0) == "é");
    {
        Editor nested(font);
        REQUIRE(TextFieldGroup::Focused() == &nested.fields);
        REQUIRE_FALSE(editor->fields.HandleInput(InputEvent{.type = InputEventType::Text, .text = U'x'}));
    }
    REQUIRE(TextFieldGroup::Focused() == &editor->fields);
    editor->input.add(SDL_Rect{90, 30, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up) {
            AfterUiDispatch([&] { editor.reset(); });
            REQUIRE(editor != nullptr);
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 95, 35, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 95, 35);
    REQUIRE_FALSE(editor);
    REQUIRE(TextFieldGroup::Focused() == &outer);
    REQUIRE(outer.FocusedId() == outer_field);
    REQUIRE(outer.Cursor() == 2);
    REQUIRE(outer.Text(outer_field) == "outer");
    REQUIRE(g_ui.captured == 0);
}

TEST_CASE("Legacy modal scopes block input layers and restore valid capture", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    int releases = 0;
    UiLayer screen;
    auto& target = screen.add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up) ++releases;
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    const auto captured = g_ui.captured;
    REQUIRE(captured != 0);
    {
        OwnedRegionSet modal(std::span<const RegionTemplate>{});
        ExclusiveRegionSet scope(modal.set());
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(releases == 0);
        REQUIRE(g_ui.captured == 0);
        {
            OwnedRegionSet nested(std::span<const RegionTemplate>{});
            ExclusiveRegionSet nested_scope(nested.set());
            dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
            REQUIRE(g_ui.captured == 0);
        }
    }
    REQUIRE(g_ui.captured == captured);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(releases == 1);

    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    {
        OwnedRegionSet modal(std::span<const RegionTemplate>{});
        ExclusiveRegionSet scope(modal.set());
        SECTION("A disabled target cannot regain suspended capture") { target.setEnabled(false); }
        SECTION("A disabled layer cannot regain suspended capture") { screen.setEnabled(false); }
        SECTION("A removed target cannot regain suspended capture") { screen.remove(target); }
    }
    REQUIRE(g_ui.captured == 0);
    REQUIRE(g_ui.current == 0);
    REQUIRE(g_ui.hover == 0);
}

TEST_CASE("Input-layer owners close only after the outer dispatch completes", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto region_count = LiveDynamicRegionCount();
    const auto set_count = LiveRegionSetCount();
    auto layer = std::make_unique<UiLayer>();
    bool finished = false;
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down) {
            AfterUiDispatch([&] {
                REQUIRE(finished);
                layer.reset();
            });
            // Reentrant dispatch must not flush the outer callback's close.
            dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
            REQUIRE(layer != nullptr);
            finished = true;
        }
        return UiResult::Handled;
    });
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    REQUIRE(layer == nullptr);
    REQUIRE(g_ui.current == 0);
    REQUIRE(g_ui.hover == 0);
    REQUIRE(g_ui.captured == 0);
    REQUIRE(LiveDynamicRegionCount() == region_count);
    REQUIRE(LiveRegionSetCount() == set_count);

    layer = std::make_unique<UiLayer>();
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Enter)
            AfterUiDispatch([&] { layer.reset(); });
        return UiResult::Handled;
    });
    UpdateRegionMousePosition(15, 15);
    REQUIRE(layer == nullptr);
    REQUIRE(g_ui.hover == 0);
    REQUIRE(g_ui.current == 0);
}

TEST_CASE("The input pump finishes its policy before committing a layer close", "[owner][ui-layer]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    InputEvent pending;
    while (DequeueEvent(&pending)) {}
    auto layer = std::make_unique<UiLayer>();
    layer->add(SDL_Rect{10, 10, 20, 20}, [&](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Down)
            AfterUiDispatch([&] { layer.reset(); });
        return UiResult::Handled;
    });
    QueueEvent(InputEventType::LeftButtonDown, {15, 15});
    QueueEvent(InputEventType::LeftButtonUp, {15, 15});
    int consumed = 0;
    UiInputPolicy policy;
    policy.track_hover = false;
    policy.consumed = [&](const InputEvent&) {
        REQUIRE(layer != nullptr);
        ++consumed;
        return InputRoute::Handled;
    };
    REQUIRE(PumpUiInput(policy));
    REQUIRE(consumed == 1);
    REQUIRE(layer == nullptr);
    REQUIRE(DequeueEvent(&pending));
    REQUIRE(pending.type == InputEventType::LeftButtonUp);
    REQUIRE_FALSE(DequeueEvent(&pending));
}

TEST_CASE("Callback reentrancy and region teardown", "[owner]")
{
    region_registration_and_callback_lifetimes();
    region_nested_input();
    nested_hover_transitions();
    owned_region_captures_across_frames();
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

TEST_CASE("Font measurements skip glyphs outside the sprite sheet", "[font][owner]")
{
    initialize_test_framebuffer();
    ScopedTemporaryDirectory directory{"wiz8-font-glyph-range"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets, "font.sti", 1, false, 1, 1);
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            ResetFontRendering();
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};

    ConfigureFontRendering(CreateEnglishTransTable());
    Font font;
    REQUIRE(font.load("font.sti"));
    REQUIRE(font.glyphs->frames.size() == 1);
    REQUIRE(GetIndex(U'é') >= static_cast<std::int16_t>(font.glyphs->frames.size()));
    REQUIRE(font.textWidth("é") == 0);
    REQUIRE(font.prefixWidth("é", 2) == 0);
    W8ControlsRect tooltip_bounds{0, 0, 250, 250};
    W8TextBuffer tooltip(&tooltip_bounds, "é", font,
                         g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    REQUIRE(tooltip.m_maxLineWidth == 0);
    REQUIRE(tooltip.m_lineCount == 1);
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

TEST_CASE("Text controls preserve toggles, repeat and radio selection", "[owner][button]")
{
    PushButtonSoundScheme(0, true);
    W8TextControl toggle(-1, 0, 0, 40, 20);
    toggle.SetActive(true);
    toggle.AddLayoutFlags(g_W8TextControlLayoutToggle | W8_TEXT_CONTROL_SILENT);
    int primary = 0;
    toggle.m_onPrimary = [&] { ++primary; };
    toggle.OnLeftButtonDown(0);
    REQUIRE(toggle.IsPressed());
    REQUIRE_FALSE(toggle.IsSelected());
    REQUIRE(primary == 0);
    toggle.OnLeftButtonUp(0);
    REQUIRE(toggle.IsSelected());
    REQUIRE(primary == 1);
    toggle.OnMouseLeave(0);
    REQUIRE(toggle.IsPressed());
    toggle.OnLeftButtonDown(0);
    toggle.OnLeftButtonUp(0);
    REQUIRE_FALSE(toggle.IsSelected());
    REQUIRE(primary == 2);
    toggle.AddLayoutFlags(g_W8TextControlLayoutStayLatched);
    for (int click = 0; click < 2; ++click) {
        toggle.OnLeftButtonDown(0);
        toggle.OnLeftButtonUp(0);
    }
    REQUIRE(toggle.IsSelected());
    REQUIRE(primary == 4);

    W8TextControl repeat(-1, 0, 0, 40, 20);
    repeat.SetActive(true);
    repeat.AddLayoutFlags(W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD | W8_TEXT_CONTROL_SILENT);
    primary = 0;
    int secondary = 0;
    repeat.m_onPrimary = [&] { ++primary; };
    repeat.m_onSecondary = [&] { ++secondary; };
    repeat.OnLeftButtonDown(0);
    repeat.ActivatePrimary(0);
    REQUIRE(primary == 1);
    repeat.OnRightButtonUp(0);
    REQUIRE(secondary == 0);
    repeat.OnLeftButtonUp(0);
    REQUIRE(primary == 2);
    repeat.ActivateSecondary(0);
    REQUIRE(secondary == 1);
    repeat.OnRightButtonUp(0);
    REQUIRE(secondary == 1);

    W8ControlSelection selection;
    selection.AddEntry(&toggle);
    selection.AddEntry(&repeat);
    int notifications = 0;
    selection.on_selection = [&](int) { ++notifications; };
    selection.SetSelected(0, false);
    for (int click = 0; click < 2; ++click) {
        repeat.OnLeftButtonDown(0);
        repeat.OnLeftButtonUp(0);
    }
    REQUIRE((selection.m_selectedIndex == 1 && !toggle.IsSelected() && repeat.IsSelected()));
    REQUIRE(notifications == 1);
    selection.SetSelected(0, false);
    REQUIRE((selection.m_selectedIndex == 0 && toggle.IsSelected() && !repeat.IsSelected()));
    REQUIRE(notifications == 1);
}

TEST_CASE("Dialog controls own interaction without legacy GUI buttons", "[owner]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    ExclusiveRegionSet button_scope(ControlRegionSetIndex());
    struct TooltipStateSnapshot {
        bool tooltips_enabled = g_settings.tooltips_enabled;
        std::vector<std::string> localized_strings = std::move(g_localized_strings);
        ~TooltipStateSnapshot()
        {
            g_settings.tooltips_enabled = tooltips_enabled;
            g_localized_strings = std::move(localized_strings);
        }
    } tooltip_state;
    g_settings.tooltips_enabled = true;
    g_localized_strings.emplace_back("Button tooltip");
    Font font{};
    PushButtonSoundScheme(0, true);
    int activations = 0;
    {
        W8ButtonWidget button;
        REQUIRE(button.ConfigureTextButton("button", font, 0, 0, 10, 10, 40, 20,
            [&](W8ButtonWidget* control) {
                REQUIRE(control->GetUserData() == 42);
                ++activations;
            }, 42));
        button.SetTooltipIndex(0);
        button.SetTooltipEnabled(true);
        auto region = std::find_if(g_regions.begin(), g_regions.end(),
                                   [](const W8Region& item) {
                                       return item.x1 == 10 && item.y1 == 10 && item.help_enabled;
                                   });
        REQUIRE(region != g_regions.end());
        REQUIRE(region->help_enabled);
        REQUIRE(region->help_text_provider() == "Button tooltip");
        button.SetTooltipEnabled(false);
        REQUIRE_FALSE(region->help_enabled);
        REQUIRE_FALSE(region->help_text_provider);
        GetInputState().left_button = false;
        GetInputState().mouse = {400, 400};
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true, false, 1);
        REQUIRE((button.IsPressed() && activations == 0));
        REQUIRE((button.GetMousePosition().x == 15 && button.GetMousePosition().y == 15));
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15, false, false, 1);
        REQUIRE(!button.IsPressed());
        REQUIRE(activations == 1);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 1);
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::MouseMove, 80, 80, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        button.SetEnabled(false);
        button.SetEnabled(true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 1));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        button.SetVisible(false);
        button.SetVisible(true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 1);
        REQUIRE(button.IsConfigured());
        button.Resize(60, 30);
        button.SetPosition(20, 20);
        REQUIRE((button.Bounds().w == 60 && button.Bounds().h == 30));
        dispatch_mouse(InputEventType::LeftButtonDown, 75, 45, true);
        REQUIRE(button.IsLeftButtonHeld());
        dispatch_mouse(InputEventType::LeftButtonUp, 75, 45);
        REQUIRE(!button.IsLeftButtonHeld());
        REQUIRE(activations == 2);
        button.SetPosition(10, 10);
        button.Resize(40, 20);
        button.policy.repeat = true;
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 15, 15, true);
        REQUIRE(activations == 4);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE((!button.IsPressed() && activations == 4));
        button.policy.repeat = false;
        button.policy.activation = ButtonActivation::Press;
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        REQUIRE(activations == 5);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 5);
        button.Reset();
        REQUIRE((!button.IsEnabled() && !button.IsPressed() && button.Bounds().w == 0));
        dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
        REQUIRE(activations == 5);
    }

    auto closing = std::make_unique<W8ButtonWidget>();
    closing->ConfigureTextButton("close", font, 0, 0, 10, 10, 40, 20,
        [&](W8ButtonWidget*) { AfterUiDispatch([&] { closing.reset(); }); }, 0);
    dispatch_mouse(InputEventType::LeftButtonDown, 15, 15, true);
    dispatch_mouse(InputEventType::LeftButtonUp, 15, 15);
    REQUIRE(!closing);
}

TEST_CASE("Modal confirmations and scrollbars dispatch through their native owners", "[owner]")
{
    initialize_test_framebuffer();
    RegionStateSnapshot saved_state;
    ResetRegions();
    ExclusiveRegionSet button_scope(ControlRegionSetIndex());
    ScopedTemporaryDirectory temporary{"wiz8-modal-controls"};
    const auto& root = temporary.path;
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
    ScopedAssetRoots scoped_roots{{assets.string(), (root / "user").string(), {"", "", ""}}};
    PushButtonSoundScheme(0, true);
    std::vector<int> positions;
    ScrollPositionCapture position_capture(positions);
    {
        W8DialogScrollBar bar;
        const W8DialogScrollBar::Resources resources{
            "arrows.sti", "track.sti", 0, RecordScrollPosition};
        REQUIRE(bar.CreateControls(&resources));
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        GetInputState().mouse.y = 400;
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 116, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 116);
        REQUIRE(positions == std::vector<int>{17});
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 21);
        REQUIRE(positions == std::vector<int>{17, 16, 15});
        bar.SetLayout(100, 20, 1, 0, 10, 30);
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 21);
        REQUIRE(positions.size() == 3);
        bar.SetLayout(100, 20, 20, 5, 10, 30);
        dispatch_mouse(InputEventType::LeftButtonDown, 101, 118, true);
        dispatch_mouse(InputEventType::LeftButtonUp, 101, 118);
        REQUIRE(positions.back() == 6);
    }

    Font font{};
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
        auto& control = accept ? dialog.m_confirm_button : dialog.m_cancel_button;
        {
            // A dialog opened on top owns the modal scope: the one beneath
            // keeps its controls but takes no input until it is closed.
            MessageDialog inner;
            inner.InputLayer().setModal(true);
            dispatch_mouse(InputEventType::LeftButtonDown, control.Bounds().x, control.Bounds().y, true);
            dispatch_mouse(InputEventType::LeftButtonUp, control.Bounds().x, control.Bounds().y);
            REQUIRE(dialog.is_open);
        }
        dialog.InputLayer().setModal(true);
        dispatch_mouse(InputEventType::LeftButtonDown, control.Bounds().x, control.Bounds().y, true);
        REQUIRE(dialog.is_open);
        dispatch_mouse(InputEventType::LeftButtonUp, control.Bounds().x, control.Bounds().y);
        REQUIRE(!dialog.is_open);
        REQUIRE(dialog.accepted == accept);
    }

    REQUIRE(font.load("font.sti"));
    {
        W8ListBoxDialog list;
        list.SetOrigin(10, 10);
        list.SetExtent(100, 80);
        list.SetBackground("background.sti", 0);
        for (int index = 0; index < 20; ++index) list.m_lines.emplace_back("X");
        REQUIRE(list.CreateControls() == 0);
        list.Draw();
        list.InputLayer().setModal(true);
        GetInputState().mouse.y = 400;
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_area_button.Bounds().x + 3,
                       list.m_area_button.Bounds().y + 21, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_area_button.Bounds().x + 3,
                       list.m_area_button.Bounds().y + 21);
        REQUIRE(list.m_selected_line == 2);
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_down_button.Bounds().x, list.m_down_button.Bounds().y, true);
        dispatch_mouse(InputEventType::LeftButtonRepeat, list.m_down_button.Bounds().x, list.m_down_button.Bounds().y, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_down_button.Bounds().x, list.m_down_button.Bounds().y);
        REQUIRE(list.m_selected_line == 4);
        dispatch_mouse(InputEventType::LeftButtonDown, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 30, true);
        REQUIRE(list.m_selected_line == 15);
        dispatch_mouse(InputEventType::LeftButtonRepeat, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 20, true);
        dispatch_mouse(InputEventType::LeftButtonUp, list.m_third_text_button.Bounds().x + 1,
                       list.m_third_text_button.Bounds().y + 20);
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
                [button](const W8Region& item) {
                    const auto bounds = button->Bounds();
                    return item.x1 == bounds.x && item.y1 == bounds.y &&
                           item.x2 == bounds.x + bounds.w && item.y2 == bounds.y + bounds.h;
                });
            REQUIRE(region != g_regions.end());
            REQUIRE((region->flags & W8_REGION_INPUT_DISABLED) != 0);
            REQUIRE_FALSE(button->m_dirty);
        }
    }
    g_dialog_interface_font = old_font;
    g_image_assets.clear();
}

namespace {
int message_box_callback_count = 0;
void message_box_callback()
{
    ++message_box_callback_count;
}
}

TEST_CASE("Message box owns its buttons and isolates modal input", "[owner]")
{
    initialize_test_framebuffer();
    RegionStateSnapshot saved_state;
    ResetRegions();
    ScopedTemporaryDirectory directory{"wiz8-message-box"};
    const auto assets = directory.path / "assets";
    const auto previous_roots = w8_native::path_roots();
    button_image_fixture(assets / "Data" / "Message Box", "GENBUTN.STI");
    button_image_fixture(assets / "Data" / "Message Box", "GENBUTN2.STI");
    button_image_fixture(assets / "Data" / "Message Box", "GENBUTN3.STI");
    button_image_fixture(assets / "Data" / "Message Box", "GENBUTN4.STI");
    button_image_fixture(assets / "Data" / "Message Box", "Ok.sti", 5, false, 40, 20);
    button_image_fixture(assets / "Data" / "Message Box", "Cancel.sti", 5, false, 40, 20);
    button_image_fixture(assets / "Data" / "Dialogs", "DialogBackground.STI");
    g_image_assets.clear();
    w8_native::configure_paths({assets.string(), (directory.path / "user").string(), {}});
    struct Restore {
        w8_native::Roots roots;
        ~Restore()
        {
            if (IsMessageBoxActive()) {
                CloseMessageBox();
                RenderMessageBox();
                RenderMessageBox();
            }
            ShutdownButtonImageManager();
            g_image_assets.clear();
            w8_native::configure_paths(roots);
        }
    } restore{previous_roots};

    REQUIRE(InitializeButtonImageManager());
    TestOwnedRegionHandler screen_handler;
    const auto screen_set = new_test_region_set();
    OwnedRegion underlying(screen_set, REGION_PRIORITY_NORMAL, screen_handler);
    underlying.region().flags = W8_REGION_RECTANGLE;
    underlying.region().SetBounds(275, 256, 315, 276);
    const auto baseline_regions = LiveDynamicRegionCount();

    Font font{};
    PushButtonSoundScheme(0, true);
    for (const bool accepted : {true, false}) {
        message_box_callback_count = 0;
        REQUIRE(CreateMessageBox("", font, 0, true, true, message_box_callback));
        REQUIRE(IsMessageBoxActive());
        REQUIRE(LiveDynamicRegionCount() > baseline_regions);

        const auto x = accepted ? 280 : 340;
        dispatch_mouse(InputEventType::LeftButtonDown, x, 260, true);
        dispatch_mouse(InputEventType::LeftButtonUp, x, 260);
        REQUIRE(screen_handler.events.empty());

        RenderMessageBox();
        RenderMessageBox();
        REQUIRE_FALSE(IsMessageBoxActive());
        REQUIRE(message_box_callback_count == (accepted ? 1 : 0));
        REQUIRE(LiveDynamicRegionCount() == baseline_regions);
    }
}

TEST_CASE("Owned regions use SDL click counts", "[owner]")
{
    RegionStateSnapshot saved_state;
    ResetRegions();
    const auto set = ControlRegionSetIndex();
    TestOwnedRegionHandler handler;
    OwnedRegion region(set, REGION_PRIORITY_NORMAL, handler);
    region.region().flags = W8_REGION_RECTANGLE;
    region.region().SetBounds(0, 0, 40, 40);
    ExclusiveRegionSet scope(set);
    for (const std::uint8_t click_count : {0, 1, 2, 3, 4}) {
        const auto down = mouse_atom(InputEventType::LeftButtonDown, 10, 10, true, false, click_count);
        DispatchRegionInput(&down);
        REQUIRE(handler.events.back() == InputEventType::LeftButtonDown);
        REQUIRE(handler.clicks == click_count);
        const auto up = mouse_atom(InputEventType::LeftButtonUp, 10, 10);
        DispatchRegionInput(&up);
    }
}

TEST_CASE("Owned region sets copy templates and release dynamic slots", "[owner]")
{
    owned_region_set_templates_and_teardown();
}

TEST_CASE("Region set views share owned regions", "[owner]")
{
    region_set_views_share_owned_regions();
}

TEST_CASE("Screen region sets follow their screen lifecycle", "[owner]")
{
    screen_region_set_lifecycle();
}

TEST_CASE("Widget-owned regions release with their widget", "[owner]")
{
    widget_owned_regions_release_with_widget();
}

TEST_CASE("Input sink capture releases only its own capture", "[owner]")
{
    input_sink_capture_can_be_released();
}

TEST_CASE("Region set hit-test order follows declared order, not creation", "[owner]")
{
    region_set_order_beats_creation_order();
}

TEST_CASE("A stale exclusive scope hides a reused set slot", "[owner]")
{
    stale_exclusive_scope_blocks_reused_set_slot();
}

TEST_CASE("assert-plain", "[.death]")
{
    Assert(false);
    FAIL("Assert did not abort");
}

namespace {
void assertion_message_handler(const char* expression, const char*, w8_long, const char*)
{
    std::fprintf(stderr, "Assertion failed: %s\n", expression);
    std::abort();
}
}

TEST_CASE("assert-message", "[.death]")
{
    srAssertSetFunc(assertion_message_handler);
    srAssertFail("handler installed", __FILE__, __LINE__, 0);
    FAIL("srAssertFail did not abort");
}

TEST_CASE("Image assets share normalized paths and retain resources across clearing", "[owner][image]")
{
    initialize_test_framebuffer();
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
    REQUIRE(original->frames.size() == 1);
    REQUIRE(bitmap->w == 1);
}

TEST_CASE("CatalogArtwork renders sprite storage", "[owner][image][catalog]")
{
    initialize_test_framebuffer();
    CatalogArtworkAssets assets;

    auto& framebuffer = GetFrameBufferSurface();
    const auto sprites = CatalogArtwork::Load(0x86);
    REQUIRE(sprites.spriteSheet());
    REQUIRE_FALSE(sprites.bitmap());
    REQUIRE((sprites.size(0).x == 4 && sprites.size(0).y == 2));
    REQUIRE((sprites.position(0).x == 2 && sprites.position(0).y == 3));
    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    sprites.draw(framebuffer, 0, 10, 10);
    REQUIRE(surface_pixel(framebuffer, 12, 13) == sprites.spriteSheet()->colors[1]);
    TakeUiUpload();
    sprites.drawAndInvalidate(framebuffer, 0, 20, 20);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{20, 20};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }
    TakeUiUpload();
    sprites.invalidate(0, 30, 30);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{30, 30};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }
    REQUIRE_THROWS_AS(sprites.drawRect(framebuffer, {0, 0, 1, 1}, 40, 40),
                      std::logic_error);
}

TEST_CASE("CatalogArtwork renders bitmap storage with mirror key semantics", "[owner][image][catalog]")
{
    initialize_test_framebuffer();
    CatalogArtworkAssets assets;
    auto& framebuffer = GetFrameBufferSurface();
    const auto bitmap = CatalogArtwork::Load(0x11);
    REQUIRE_FALSE(bitmap.spriteSheet());
    REQUIRE(bitmap.bitmap());
    REQUIRE((bitmap.size(0).x == 2 && bitmap.size(0).y == 1));
    REQUIRE((bitmap.position(0).x == 0 && bitmap.position(0).y == 0));
    const auto red = Get16BPPColor({255, 0, 0, 255});
    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    bitmap.draw(framebuffer, 0, 50, 50);
    REQUIRE(surface_pixel(framebuffer, 50, 50) == 0x07e0);
    REQUIRE(surface_pixel(framebuffer, 51, 50) == red);
    TakeUiUpload();
    bitmap.drawAndInvalidate(framebuffer, 0, 60, 60);
    {
        const auto upload = TakeUiUpload();
        const SDL_Point corner{60, 60};
        REQUIRE(SDL_PointInRect(&corner, &upload));
    }

    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    bitmap.draw(framebuffer, 0, 70, 70, {.mirror = true});
    REQUIRE(surface_pixel(framebuffer, 70, 70) == red);
    REQUIRE(surface_pixel(framebuffer, 71, 70) == 0);
    bitmap.drawRect(framebuffer, {0, 0, 1, 1}, 80, 80);
    REQUIRE(surface_pixel(framebuffer, 80, 80) == 0);
}

TEST_CASE("Controls draw optional CatalogArtwork backgrounds", "[owner][catalog]")
{
    initialize_test_framebuffer();
    CatalogArtworkAssets assets;
    auto& framebuffer = GetFrameBufferSurface();
    const auto sprites = CatalogArtwork::Load(0x86);
    REQUIRE(SDL_FillSurfaceRect(&framebuffer, nullptr, 0x07e0));
    Controls with_background(100, 100, 140, 120, sprites, 0);
    with_background.m_active = true;
    with_background.m_dirty = true;
    with_background.Redraw(GetFrameBufferSurface(), false);
    REQUIRE(surface_pixel(framebuffer, 102, 103) == sprites.spriteSheet()->colors[1]);
    Controls without_background(150, 100, 190, 120, std::nullopt, 0);
    without_background.m_active = true;
    without_background.m_dirty = true;
    without_background.Redraw(GetFrameBufferSurface(), false);
    REQUIRE(surface_pixel(framebuffer, 152, 103) == 0x07e0);

    // A bounded invalidation must not narrow a pending whole-panel one.
    const SDL_Point extent = sprites.size(0);
    const W8ControlsRect corner{100, 100, 101, 101};
    TakeUiUpload();
    with_background.InvalidateAll();
    with_background.InvalidateArea(corner);
    with_background.Redraw(GetFrameBufferSurface(), false);
    const SDL_Rect whole{100, 100, extent.x, extent.y};
    const auto upload = TakeUiUpload();
    REQUIRE(SDL_RectsEqual(&upload, &whole));
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
        REQUIRE(sprites.frames.size() == 1);
        REQUIRE(&g_item_video_objects.Sprites(index) == &sprites);
    }
    REQUIRE(g_item_video_objects.LoadedImageCount() == records.size());
    REQUIRE(&g_item_video_objects.Sprites(0) == &g_item_video_objects.Sprites(1));
    auto held = g_image_assets.sprites("Data\\Items\\icon.sti");
    REQUIRE(held);
    g_image_assets.clear();
    REQUIRE(held->frames.size() == 1);
    REQUIRE(&g_item_video_objects.Sprites(0) == held.get());
    REQUIRE_THROWS_AS(CatalogArtwork::Load(-1, 0), std::out_of_range);
    REQUIRE_THROWS_AS(CatalogArtwork::Load(0, -1), std::out_of_range);
    REQUIRE_THROWS_AS(CatalogArtwork::Load(0x86, std::numeric_limits<int>::max()),
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

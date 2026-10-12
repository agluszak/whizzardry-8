#include <cstddef>
#include <cstdint>
#include <array>
#include <deque>
#include <fmt/format.h>
#include <algorithm>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <span>
#include <string_view>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include "wiz8/regions.h"
#include "Video2.h"
#include "compat/native.h"
#ifdef WIZ8_RUNTIME_TESTS
#include <string.h>

#include "runtime_instrumentation.h"
#include "wiz8/local_code/Gameloop.h"
#endif
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSPartyMovement.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/RCSItemsPage.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/cursor.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/sr_api.h"
#include "input.h"
#include "timer.h"

enum { W8_SCREEN_WIDTH = 640, W8_SCREEN_HEIGHT = 480, W8_HELP_MARGIN = 2 };
enum { W8_REGION_MODE_MASK = 0xf };

// bool-byte-ok: W8RegionCallback result
static unsigned char ConsumeRegionInput(const InputEvent*, W8Region*)
{
    return 1;
}

namespace {
ScreenRegionSet& InputSinkRegion()
{
    static constexpr std::array<RegionTemplate, 1> templates{{
        {0x00000001, 0, 0, 0, 0, ConsumeRegionInput, 0, false, 0},
    }};
    static ScreenRegionSet region{templates};
    return region;
}
}

void CaptureInputSink()
{
    auto& region = InputSinkRegion();
    region.Create();
    region.Enable();
    region.EnableInput();
    ActivateDialogRegion(region->index(0));
}

bool InputSinkCaptured()
{
    auto& region = InputSinkRegion();
    return region && g_captured_region_index == region->index(0);
}

void ReleaseInputSinkCapture()
{
    auto& region = InputSinkRegion();
    if (!region) return;
    ClearActiveRegionIfMatches(region->index(0));
    region.DisableInput();
    region.Disable();
}

// Retail catalog at GLOBAL WIZ8 0x00620048 / 0x0061f238 contained 313 regions / 51 sets; records now live with owning screens.
// GLOBAL: WIZ8 0x00620048
std::deque<W8Region> g_regions = {
    {0x00000001, 0, 0, 0, 0, 0, 0, false, 0, -1, 0},
};
// GLOBAL: WIZ8 0x0061f238
std::deque<W8RegionSet> g_region_sets = {W8RegionSet{}};
// GLOBAL: WIZ8 0x00689B3C
unsigned int g_current_region_index;
// GLOBAL: WIZ8 0x00689B40
std::string g_default_help_text;
// GLOBAL: WIZ8 0x00689B44
unsigned int g_captured_region_index;
// GLOBAL: WIZ8 0x00689B4C
unsigned int g_hover_region_index;
// GLOBAL: WIZ8 0x00689B50
bool g_region_help_force_enabled;
SDL_Point g_region_mouse_position{};

// GLOBAL: WIZ8 0x00689B32
bool g_dev_mode;

namespace {
struct ExclusiveRegionScope {
    ExclusiveRegionSet* scope;
    unsigned int set_index;
    std::uint64_t sequence;
};

std::vector<ExclusiveRegionScope> g_exclusive_region_sets;
std::vector<unsigned int> g_reusable_region_indices;
std::vector<unsigned int> g_reusable_region_set_indices;
// Live set indices sorted by (order, sequence); the hit-test scan order.
std::vector<unsigned int> g_region_set_scan_order;
std::uint64_t g_region_set_sequence = 0;
constexpr std::size_t kFirstDynamicRegionIndex = 1;
constexpr std::size_t kFirstDynamicRegionSetIndex = 1;
unsigned int g_control_region_set = 0;

// A scope whose set slot was destroyed and reused must not expose the new occupant.
bool ExclusiveScopeIsLive(const ExclusiveRegionScope& scope)
{
    return scope.set_index < g_region_sets.size() &&
           g_region_sets[scope.set_index].sequence == scope.sequence;
}

bool RegionAllowedByExclusiveSet(unsigned int region_index)
{
    if (g_exclusive_region_sets.empty()) return true;
    const auto& scope = g_exclusive_region_sets.back();
    if (!ExclusiveScopeIsLive(scope)) return false;
    const auto region_indices = g_region_sets[scope.set_index].Regions();
    return std::ranges::find(region_indices, region_index) != region_indices.end();
}

struct DispatchPoint {
    unsigned short x;
    unsigned short y;
};

DispatchPoint ToDispatchPoint(const InputEvent& event)
{
    return {
        static_cast<unsigned short>(event.position.x +
                                    g_cursor_hotspot_x),
        static_cast<unsigned short>(event.position.y +
                                    g_cursor_hotspot_y)};
}

bool RegionContainsInputPoint(unsigned int region_index, unsigned short x, unsigned short y,
                              bool dispatch_coordinates)
{
    const auto& region = g_regions[region_index];
    if (dispatch_coordinates && region.text_field) {
        return region.ContainsPoint(static_cast<unsigned short>(static_cast<int>(x) - g_cursor_hotspot_x),
                                    static_cast<unsigned short>(static_cast<int>(y) - g_cursor_hotspot_y));
    }
    return region.ContainsPoint(x, y);
}
}

// FUNCTION: WIZ8 0x004f27a0
void SetRegionHelpDelay(int delay_ms)
{
    if (delay_ms == 0) {
        delay_ms = g_settings.tooltip_delay_ms;
    }
    g_region_help_delay = delay_ms;
}

/* Region motion is re-derived from the current pointer, so it carries the live
   modifier and button state; drag handlers rely on the held-button mask. */
static InputEvent SyntheticMouseMove(SDL_Point position)
{
    const auto& input = GetInputState();
    InputEvent event{};
    event.timestamp = w8_get_ticks();
    event.modifiers = input.modifiers;
    event.buttons = (input.left_button ? SDL_BUTTON_LMASK : 0) |
                    (input.right_button ? SDL_BUTTON_RMASK : 0);
    event.type = InputEventType::MouseMove;
    event.position = position;
    return event;
}

std::int32_t RegionReasonForInput(const W8Region& region, const InputEvent& input)
{
    switch (input.type) {
    case InputEventType::MouseMove:
        if (region.flags & W8_REGION_MOUSE_LEAVE) return REGION_REASON_LOST_MOUSE;
        if (region.flags & W8_REGION_MOUSE_ENTER) return REGION_REASON_GAIN_MOUSE;
        return REGION_REASON_MOVE;
    case InputEventType::LeftButtonDown:
        return REGION_REASON_LBUTTON_DWN |
               (input.clicks >= 2 ? REGION_REASON_LBUTTON_DOUBLECLICK : 0);
    case InputEventType::LeftButtonUp:
        return REGION_REASON_LBUTTON_UP;
    case InputEventType::LeftButtonDoubleClick:
        return REGION_REASON_NONE;
    case InputEventType::LeftButtonRepeat:
        return REGION_REASON_LBUTTON_REPEAT;
    case InputEventType::RightButtonDown:
        return REGION_REASON_RBUTTON_DWN;
    case InputEventType::RightButtonUp:
        return REGION_REASON_RBUTTON_UP;
    case InputEventType::RightButtonRepeat:
        return REGION_REASON_RBUTTON_REPEAT;
    default:
        break;
    }
    return REGION_REASON_NONE;
}

// FUNCTION: WIZ8 0x004F27C0
void SetRegionHelpForceEnabled(bool enabled)
{
    g_region_help_force_enabled = enabled;
}

// FUNCTION: WIZ8 0x004F27D0
void EnableRegionHelpFlag(W8Region* region)
{
    region->help_enabled = true;
}

// FUNCTION: WIZ8 0x004F27E0
void DisableRegionHelpFlag(W8Region* region)
{
    region->help_enabled = false;
}

// FUNCTION: WIZ8 0x004f1220
void ReleaseDefaultHelpText(void)
{
    g_default_help_text.clear();
}

static bool FindEnabledRegionInSet(const W8RegionSet& set, unsigned short x, unsigned short y,
                                   bool dispatch_coordinates, unsigned int* found_region,
                                   bool text_fields_only = false)
{
    if (set.enabled != 1) return false;
    bool found_in_set = false;
    unsigned int best_region = 0;
    int best_priority = 0;
    for (const auto region_index : set.Regions()) {
        if (text_fields_only && !g_regions[region_index].text_field) continue;
        if (!RegionContainsInputPoint(region_index, x, y, dispatch_coordinates)) continue;
        const int priority = g_regions[region_index].priority;
        if (found_in_set && priority <= best_priority) continue;
        found_in_set = true;
        best_region = region_index;
        best_priority = priority;
    }
    if (found_in_set) *found_region = best_region;
    return found_in_set;
}

/* Sets are scanned by declared order, then creation sequence; the widget
   control set is always consulted last, except that its text fields come
   first because they are drawn over the panel hosting them. A modal scope
   restricts the scan to its own set. */
static bool FindEnabledRegionAtPoint(unsigned short x, unsigned short y, unsigned int* found_region,
                                     bool dispatch_coordinates = false)
{
    if (!g_exclusive_region_sets.empty()) {
        const auto& scope = g_exclusive_region_sets.back();
        return ExclusiveScopeIsLive(scope) &&
               FindEnabledRegionInSet(g_region_sets[scope.set_index], x, y, dispatch_coordinates,
                                      found_region);
    }
    const bool has_control_set =
        g_control_region_set != 0 && g_control_region_set < g_region_sets.size();
    if (has_control_set && FindEnabledRegionInSet(g_region_sets[g_control_region_set], x, y,
                                                  dispatch_coordinates, found_region, true)) {
        return true;
    }
    for (const auto set_index : g_region_set_scan_order) {
        if (set_index >= g_region_sets.size() || set_index == g_control_region_set) continue;
        if (FindEnabledRegionInSet(g_region_sets[set_index], x, y, dispatch_coordinates,
                                   found_region)) {
            return true;
        }
    }
    return has_control_set &&
           FindEnabledRegionInSet(g_region_sets[g_control_region_set], x, y, dispatch_coordinates,
                                  found_region);
}

namespace {
struct RegionDispatchResult {
    unsigned char handled;
    bool binding_unchanged;
};

// Region storage is stable, but a callback may destroy/rebind its widget.
RegionDispatchResult DispatchRegionEvent(W8Region& region, const InputEvent& event)
{
    const auto generation = region.binding_generation;
    unsigned char handled = 0;
    if (region.callback) {
        handled = region.callback(&event, &region);
    } else if (region.handler) {
        handled = static_cast<unsigned char>(region.handler->HandleRegionInput(region, event));
    } else {
        return {0, false};
    }
    return {handled, region.binding_generation == generation};
}

bool DispatchUnchangedRegion(W8Region& region, const InputEvent& event)
{
    return DispatchRegionEvent(region, event).binding_unchanged;
}

bool LeaveRegion(W8Region& region, const InputEvent& event)
{
    region.flags = (region.flags & ~W8_REGION_MOUSE_STATE_MASK) | W8_REGION_MOUSE_LEAVE;
    if (!DispatchUnchangedRegion(region, event)) return false;
    if (region.flags & W8_REGION_HELP_SHOWN) {
        VideoRemoveToolTip();
        region.flags &= ~W8_REGION_HELP_SHOWN;
    }
    region.flags &= ~W8_REGION_MOUSE_STATE_MASK;
    g_region_help_delay = static_cast<unsigned short>(g_settings.tooltip_delay_ms);
    SetRegionHelpForceEnabled(false);
    return true;
}

W8Region* FindDispatchRegion(const DispatchPoint& point, unsigned int& region_index)
{
    region_index =
        RegionAllowedByExclusiveSet(g_captured_region_index) ? g_captured_region_index : 0;
    if (region_index != 0) return &g_regions[region_index];

    if (FindEnabledRegionAtPoint(point.x, point.y, &region_index, true)) {
        return &g_regions[region_index];
    }
    return nullptr;
}
}

ExclusiveRegionSet::ExclusiveRegionSet(unsigned int set)
    : set_(set), sequence_(set < g_region_sets.size() ? g_region_sets[set].sequence : 0)
{
    const auto save_region = [](unsigned int index) {
        SavedRegionState saved{index, 0, 0};
        if (index != 0 && index < g_regions.size()) {
            auto& region = g_regions[index];
            saved.generation = region.binding_generation;
            saved.flags = region.flags;
        }
        return saved;
    };
    saved_current_ = save_region(g_current_region_index);
    saved_captured_ = save_region(g_captured_region_index);
    saved_hover_ = save_region(g_hover_region_index);

    if (saved_hover_.index != 0 && saved_hover_.index < g_regions.size()) {
        SDL_Point mouse;
        SGPMouseGetPos(&mouse);
        const auto event = SyntheticMouseMove(mouse);
        LeaveRegion(g_regions[saved_hover_.index], event);
    }
    const auto clear_mouse_state = [](unsigned int index) {
        if (index != 0 && index < g_regions.size()) {
            auto& region = g_regions[index];
            if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
            region.flags &= ~(W8_REGION_MOUSE_STATE_MASK | W8_REGION_HELP_SHOWN);
        }
    };
    clear_mouse_state(saved_current_.index);
    clear_mouse_state(saved_captured_.index);
    clear_mouse_state(saved_hover_.index);
    g_current_region_index = 0;
    g_captured_region_index = 0;
    g_hover_region_index = 0;
    g_exclusive_region_sets.push_back({this, set_, sequence_});
}

ExclusiveRegionSet::~ExclusiveRegionSet()
{
    size_t scope_index = g_exclusive_region_sets.size();
    for (size_t index = g_exclusive_region_sets.size(); index > 0; --index) {
        if (g_exclusive_region_sets[index - 1].scope == this) {
            scope_index = index - 1;
            break;
        }
    }
    if (scope_index == g_exclusive_region_sets.size()) return;

    const bool was_top = scope_index + 1 == g_exclusive_region_sets.size();
    if (!was_top) {
        if (auto* child = g_exclusive_region_sets[scope_index + 1].scope) {
            child->saved_current_ = saved_current_;
            child->saved_captured_ = saved_captured_;
            child->saved_hover_ = saved_hover_;
        }
        g_exclusive_region_sets.erase(g_exclusive_region_sets.begin() + scope_index);
        return;
    }

    const auto clear_mouse_state = [](unsigned int index) {
        if (index != 0 && index < g_regions.size()) {
            auto& region = g_regions[index];
            if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
            region.flags &= ~(W8_REGION_MOUSE_STATE_MASK | W8_REGION_HELP_SHOWN);
        }
    };
    clear_mouse_state(g_current_region_index);
    clear_mouse_state(g_captured_region_index);
    clear_mouse_state(g_hover_region_index);
    g_exclusive_region_sets.erase(g_exclusive_region_sets.begin() + scope_index);

    const auto restore_flags = [](const SavedRegionState& saved) {
        if (saved.index == 0 || saved.index >= g_regions.size() ||
            g_regions[saved.index].binding_generation != saved.generation) {
            return;
        }
        g_regions[saved.index].flags =
            (g_regions[saved.index].flags & ~W8_REGION_MOUSE_STATE_MASK) |
            (saved.flags & W8_REGION_MOUSE_STATE_MASK);
    };
    restore_flags(saved_current_);
    restore_flags(saved_captured_);
    restore_flags(saved_hover_);
    const auto restore_index = [](const SavedRegionState& saved) {
        if (saved.index == 0 || saved.index >= g_regions.size() ||
            g_regions[saved.index].binding_generation != saved.generation ||
            !RegionAllowedByExclusiveSet(saved.index)) {
            return 0u;
        }
        return saved.index;
    };
    g_current_region_index = restore_index(saved_current_);
    g_captured_region_index = restore_index(saved_captured_);
    g_hover_region_index = restore_index(saved_hover_);
}

/* Dispatch one mouse-position event through the enabled region sets. A forced
   modal region bypasses hit testing; otherwise the first containing region
   receives leave/enter transitions, hover help timing, and the ordinary
   position callback as one transaction. */
// FUNCTION: WIZ8 0x004f1360
unsigned int UpdateRegionMousePosition(int x, int y)
{
    g_region_mouse_position = {x, y};
    const auto event = SyntheticMouseMove({x, y});
    unsigned int region_index;

    if (g_captured_region_index != 0 &&
        RegionAllowedByExclusiveSet(g_captured_region_index)) {
        const auto captured_index = g_captured_region_index;
        W8Region* forced = &g_regions[captured_index];
        const bool inside = RegionContainsInputPoint(
            captured_index, static_cast<unsigned short>(x), static_cast<unsigned short>(y), true);
        const bool was_hovered = g_hover_region_index == captured_index;
        if (!inside && was_hovered) {
            const auto held_buttons =
                forced->flags & (W8_REGION_LEFT_BUTTON_HELD | W8_REGION_RIGHT_BUTTON_HELD);
            g_hover_region_index = 0;
            if (!LeaveRegion(*forced, event)) return g_current_region_index;
            forced->flags |= held_buttons;
            PlayButtonSound(1);
            return g_current_region_index;
        }
        if (inside && !was_hovered) {
            g_hover_region_index = captured_index;
            forced->flags = (forced->flags & ~W8_REGION_MOUSE_TRANSITION_MASK) |
                            W8_REGION_MOUSE_ENTER;
            SetRegionHelpText(fmt::format("Region {:d}", captured_index));
            if (!DispatchUnchangedRegion(*forced, event)) return g_current_region_index;
            if (forced->help_enabled &&
                (g_settings.tooltips_enabled || g_region_help_force_enabled)) {
                g_region_help_clock = w8_get_ticks() + g_region_help_delay;
            }
            forced->flags &= ~W8_REGION_MOUSE_TRANSITION_MASK;
            PlayButtonSound(0);
            return g_current_region_index;
        }
        forced->flags &= ~W8_REGION_MOUSE_TRANSITION_MASK;
        DispatchUnchangedRegion(*forced, event);
        return g_current_region_index;
    }
    if (g_captured_region_index != 0) g_captured_region_index = 0;

    if (FindEnabledRegionAtPoint(static_cast<unsigned short>(x), static_cast<unsigned short>(y),
                                 &region_index)) {
        W8Region* region = &g_regions[region_index];
        const auto generation = region->binding_generation;
        unsigned int previous_index = g_hover_region_index;
        g_current_region_index = region_index;
        if (previous_index != 0 && previous_index != region_index) {
            W8Region* previous = &g_regions[previous_index];
            if (!LeaveRegion(*previous, event) || region->binding_generation != generation)
                return g_current_region_index;
            PlayButtonSound(1);
        }
        if (previous_index != region_index) {
            region->flags |= W8_REGION_MOUSE_ENTER;
            SetRegionHelpText(fmt::format("Region {:d}", region_index));
        }
        if (!DispatchUnchangedRegion(*region, event)) return g_current_region_index;
        if (g_current_region_index != previous_index) {
            if (region->help_enabled &&
                (g_settings.tooltips_enabled || g_region_help_force_enabled)) {
                g_region_help_clock = w8_get_ticks() + (g_region_help_delay);
            }
            PlayButtonSound(0);
        }
        region->flags &= ~W8_REGION_MOUSE_TRANSITION_MASK;
        g_hover_region_index = g_current_region_index;
        return g_current_region_index;
    }

    g_current_region_index = 0;
    if (g_hover_region_index != 0) {
        unsigned int previous_index = g_hover_region_index;
        W8Region* previous = &g_regions[previous_index];
        if (!LeaveRegion(*previous, event)) return g_current_region_index;
        PlayButtonSound(1);
    }
    g_hover_region_index = g_current_region_index;
    return g_current_region_index;
}

SDL_Point GetRegionMousePosition()
{
    return g_region_mouse_position;
}

/* Find the first enabled region containing the mouse position.  Moving to a
   different region also sends the old region its leave transition and drops
   any help box it still owns. */
// FUNCTION: WIZ8 0x004f16f0
unsigned int FindRegionAtPoint(unsigned short x, unsigned short y)
{
    const auto event = SyntheticMouseMove({x, y});
    unsigned int region_index;

    if (g_captured_region_index != 0 &&
        RegionAllowedByExclusiveSet(g_captured_region_index)) {
        return g_captured_region_index;
    }

    if (FindEnabledRegionAtPoint(x, y, &region_index)) {
        if (g_hover_region_index != 0 && g_hover_region_index != region_index) {
            W8Region* previous = &g_regions[g_hover_region_index];
            const auto generation = g_regions[region_index].binding_generation;
            if (!LeaveRegion(*previous, event) || g_regions[region_index].binding_generation != generation)
                return 0;
            g_hover_region_index = 0;
            g_current_region_index = 0;
        }
        return region_index;
    }

    if (g_hover_region_index != 0 &&
        (g_regions[g_hover_region_index].flags & W8_REGION_HELP_SHOWN) != 0) {
        unsigned int previous_index = g_hover_region_index;
        VideoRemoveToolTip();
        g_regions[previous_index].flags &= ~W8_REGION_HELP_SHOWN;
    }
    return 0;
}

/* Route one queued input atom to the forced region, the current hot region,
   or the first enabled region under the event's mouse position. Retail carries
   three copies of the dispatch tail (0x004F1B41 for the region-set scan,
   0x004F1C4C and 0x004F1D34 for the current and captured regions); each runs
   the callback, plays the optional sound and returns the callback result. */
// FUNCTION: WIZ8 0x004f1910
unsigned char DispatchRegionInput(const InputEvent* event)
{
    unsigned int region_index = 0;
    int sound_id = -1;
    g_region_mouse_position = {event->position.x,
                               event->position.y};
    const auto point = ToDispatchPoint(*event);
#ifdef WIZ8_RUNTIME_TESTS
    if (event->type == InputEventType::MouseMove || event->type == InputEventType::LeftButtonDown ||
        event->type == InputEventType::LeftButtonUp || event->type == InputEventType::RightButtonDown ||
        event->type == InputEventType::RightButtonUp) {
        RuntimeObserve(RUNTIME_MOUSE_DISPATCH, event->type,
                       static_cast<w8_ulong>(point.x) | (static_cast<w8_ulong>(point.y) << 16),
                       g_current_screen_state.id);
    }
#endif

    W8Region* region = FindDispatchRegion(point, region_index);
    if (!region) return 0;
    if (region->help_enabled && (g_settings.tooltips_enabled || g_region_help_force_enabled) &&
        event->type != InputEventType::MouseMove) {
        if ((region->flags & W8_REGION_HELP_SHOWN) != 0) {
            VideoRemoveToolTip();
            region->flags &= ~W8_REGION_HELP_SHOWN;
        }
        if (region->help_enabled && (g_settings.tooltips_enabled || g_region_help_force_enabled)) {
            g_region_help_clock = w8_get_ticks() + (g_region_help_delay);
        }
    }

    switch (event->type) {
    case InputEventType::LeftButtonDown:
    case InputEventType::RightButtonDown:
        region->CaptureButtonDown(event->type);
        g_captured_region_index = region_index;
        g_current_region_index = region_index;
        g_hover_region_index = region_index;
        sound_id = 2;
        break;
    case InputEventType::LeftButtonUp:
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0) {
            sound_id = 3;
        }
        break;
    case InputEventType::RightButtonUp:
        if ((region->flags & W8_REGION_RIGHT_BUTTON_HELD) != 0) {
            sound_id = 3;
        }
        break;
    default:
        break;
    }

#ifdef WIZ8_RUNTIME_TESTS
    w8_ulong callback_address = 0;
    memcpy(&callback_address, &region->callback, sizeof(callback_address));
    RuntimeObserve(RUNTIME_REGION_ACTIVATED, region_index, callback_address, region->callback_id);
#endif
    const auto generation = region->binding_generation;
    const auto dispatch = DispatchRegionEvent(*region, *event);
    if (region->binding_generation == generation) {
        if (event->type == InputEventType::LeftButtonUp) {
            region->flags &= ~W8_REGION_LEFT_BUTTON_HELD;
            if (g_captured_region_index == region_index) g_captured_region_index = 0;
        } else if (event->type == InputEventType::RightButtonUp) {
            region->flags &= ~W8_REGION_RIGHT_BUTTON_HELD;
            if (g_captured_region_index == region_index) g_captured_region_index = 0;
        }
    }
    if (sound_id != -1) {
        PlayButtonSound(sound_id);
    }
    return dispatch.handled;
}

/* Raises the help box for one region, taking a stale one down first. The
   selected text comes either from the region's indexed notice entry or the
   shared fallback, and the final position is clamped inside the 640x480
   screen before the region records ownership of the box. */
// FUNCTION: WIZ8 0x004f2650
void ShowRegionHelp(unsigned int region_index)
{
    W8Region* region;
    unsigned int mode;
    std::string text;
    SDL_Point anchor;
    int width;
    int height;

    if (!g_settings.tooltips_enabled && !g_region_help_force_enabled) {
        return;
    }
    region = &g_regions[region_index];
    mode = region->flags & W8_REGION_MODE_MASK;
    if (mode != 1 && mode != 2 && (region->flags & W8_REGION_HELP_SHOWN) != 0) {
        VideoRemoveToolTip();
        region->flags &= ~W8_REGION_HELP_SHOWN;
    }
    if ((region->flags & W8_REGION_HELP_SHOWN) != 0) {
        return;
    }
    if (!region->help_text.empty()) {
        text = region->help_text;
    } else if (region->help_text_id == -1) {
        text = g_default_help_text;
        if (text.empty()) {
            return;
        }
    } else {
        text = g_localized_strings[region->help_text_id];
    }
    VideoToolTip(text);
    width = g_help_box_width + W8_HELP_MARGIN;
    height = g_help_box_height + W8_HELP_MARGIN;
    SGPMouseGetPos(&anchor);
    anchor.y -= height;
    if (anchor.x < 0) {
        anchor.x = W8_HELP_MARGIN;
    }
    if (anchor.x + width > W8_SCREEN_WIDTH - 1) {
        anchor.x = W8_SCREEN_WIDTH - width;
    }
    if (anchor.y < 0) {
        anchor.y = W8_HELP_MARGIN;
    }
    if (anchor.y + height > W8_SCREEN_HEIGHT - 1) {
        anchor.y = W8_SCREEN_HEIGHT - height;
    }
    VideoPositionToolTip(anchor.x, anchor.y);
    region->flags |= W8_REGION_HELP_SHOWN;
}

/* Force one region to the front of modal dispatch. If another region owned
   that role, send its ordinary mouse-leave callback first and release any
   transition object it still owns. */
// FUNCTION: WIZ8 0x004f2040
void ActivateDialogRegion(unsigned int region_index)
{
    if (region_index >= g_regions.size()) {
        srAssertFail("uiRegionIndex < guiRegionCount",
                     "C:\\Projects\\Wizardry 8\\Local Code\\RegionManager.cpp", 0x1d6, 0);
    }

    g_captured_region_index = region_index;
    g_current_region_index = region_index;
    if (g_hover_region_index == region_index) {
        return;
    }

    if (g_hover_region_index != 0) {
        const auto event = SyntheticMouseMove(g_region_mouse_position);

        W8Region* previous = &g_regions[g_hover_region_index];
        if (!LeaveRegion(*previous, event)) return;
        g_hover_region_index = 0;
    }
    g_regions[g_captured_region_index].flags &= ~W8_REGION_MOUSE_STATE_MASK;
}

// FUNCTION: WIZ8 0x004f21b0
unsigned char ClearActiveRegionIfMatches(unsigned int region_index)
{
    if (g_captured_region_index == region_index) {
        g_captured_region_index = 0;
        g_current_region_index = 0;
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x004f21d0
bool PumpUiInput(const UiInputPolicy& policy)
{
    if (policy.track_hover) {
        SDL_Point mouse;
        SGPMouseGetPos(&mouse);
        UpdateRegionMousePosition(mouse.x, mouse.y);
    }
    bool drained = false;
    InputEvent input;
    while (DequeueEvent(&input)) {
        drained = true;
        auto route = policy.before ? policy.before(input) : InputRoute::Pass;
        if (route == InputRoute::Pass) {
            const bool consumed = (!policy.ui_accepts || policy.ui_accepts(input)) &&
                                  DispatchRegionInput(&input) != 0;
            const auto& next = consumed ? policy.consumed : policy.unhandled;
            route = next ? next(input) : InputRoute::Handled;
        }
        if (route == InputRoute::Stop) break;
    }
    return drained;
}

unsigned int GetForcedRegion(void)
{
    return g_captured_region_index;
}

void W8RegionSet::SetInputEnabled(bool enabled) const
{
    for (auto index : Regions()) g_regions[index].SetInputEnabled(enabled);
}

bool W8Region::ContainsPoint(int x, int y) const
{
    switch (flags & W8_REGION_MODE_MASK) {
    case W8_REGION_RECTANGLE: {
        const SDL_Point point{x, y};
        const SDL_Rect bounds{x1, y1, x2 - x1 + 1, y2 - y1 + 1};
        return SDL_PointInRect(&point, &bounds);
    }
    case W8_REGION_CIRCLE: {
        const short delta_x = static_cast<short>(x - x1);
        const short delta_y = static_cast<short>(y - y1);
        return delta_x * delta_x + delta_y * delta_y <= x2 * x2;
    }
    default: return false;
    }
}

// FUNCTION: WIZ8 0x004f25a0
void UpdateRegionHelp(void)
{
    if (g_captured_region_index == 0) {
        if (g_current_region_index != 0 && g_regions[g_current_region_index].help_enabled &&
            (g_settings.tooltips_enabled || g_region_help_force_enabled) &&
            TimeUntilDeadline(g_region_help_clock) == 0) {
            ShowRegionHelp(g_current_region_index);
        }
    } else if (g_regions[g_captured_region_index].help_enabled &&
               (g_settings.tooltips_enabled || g_region_help_force_enabled) &&
               TimeUntilDeadline(g_region_help_clock) == 0) {
        ShowRegionHelp(g_captured_region_index);
    }
}

// FUNCTION: WIZ8 0x004f2750
void SetRegionHelpText(std::string_view text)
{
    ReleaseDefaultHelpText();
    g_default_help_text = text;
}

// FUNCTION: WIZ8 0x004f27f0
void ResetRegionHelp(bool delayed)
{
    unsigned int region_index = g_current_region_index;

    VideoRemoveToolTip();
    g_regions[region_index].flags &= 0xfffffdff;
    if (!delayed) {
        ShowRegionHelp(g_current_region_index);
    } else if (g_regions[g_current_region_index].help_enabled &&
               (g_settings.tooltips_enabled || g_region_help_force_enabled)) {
        g_region_help_clock = w8_get_ticks() + (g_region_help_delay);
    }
}

static void RemoveFromScanOrder(unsigned int set_index)
{
    std::erase(g_region_set_scan_order, set_index);
}

static void InsertIntoScanOrder(unsigned int set_index)
{
    RemoveFromScanOrder(set_index);
    std::erase_if(g_region_set_scan_order,
                  [](unsigned int index) { return index >= g_region_sets.size(); });
    const auto& set = g_region_sets[set_index];
    const auto key = std::pair{set.order, set.sequence};
    const auto position = std::ranges::upper_bound(
        g_region_set_scan_order, key, {}, [](unsigned int index) {
            return std::pair{g_region_sets[index].order, g_region_sets[index].sequence};
        });
    g_region_set_scan_order.insert(position, set_index);
}

// FUNCTION: WIZ8 0x004f2880
unsigned int CreateRegionSet(int order)
{
    unsigned int set_index = 0;
    while (!g_reusable_region_set_indices.empty()) {
        const auto candidate = g_reusable_region_set_indices.back();
        g_reusable_region_set_indices.pop_back();
        if (candidate < kFirstDynamicRegionSetIndex || candidate >= g_region_sets.size()) {
            continue;
        }
        set_index = candidate;
        break;
    }
    if (set_index == 0) {
        g_region_sets.emplace_back();
        set_index = static_cast<unsigned int>(g_region_sets.size() - 1);
    }
    auto& set = g_region_sets[set_index];
    set = W8RegionSet{};
    set.order = order;
    set.sequence = ++g_region_set_sequence;
    InsertIntoScanOrder(set_index);
    return set_index;
}

static void ReleaseRegionSetSlot(unsigned int region_set_index)
{
    if (region_set_index < kFirstDynamicRegionSetIndex ||
        region_set_index >= g_region_sets.size() || region_set_index == g_control_region_set) {
        return;
    }
    RemoveFromScanOrder(region_set_index);
    g_region_sets[region_set_index] = W8RegionSet{};
    if (std::ranges::find(g_reusable_region_set_indices, region_set_index) ==
        g_reusable_region_set_indices.end()) {
        g_reusable_region_set_indices.push_back(region_set_index);
    }
}

void DestroyRegionSet(unsigned int region_set_index)
{
    if (region_set_index < kFirstDynamicRegionSetIndex ||
        region_set_index >= g_region_sets.size() || region_set_index == g_control_region_set) {
        return;
    }

    g_region_sets.at(region_set_index).enabled = 0;
    const auto active_regions = g_region_sets[region_set_index].Regions();
    const std::vector<unsigned int> region_indices(active_regions.begin(), active_regions.end());
    for (const auto region_index : region_indices) {
        RemoveRegionFromSet(region_set_index, region_index);
    }
    ReleaseRegionSetSlot(region_set_index);
}

unsigned int ControlRegionSetIndex(void)
{
    if (g_control_region_set == 0) {
        g_control_region_set = CreateRegionSet();
    }
    g_region_sets.at(g_control_region_set).enabled = 1;
    return g_control_region_set;
}

std::size_t LiveRegionSetCount(void)
{
    return g_region_set_scan_order.size();
}

std::size_t LiveDynamicRegionCount(void)
{
    std::size_t count = 0;
    for (std::size_t index = kFirstDynamicRegionIndex; index < g_regions.size(); ++index) {
        const bool registered = std::ranges::any_of(
            g_region_sets, [index](const W8RegionSet& set) {
                const auto regions = set.Regions();
                return std::ranges::find(regions, static_cast<unsigned int>(index)) !=
                       regions.end();
            });
        if (registered) ++count;
    }
    return count;
}
// FUNCTION: WIZ8 0x004f28e0
void ResetRegionSet(unsigned int region_set_index)
{
    g_region_sets.at(region_set_index).Reset();
}

unsigned int W8RegionSet::AddRegion(int priority)
{
    unsigned int region_index;
    if (active_count == region_ids.size()) {
        // Reserve first so a failed allocation cannot orphan a region.
        if (region_ids.size() == region_ids.capacity())
            region_ids.reserve(std::max(size_t{8}, region_ids.capacity() * 2));
        if (!g_reusable_region_indices.empty()) {
            region_index = g_reusable_region_indices.back();
            g_reusable_region_indices.pop_back();
            const auto generation = g_regions[region_index].binding_generation + 1;
            g_regions[region_index] = g_regions.front();
            g_regions[region_index].binding_generation = generation;
        } else {
            region_index = static_cast<unsigned int>(g_regions.size());
            g_regions.push_back(g_regions.front());
            ++g_regions.back().binding_generation;
        }
        region_ids.push_back(region_index);
    } else {
        region_index = region_ids[active_count];
    }
    g_regions[region_index].priority = priority;
    auto insertion_position = active_count;
    for (size_t position = 0; position < active_count; ++position) {
        if (g_regions[region_ids[position]].priority < priority) {
            insertion_position = position;
            break;
        }
    }
    std::rotate(region_ids.begin() + insertion_position, region_ids.begin() + active_count,
                region_ids.begin() + active_count + 1);
    ++active_count;
    return region_index;
}

bool W8RegionSet::RemoveRegion(unsigned int region_index)
{
    const auto found = std::ranges::find(region_ids, region_index);
    if (found == region_ids.end()) return false;
    const auto position = static_cast<size_t>(found - region_ids.begin());
    if (position < active_count) --active_count;
    region_ids.erase(found);
    return true;
}

void W8RegionSet::AttachRegion(unsigned int region_index)
{
    if (region_index >= g_regions.size()) {
        throw std::out_of_range("region index out of range");
    }
    if (std::ranges::find(region_ids.begin(), region_ids.begin() + active_count, region_index) !=
        region_ids.begin() + active_count) {
        return;
    }

    const auto priority = g_regions[region_index].priority;
    auto insertion_position = active_count;
    for (std::size_t position = 0; position < active_count; ++position) {
        if (g_regions[region_ids[position]].priority < priority) {
            insertion_position = position;
            break;
        }
    }
    if (active_count == region_ids.size()) {
        region_ids.push_back(region_index);
    } else {
        region_ids[active_count] = region_index;
    }
    std::rotate(region_ids.begin() + insertion_position, region_ids.begin() + active_count,
                region_ids.begin() + active_count + 1);
    ++active_count;
}

// FUNCTION: WIZ8 0x004f2920
unsigned int AddRegionToSet(unsigned int region_set_index, int priority)
{
    return g_region_sets.at(region_set_index).AddRegion(priority);
}

OwnedRegion::OwnedRegion(unsigned int set, int priority, W8RegionHandler& handler)
    : set_(set), index_(AddRegionToSet(set, priority))
{
    auto& owned_region = g_regions[index_];
    owned_region.handler = &handler;
    generation_ = ++owned_region.binding_generation;
}

OwnedRegion::OwnedRegion(unsigned int set, const RegionTemplate& entry, int priority)
    : set_(set), index_(AddRegionToSet(set, priority))
{
    auto& owned_region = g_regions[index_];
    owned_region.flags = entry.flags;
    owned_region.x1 = entry.x1;
    owned_region.y1 = entry.y1;
    owned_region.x2 = entry.x2;
    owned_region.y2 = entry.y2;
    owned_region.callback = entry.callback;
    owned_region.callback_id = entry.callback_id;
    owned_region.help_enabled = entry.help_enabled;
    owned_region.help_text_id = entry.help_text_id;
    owned_region.unknown_13 = 0;
    owned_region.help_text.clear();
    owned_region.text_field = false;
    owned_region.handler = nullptr;
    generation_ = ++owned_region.binding_generation;
}

OwnedRegionSet::OwnedRegionSet(std::span<const RegionTemplate> entries, int order)
    : set_(CreateRegionSet(order))
{
    try {
        regions_.reserve(entries.size());
        for (const auto& entry : entries) {
            regions_.emplace_back(set_, entry);
        }
    } catch (...) {
        regions_.clear();
        DestroyRegionSet(set_);
        set_ = 0;
        throw;
    }
}

OwnedRegionSet::~OwnedRegionSet()
{
    if (set_ == 0) return;
    Disable();
    regions_.clear();
    DestroyRegionSet(set_);
}

void OwnedRegionSet::Enable()
{
    if (set_ != 0) g_region_sets.at(set_).enabled = 1;
}

void OwnedRegionSet::Disable()
{
    if (set_ != 0) g_region_sets.at(set_).enabled = 0;
}

void OwnedRegionSet::EnableInput()
{
    if (set_ != 0) g_region_sets.at(set_).SetInputEnabled(true);
}

void OwnedRegionSet::DisableInput()
{
    if (set_ != 0) g_region_sets.at(set_).SetInputEnabled(false);
}

bool OwnedRegionSet::IsEnabled() const
{
    return set_ != 0 && set_ < g_region_sets.size() && g_region_sets[set_].enabled != 0;
}

void ScreenRegionSet::Create()
{
    if (!set_) {
        set_.emplace(templates_, order_);
    }
}

void ScreenRegionSet::Destroy()
{
    set_.reset();
}

void ScreenRegionSet::Enable()
{
    if (set_) {
        set_->Enable();
    }
}

void ScreenRegionSet::Disable()
{
    if (set_) {
        set_->Disable();
    }
}

void ScreenRegionSet::EnableInput()
{
    if (set_) {
        set_->EnableInput();
    }
}

void ScreenRegionSet::DisableInput()
{
    if (set_) {
        set_->DisableInput();
    }
}

RegionSetView::RegionSetView(const OwnedRegionSet& source, std::size_t first, std::size_t count,
                             int order)
{
    if (first > source.size() || count > source.size() - first) {
        throw std::out_of_range("region set view range out of bounds");
    }
    set_ = CreateRegionSet(order);
    try {
        auto& view = g_region_sets.at(set_);
        for (std::size_t index = first; index < first + count; ++index) {
            view.AttachRegion(source.index(index));
        }
    } catch (...) {
        ReleaseRegionSetSlot(set_);
        set_ = 0;
        throw;
    }
}

RegionSetView::~RegionSetView()
{
    if (set_ == 0) return;
    Disable();
    ReleaseRegionSetSlot(set_);
}

void RegionSetView::Enable()
{
    if (set_ != 0) g_region_sets.at(set_).enabled = 1;
}

void RegionSetView::Disable()
{
    if (set_ != 0) g_region_sets.at(set_).enabled = 0;
}

void RegionSetView::EnableInput()
{
    if (set_ != 0) g_region_sets.at(set_).SetInputEnabled(true);
}

void RegionSetView::DisableInput()
{
    if (set_ != 0) g_region_sets.at(set_).SetInputEnabled(false);
}

bool RegionSetView::IsEnabled() const
{
    return set_ != 0 && set_ < g_region_sets.size() && g_region_sets[set_].enabled != 0;
}

OwnedRegion::~OwnedRegion()
{
    reset();
}

OwnedRegion::OwnedRegion(OwnedRegion&& other) noexcept
    : set_(other.set_), index_(other.index_), generation_(other.generation_)
{
    other.set_ = 0;
    other.index_ = 0;
    other.generation_ = 0;
}

OwnedRegion& OwnedRegion::operator=(OwnedRegion&& other) noexcept
{
    if (this == &other) return *this;
    reset();
    set_ = other.set_;
    index_ = other.index_;
    generation_ = other.generation_;
    other.set_ = 0;
    other.index_ = 0;
    other.generation_ = 0;
    return *this;
}

OwnedRegion::operator bool() const
{
    return index_ != 0 && index_ < g_regions.size() &&
           g_regions[index_].binding_generation == generation_;
}

W8Region& OwnedRegion::region()
{
    if (!*this) throw std::logic_error("Accessing an unbound owned region");
    return g_regions[index_];
}

const W8Region& OwnedRegion::region() const
{
    if (!*this) throw std::logic_error("Accessing an unbound owned region");
    return g_regions[index_];
}

W8Region& OwnedRegionSet::operator[](std::size_t index)
{
    const auto region = regions_.at(index).index();
    if (region == 0) throw std::logic_error("Accessing an unowned region slot");
    return g_regions.at(region);
}

void OwnedRegion::reset()
{
    if (*this) RemoveRegionFromSet(set_, index_);
    set_ = 0;
    index_ = 0;
    generation_ = 0;
}

void RemoveRegionFromSet(unsigned int region_set_index, unsigned int region_index)
{
    if (!g_region_sets.at(region_set_index).RemoveRegion(region_index) ||
        region_index < kFirstDynamicRegionIndex || region_index >= g_regions.size()) {
        return;
    }
    auto& region = g_regions[region_index];
    if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
    region.flags &= ~(W8_REGION_MOUSE_STATE_MASK | W8_REGION_HELP_SHOWN);
    const auto generation = region.binding_generation + 1;
    region = g_regions.front();
    region.binding_generation = generation;
    g_reusable_region_indices.push_back(region_index);
    if (g_current_region_index == region_index) g_current_region_index = 0;
    if (g_captured_region_index == region_index) g_captured_region_index = 0;
    if (g_hover_region_index == region_index) g_hover_region_index = 0;
}

void UnbindWidgetRegion(unsigned int region_index, W8Widget* widget, OwnedRegion* owner)
{
    if (region_index >= g_regions.size()) return;
    auto& region = g_regions[region_index];
    if (region.handler != widget) return;

    g_regions[region_index].SetInputEnabled(false);
    if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
    region.flags &= ~(W8_REGION_MOUSE_STATE_MASK | W8_REGION_HELP_SHOWN);
    region.handler = nullptr;
    region.help_enabled = false;
    region.help_text_id = -1;
    region.help_text.clear();
    ++region.binding_generation;
    if (owner && owner->index_ == region_index) {
        owner->generation_ = region.binding_generation;
    }
    if (g_captured_region_index == region_index) g_captured_region_index = 0;
    if (g_current_region_index == region_index) g_current_region_index = 0;
    if (g_hover_region_index == region_index) g_hover_region_index = 0;
}

// FUNCTION: WIZ8 0x004f29c0
void SetRegionCallback(unsigned int region_index, W8RegionCallback callback,
                       unsigned short callback_id)
{
    if (region_index >= g_regions.size()) {
        srAssertFail("uiRegionIndex < guiRegionCount",
                     "C:\\Projects\\Wizardry 8\\Local Code\\RegionManager.cpp", 0x4df, 0);
    }
    ++g_regions[region_index].binding_generation;
    g_regions[region_index].callback = callback;
    g_regions[region_index].callback_id = callback_id;
}

/* Send the current hot region a mouse-leave transition at the live cursor
   position, then relinquish its help and hover state. */
// FUNCTION: WIZ8 0x004f2a80
void ClearHotRegion(void)
{
    SDL_Point mouse;
    SGPMouseGetPos(&mouse);
    const auto event = SyntheticMouseMove(mouse);

    if (g_current_region_index != 0) {
        W8Region* region = &g_regions[g_current_region_index];
        unsigned int mode = region->flags & W8_REGION_MODE_MASK;
        if (mode == 1 || mode == 2) {
            if (!LeaveRegion(*region, event)) return;
            g_current_region_index = 0;
        }
    }
}

/* Drops every region back to its resting state. The three tracked regions are
   released first - each only if it still carries bit 0x200 - then every region
   set is disabled and every region keeps only its low two flag bits. The three
   trackers are cleared last, and the fourth field is reseeded from the
   settings word rather than zeroed. */
// FUNCTION: WIZ8 0x004f1240
void ResetRegions(void)
{
    unsigned int index;

    index = g_current_region_index;
    if (g_current_region_index != 0 &&
        (g_regions[g_current_region_index].flags & W8_REGION_HELP_SHOWN) != 0) {
        VideoRemoveToolTip();
        g_regions[index].flags &= ~W8_REGION_HELP_SHOWN;
    }
    index = g_hover_region_index;
    if (g_hover_region_index != 0 &&
        (g_regions[g_hover_region_index].flags & W8_REGION_HELP_SHOWN) != 0) {
        VideoRemoveToolTip();
        g_regions[index].flags &= ~W8_REGION_HELP_SHOWN;
    }
    index = g_captured_region_index;
    if (g_captured_region_index != 0 &&
        (g_regions[g_captured_region_index].flags & W8_REGION_HELP_SHOWN) != 0) {
        VideoRemoveToolTip();
        g_regions[index].flags &= ~W8_REGION_HELP_SHOWN;
    }
    for (std::size_t set_index = 0; set_index < g_region_sets.size(); ++set_index) {
        auto& set = g_region_sets[set_index];
        const bool control_set = g_control_region_set != 0 && set_index == g_control_region_set;
        if (!control_set) set.enabled = 0;
        for (const auto region_index : set.Regions()) {
            auto& region = g_regions[region_index];
            if (control_set) {
                region.flags &= ~W8_REGION_MOUSE_STATE_MASK;
            } else {
                region.flags &= (W8_REGION_RECTANGLE | W8_REGION_CIRCLE);
            }
        }
    }
    g_current_region_index = 0;
    g_hover_region_index = 0;
    g_captured_region_index = 0;
    SetRegionHelpForceEnabled(false);
    g_region_help_delay = static_cast<unsigned short>(g_settings.tooltip_delay_ms);
}

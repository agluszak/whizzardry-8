/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09,
   2026-10-11. Distributed under the accompanying SFI Source Code license agreement. */

#include "wiz8/platform/input.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>

#include "wiz8/compat/native.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/unicode.h"

namespace {
constexpr std::uint64_t button_repeat_timeout = 250;
constexpr std::uint64_t button_repeat_interval = 50;

InputState state;
SDL_Window* input_window = nullptr;
bool focused = false;
bool text_input_requested = false;
float wheel_remainder = 0;

void release_held_input()
{
    state.keys.clear();
    state.modifiers = 0;
    state.left_button = state.right_button = false;
    state.left_repeat_at = state.right_repeat_at = 0;
    wheel_remainder = 0;
}

/* The only window <-> 640x480 design-space conversion. */
SDL_Point window_to_game(float x, float y)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(input_window, &width, &height) || width <= 0 || height <= 0)
        return state.mouse;
    return {std::clamp(int(std::floor(x * 640 / width)), 0, 639),
            std::clamp(int(std::floor(y * 480 / height)), 0, 479)};
}

SDL_FPoint game_to_window(SDL_Point point)
{
    int width = 0, height = 0;
    SDL_GetWindowSize(input_window, &width, &height);
    return {float(point.x * width) / 640, float(point.y * height) / 480};
}

void queue_button_repeat(bool held, std::uint64_t& repeat_at, InputEventType type,
                         std::uint64_t now)
{
    if (!held) {
        repeat_at = 0;
        return;
    }
    if (repeat_at == 0 || repeat_at > now) return;
    PostInputEvent({.type = type, .position = state.mouse});
    repeat_at = now + button_repeat_interval;
}

void set_modifiers(SDL_Keymod mod)
{
    state.modifiers = ((mod & SDL_KMOD_SHIFT) ? SHIFT_DOWN : 0) |
                      ((mod & SDL_KMOD_CTRL) ? CTRL_DOWN : 0) |
                      ((mod & SDL_KMOD_ALT) ? ALT_DOWN : 0);
}

void handle_key(const SDL_KeyboardEvent& event)
{
    const bool down = event.type == SDL_EVENT_KEY_DOWN;
    set_modifiers(event.mod);
    // Keep keypad identity independent of Num Lock, as retail bindings do.
    const SDL_Keycode key =
        event.scancode >= SDL_SCANCODE_KP_DIVIDE && event.scancode <= SDL_SCANCODE_KP_PERIOD
            ? SDL_GetKeyFromScancode(event.scancode, SDL_KMOD_NONE, false)
            : event.key;
    if (key == SDLK_UNKNOWN || (key >= SDLK_LCTRL && key <= SDLK_RGUI)) return;
    if (key == SDLK_PRINTSCREEN) {
        if (!down) {
            g_screenshot_pending = true;
        }
        return;
    }
    if (down) {
        PostInputEvent(
            {.type = state.keys.contains(key) ? InputEventType::KeyRepeat : InputEventType::KeyDown,
             .key = key,
             .position = state.mouse});
        state.keys.insert(key);
    } else if (state.keys.contains(key)) {
        state.keys.erase(key);
        PostInputEvent({.type = InputEventType::KeyUp, .key = key, .position = state.mouse});
    }
}

void handle_button(const SDL_MouseButtonEvent& event)
{
    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    state.mouse = window_to_game(event.x, event.y);
    if (event.button == SDL_BUTTON_LEFT) {
        state.left_button = down;
        PostInputEvent(
            {.type = down ? InputEventType::LeftButtonDown : InputEventType::LeftButtonUp,
             .position = state.mouse,
             .clicks = event.clicks});
    } else if (event.button == SDL_BUTTON_RIGHT) {
        state.right_button = down;
        PostInputEvent(
            {.type = down ? InputEventType::RightButtonDown : InputEventType::RightButtonUp,
             .position = state.mouse,
             .clicks = event.clicks});
    }
}

void handle_wheel(const SDL_MouseWheelEvent& event)
{
    state.mouse = window_to_game(event.mouse_x, event.mouse_y);
    wheel_remainder += (event.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f) * event.y;
    while (std::abs(wheel_remainder) >= 1) {
        const int detent = wheel_remainder > 0 ? 1 : -1;
        wheel_remainder -= float(detent);
        PostInputEvent(
            {.type = InputEventType::MouseWheel, .position = state.mouse, .wheel = detent});
    }
}

} // namespace

InputState& GetInputState()
{
    return state;
}

void SetInputWindow(SDL_Window* window)
{
    state = {};
    release_held_input();
    input_window = window;
    focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
    state.mouse = {};
    SetNativeTextInputEnabled(text_input_requested);
}

void HandleInputEvent(const SDL_Event& event)
{
    if (!input_window || SDL_GetWindowFromEvent(&event) != input_window) return;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        focused = true;
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        focused = false;
        release_held_input();
        return;
    }
    if (!focused) return;
    switch (event.type) {
    case SDL_EVENT_TEXT_INPUT: {
        if (!text_input_requested) break;
        const std::string_view text(event.text.text);
        std::size_t offset = 0;
        while (offset < text.size())
            PostInputEvent({.type = InputEventType::Text,
                            .text = wiz8::text::next(text, offset),
                            .position = state.mouse});
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handle_key(event.key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        state.mouse = window_to_game(event.motion.x, event.motion.y);
        if (state.mouse_move_events)
            PostInputEvent({.type = InputEventType::MouseMove, .position = state.mouse});
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        handle_button(event.button);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        handle_wheel(event.wheel);
        break;
    default:
        break;
    }
}

void PostInputEvent(InputEvent event)
{
    event.timestamp = w8_get_ticks();
    event.modifiers = state.modifiers;
    event.buttons =
        (state.left_button ? SDL_BUTTON_LMASK : 0) | (state.right_button ? SDL_BUTTON_RMASK : 0);
    switch (event.type) {
    case InputEventType::LeftButtonDown:
        state.left_repeat_at = event.timestamp + button_repeat_timeout;
        break;
    case InputEventType::RightButtonDown:
        state.right_repeat_at = event.timestamp + button_repeat_timeout;
        break;
    case InputEventType::LeftButtonUp:
        state.left_repeat_at = 0;
        break;
    case InputEventType::RightButtonUp:
        state.right_repeat_at = 0;
        break;
    default:
        break;
    }
    state.events.push_back(event);
}

std::optional<InputEvent> PollInputEvent()
{
    const std::uint64_t now = w8_get_ticks();
    queue_button_repeat(state.left_button, state.left_repeat_at, InputEventType::LeftButtonRepeat,
                        now);
    queue_button_repeat(state.right_button, state.right_repeat_at,
                        InputEventType::RightButtonRepeat, now);
    if (state.events.empty()) return std::nullopt;
    InputEvent event = state.events.front();
    state.events.pop_front();
    return event;
}

bool WarpGameMouse(SDL_Window* window, int x, int y)
{
    if (!window || window != input_window) return false;
    const SDL_Point point{std::clamp(x, 0, 639), std::clamp(y, 0, 479)};
    const SDL_FPoint native = game_to_window(point);
    SDL_WarpMouseInWindow(window, native.x, native.y);
    state.mouse = point;
    return true;
}

void FreeMouseCursor()
{
    if (input_window) SDL_SetWindowMouseRect(input_window, nullptr);
}

void SetNativeTextInputEnabled(bool enabled)
{
    text_input_requested = enabled;
    if (input_window) {
        if (enabled)
            SDL_StartTextInput(input_window);
        else
            SDL_StopTextInput(input_window);
    }
}

bool NativeTextInputEnabled()
{
    return text_input_requested;
}

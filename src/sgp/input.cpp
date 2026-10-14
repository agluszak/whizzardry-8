/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09,
   2026-10-11. Distributed under the accompanying SFI Source Code license agreement. */

#include "input.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
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
constexpr std::size_t max_input_events = 256;

InputState state;
std::deque<InputEvent> events;
SDL_Window* input_window = nullptr;
bool focused = false;
bool text_input_requested = false;
float wheel_remainder = 0;

void release_held_input()
{
    state.keys.reset();
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

void push(InputEvent event)
{
    if (events.size() == max_input_events) return;
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
    events.push_back(event);
}

void queue_button_repeat(bool held, std::uint64_t& repeat_at, InputEventType type,
                         std::uint64_t now)
{
    if (!held) {
        repeat_at = 0;
        return;
    }
    if (repeat_at == 0 || repeat_at > now) return;
    push({.type = type, .position = state.mouse});
    repeat_at = now + button_repeat_interval;
}

void set_modifiers(SDL_Keymod mod)
{
    state.modifiers = ((mod & SDL_KMOD_SHIFT) ? SHIFT_DOWN : 0) |
                      ((mod & SDL_KMOD_CTRL) ? CTRL_DOWN : 0) |
                      ((mod & SDL_KMOD_ALT) ? ALT_DOWN : 0);
    state.keys[VK_SHIFT] = (state.modifiers & SHIFT_DOWN) != 0;
    state.keys[VK_CONTROL] = (state.modifiers & CTRL_DOWN) != 0;
    state.keys[VK_MENU] = (state.modifiers & ALT_DOWN) != 0;
}

/* The only SDL key -> game virtual-key mapping. Keypad keys map by scancode, so
   they keep their numpad codes whether or not Num Lock is on. */
} // namespace

std::uint16_t GameKeyFromSdl(SDL_Scancode scancode, SDL_Keycode key)
{
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
        return std::uint16_t(VK_NUMPAD0 + 1 + (scancode - SDL_SCANCODE_KP_1));
    switch (scancode) {
    case SDL_SCANCODE_KP_0:
        return VK_NUMPAD0;
    case SDL_SCANCODE_KP_MULTIPLY:
        return 0x6a;
    case SDL_SCANCODE_KP_PLUS:
        return 0x6b;
    case SDL_SCANCODE_KP_ENTER:
        return VK_NUMPAD_ENTER;
    case SDL_SCANCODE_KP_MINUS:
        return 0x6d;
    case SDL_SCANCODE_KP_PERIOD:
        return 0x6e;
    case SDL_SCANCODE_KP_DIVIDE:
        return 0x6f;
    default:
        break;
    }
    if (key >= SDLK_A && key <= SDLK_Z) return std::uint16_t(key - SDLK_A + 'A');
    if (key >= SDLK_0 && key <= SDLK_9) return std::uint16_t(key);
    if (key >= SDLK_F1 && key <= SDLK_F12) return std::uint16_t(key - SDLK_F1 + VK_F1);
    if (key >= SDLK_F13 && key <= SDLK_F24) return std::uint16_t(key - SDLK_F13 + VK_F12 + 1);
    switch (key) {
    case SDLK_BACKSPACE:
        return VK_BACK;
    case SDLK_TAB:
        return VK_TAB;
    case SDLK_RETURN:
        return VK_RETURN;
    case SDLK_ESCAPE:
        return VK_ESCAPE;
    case SDLK_SPACE:
        return VK_SPACE;
    case SDLK_CAPSLOCK:
        return VK_CAPITAL;
    case SDLK_PAUSE:
        return VK_PAUSE;
    case SDLK_PRINTSCREEN:
        return VK_SNAPSHOT;
    case SDLK_NUMLOCKCLEAR:
        return 0x90;
    case SDLK_SCROLLLOCK:
        return 0x91;
    case SDLK_SEMICOLON:
        return 0xba;
    case SDLK_EQUALS:
        return 0xbb;
    case SDLK_COMMA:
        return 0xbc;
    case SDLK_MINUS:
        return 0xbd;
    case SDLK_PERIOD:
        return 0xbe;
    case SDLK_SLASH:
        return 0xbf;
    case SDLK_GRAVE:
        return 0xc0;
    case SDLK_LEFTBRACKET:
        return 0xdb;
    case SDLK_BACKSLASH:
        return 0xdc;
    case SDLK_RIGHTBRACKET:
        return 0xdd;
    case SDLK_APOSTROPHE:
        return 0xde;
    case SDLK_PAGEUP:
        return VK_PRIOR;
    case SDLK_PAGEDOWN:
        return VK_NEXT;
    case SDLK_END:
        return VK_END;
    case SDLK_HOME:
        return VK_HOME;
    case SDLK_LEFT:
        return VK_LEFT;
    case SDLK_UP:
        return VK_UP;
    case SDLK_RIGHT:
        return VK_RIGHT;
    case SDLK_DOWN:
        return VK_DOWN;
    case SDLK_INSERT:
        return VK_INSERT;
    case SDLK_DELETE:
        return VK_DELETE;
    default:
        return 0;
    }
}

namespace {
void handle_key(const SDL_KeyboardEvent& event)
{
    const bool down = event.type == SDL_EVENT_KEY_DOWN;
    set_modifiers(event.mod);
    const std::uint16_t key = GameKeyFromSdl(event.scancode, event.key);
    if (key == 0) return;
    if (key == VK_SNAPSHOT) {
        if (!down) {
            if (state.modifiers & CTRL_DOWN)
                VideoCaptureToggle();
            else
                PrintScreen();
        }
        return;
    }
    if (down) {
        push({.type = state.keys[key] ? InputEventType::KeyRepeat : InputEventType::KeyDown,
              .key = key,
              .position = state.mouse});
        state.keys[key] = true;
        state.received = true;
    } else if (state.keys[key]) {
        state.keys[key] = false;
        push({.type = InputEventType::KeyUp, .key = key, .position = state.mouse});
    }
}

void handle_button(const SDL_MouseButtonEvent& event)
{
    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    state.mouse = window_to_game(event.x, event.y);
    state.received = true;
    if (event.button == SDL_BUTTON_LEFT) {
        state.left_button = down;
        push({.type = down ? InputEventType::LeftButtonDown : InputEventType::LeftButtonUp,
              .position = state.mouse,
              .clicks = event.clicks});
        /* SDL applies the platform's double-click timing; the game wants its
           double-click event on the second release. */
        if (!down && event.clicks != 0 && (event.clicks & 1) == 0)
            push({.type = InputEventType::LeftButtonDoubleClick,
                  .position = state.mouse,
                  .clicks = event.clicks});
    } else if (event.button == SDL_BUTTON_RIGHT) {
        state.right_button = down;
        push({.type = down ? InputEventType::RightButtonDown : InputEventType::RightButtonUp,
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
        push({.type = InputEventType::MouseWheel, .position = state.mouse, .wheel = detent});
    }
}

struct KeyName {
    std::string_view name;
    std::uint16_t key;
};

// GLOBAL: WIZ8 0x00647770
constexpr KeyName key_names[] = {
    {"F1", 112},        {"F2", 113},        {"F3", 114},        {"F4", 115},
    {"F5", 116},        {"F6", 117},        {"F7", 118},        {"F8", 119},
    {"F9", 120},        {"F10", 121},       {"F11", 122},       {"F12", 123},
    {"ESC", 27},        {"TAB", 9},         {"PAUSE", 19},      {"NUM_LOCK", 144},
    {"BACKSPACE", 8},   {"INSERT", 45},     {"DEL", 46},        {"KEY_END", 35},
    {"PGDN", 34},       {"PGUP", 33},       {"HOME", 36},       {"ENTER", 13},
    {"SPACE", 32},      {"DNARROW", 40},    {"LEFTARROW", 37},  {"RIGHTARROW", 39},
    {"UPARROW", 38},    {"NUM_0", 96},      {"NUM_1", 97},      {"NUM_2", 98},
    {"NUM_3", 99},      {"NUM_4", 100},     {"NUM_5", 101},     {"NUM_6", 102},
    {"NUM_7", 103},     {"NUM_8", 104},     {"NUM_9", 105},     {"NUM_TIMES", 106},
    {"NUM_PLUS", 107},  {"NUM_ENTER", 108}, {"NUM_MINUS", 109}, {"NUM_PERIOD", 110},
    {"NUM_SLASH", 111}, {"SCRL_LOCK", 145}, {"/", 191},         {"-", 189},
    {".", 190}};

struct KeyCharacters {
    std::uint16_t key;
    char plain;
    char shifted;
};

/* US layout, matching retail's 0x005ffc3c table for every non-letter key. */
constexpr KeyCharacters key_characters[] = {
    {0x6a, '*', '*'}, {0x6b, '+', '+'},  {0x6d, '-', '-'}, {0x6e, '.', '.'},
    {0x6f, '/', '/'}, {0xba, ';', ':'},  {0xbb, '=', '+'}, {0xbc, ',', '<'},
    {0xbd, '-', '_'}, {0xbe, '.', '>'},  {0xbf, '/', '?'}, {0xc0, '`', '~'},
    {0xdb, '[', '{'}, {0xdc, '\\', '|'}, {0xdd, ']', '}'}, {0xde, '\'', '"'}};

} // namespace

InputState& GetInputState()
{
    return state;
}

// FUNCTION: WIZ8 0x00401ea0
bool InitializeInputManager()
{
    release_held_input();
    events.clear();
    state.mouse_move_events = false;
    state.received = false;
    state.mouse = {320, 240};
    return true;
}

void SetInputWindow(SDL_Window* window)
{
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
            push({.type = InputEventType::Text,
                  .text = wiz8::text::next(text, offset),
                  .position = state.mouse});
        state.received = true;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handle_key(event.key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        state.mouse = window_to_game(event.motion.x, event.motion.y);
        state.received = true;
        if (state.mouse_move_events)
            push({.type = InputEventType::MouseMove, .position = state.mouse});
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

// FUNCTION: WIZ8 0x00401f90
void QueueEvent(InputEventType type, SDL_Point position, std::uint16_t key)
{
    push({.type = type, .key = key, .position = position});
}

// FUNCTION: WIZ8 0x00402140
bool DequeueEvent(InputEvent* event)
{
    const std::uint64_t now = w8_get_ticks();
    queue_button_repeat(state.left_button, state.left_repeat_at, InputEventType::LeftButtonRepeat,
                        now);
    queue_button_repeat(state.right_button, state.right_repeat_at,
                        InputEventType::RightButtonRepeat, now);
    if (events.empty()) return false;
    *event = events.front();
    events.pop_front();
    return true;
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

// FUNCTION: WIZ8 0x00402750
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

std::uint16_t KeyFromBindingName(std::string_view name)
{
    for (const KeyName& entry : key_names)
        if (entry.name == name) return entry.key;
    return name.size() == 1 ? TranslateCharacterToKey(static_cast<unsigned char>(name[0])) : 0;
}

// FUNCTION: WIZ8 0x00402780
std::uint16_t TranslateKeyToCharacter(std::uint16_t key, std::uint16_t modifiers)
{
    if (modifiers & (CTRL_DOWN | ALT_DOWN)) return 0;
    const bool shift = (modifiers & SHIFT_DOWN) != 0;
    if (key == VK_SPACE) return ' ';
    if (key >= 'A' && key <= 'Z') return shift ? key : std::uint16_t(key + ('a' - 'A'));
    if (key >= '0' && key <= '9') return shift ? std::uint16_t(")!@#$%^&*("[key - '0']) : key;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD0 + 9) return std::uint16_t('0' + key - VK_NUMPAD0);
    for (const KeyCharacters& entry : key_characters)
        if (entry.key == key) return std::uint16_t(shift ? entry.shifted : entry.plain);
    return 0;
}

// FUNCTION: WIZ8 0x004027C0
std::uint16_t TranslateCharacterToKey(std::uint16_t character)
{
    if (character == 0) return 0;
    for (std::uint16_t modifiers : {std::uint16_t(0), SHIFT_DOWN})
        for (std::uint16_t key = 0; key < 256; ++key)
            if (TranslateKeyToCharacter(key, modifiers) == character) return key;
    return 0;
}

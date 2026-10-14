#ifndef __INPUT_
#define __INPUT_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-11.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>

/* The SDL3 input pipeline: SDL events update one InputState and queue typed
   InputEvents in the game's 640x480 design space. */

/* Modifier bits. Key bindings store these values in save files. */
inline constexpr std::uint16_t SHIFT_DOWN = 0x01;
inline constexpr std::uint16_t CTRL_DOWN = 0x02;
inline constexpr std::uint16_t ALT_DOWN = 0x04;

/* Virtual-key codes: the game's keyboard vocabulary. Key bindings store these
   values in save files and MGSKeyboard.ini names them. */
inline constexpr std::uint16_t VK_BACK = 0x08;
inline constexpr std::uint16_t VK_TAB = 0x09;
inline constexpr std::uint16_t VK_RETURN = 0x0D;
inline constexpr std::uint16_t VK_SHIFT = 0x10;
inline constexpr std::uint16_t VK_CONTROL = 0x11;
inline constexpr std::uint16_t VK_MENU = 0x12;
inline constexpr std::uint16_t VK_PAUSE = 0x13;
inline constexpr std::uint16_t VK_CAPITAL = 0x14;
inline constexpr std::uint16_t VK_ESCAPE = 0x1B;
inline constexpr std::uint16_t VK_SPACE = 0x20;
inline constexpr std::uint16_t VK_PRIOR = 0x21;
inline constexpr std::uint16_t VK_NEXT = 0x22;
inline constexpr std::uint16_t VK_END = 0x23;
inline constexpr std::uint16_t VK_HOME = 0x24;
inline constexpr std::uint16_t VK_LEFT = 0x25;
inline constexpr std::uint16_t VK_UP = 0x26;
inline constexpr std::uint16_t VK_RIGHT = 0x27;
inline constexpr std::uint16_t VK_DOWN = 0x28;
inline constexpr std::uint16_t VK_SNAPSHOT = 0x2C;
inline constexpr std::uint16_t VK_INSERT = 0x2D;
inline constexpr std::uint16_t VK_DELETE = 0x2E;
inline constexpr std::uint16_t VK_NUMPAD0 = 0x60;
inline constexpr std::uint16_t VK_NUMPAD_ENTER = 0x6C;
inline constexpr std::uint16_t VK_F1 = 0x70;
inline constexpr std::uint16_t VK_F12 = 0x7B;

enum class InputEventType : std::uint8_t {
    KeyDown,
    KeyUp,
    KeyRepeat,
    LeftButtonDown,
    LeftButtonUp,
    LeftButtonDoubleClick,
    LeftButtonRepeat,
    RightButtonDown,
    RightButtonUp,
    RightButtonRepeat,
    MouseMove,
    MouseWheel,
    Text,
};

inline bool IsLeftButtonEvent(InputEventType type)
{
    return type == InputEventType::LeftButtonDown || type == InputEventType::LeftButtonUp ||
           type == InputEventType::LeftButtonDoubleClick || type == InputEventType::LeftButtonRepeat;
}

struct InputEvent {
    InputEventType type = InputEventType::MouseMove;
    std::uint64_t timestamp = 0;
    std::uint16_t modifiers = 0;
    std::uint16_t key = 0;
    char32_t text = 0;
    SDL_Point position{};
    int wheel = 0;
    SDL_MouseButtonFlags buttons = 0;
    std::uint8_t clicks = 0;

    bool IsKeyPress() const
    {
        return type == InputEventType::KeyDown || type == InputEventType::KeyRepeat;
    }
};

struct InputState {
    std::bitset<256> keys;
    std::uint16_t modifiers = 0;
    SDL_Point mouse{320, 240};
    bool left_button = false;
    bool right_button = false;
    std::uint64_t left_repeat_at = 0;
    std::uint64_t right_repeat_at = 0;
    bool mouse_move_events = false;
    bool received = false;
};

InputState& GetInputState();

bool InitializeInputManager();
void SetInputWindow(SDL_Window* window);
void HandleInputEvent(const SDL_Event& event);
void QueueEvent(InputEventType type, SDL_Point position, std::uint16_t key = 0);
bool DequeueEvent(InputEvent* event);

inline bool IsKeyHeld(std::uint16_t key)
{
    return key < 256 && GetInputState().keys[key];
}
inline bool IsLeftButtonHeld()
{
    return GetInputState().left_button;
}
inline bool IsRightButtonHeld()
{
    return GetInputState().right_button;
}
inline SDL_Point GetGameMousePosition()
{
    return GetInputState().mouse;
}
inline void SetMouseMoveEventsEnabled(bool enabled)
{
    GetInputState().mouse_move_events = enabled;
}

bool WarpGameMouse(SDL_Window* window, int x, int y);
void FreeMouseCursor();

void SetNativeTextInputEnabled(bool enabled);
bool NativeTextInputEnabled();

/* Key bindings: retail key names, and the US-layout key/character table. */
std::uint16_t KeyFromBindingName(std::string_view name);
std::uint16_t GameKeyFromSdl(SDL_Scancode scancode, SDL_Keycode key);
std::uint16_t TranslateKeyToCharacter(std::uint16_t key, std::uint16_t modifiers);
std::uint16_t TranslateCharacterToKey(std::uint16_t character);

/* Pointer button presses, releases and repeats (not moves, wheel or double-clicks). */
inline bool IsMouseButtonEvent(const InputEvent& event)
{
    switch (event.type) {
    case InputEventType::LeftButtonDown:
    case InputEventType::LeftButtonUp:
    case InputEventType::LeftButtonRepeat:
    case InputEventType::RightButtonDown:
    case InputEventType::RightButtonUp:
    case InputEventType::RightButtonRepeat:
        return true;
    default:
        return false;
    }
}

/* Key presses and committed text: the events a text field consumes. */
inline bool IsTypingEvent(const InputEvent& event)
{
    return event.type == InputEventType::KeyDown || event.type == InputEventType::KeyRepeat ||
           event.type == InputEventType::Text;
}

inline char32_t TypedCharacter(const InputEvent& event)
{
    return event.type == InputEventType::Text ? event.text : TranslateKeyToCharacter(event.key, event.modifiers);
}

#endif

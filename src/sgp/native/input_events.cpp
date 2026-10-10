#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/GameData.h"
#include <stdexcept>

#include "native/input_events.h"
#include "input.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
bool restore_pending = false;
SDL_Window* input_window = nullptr;
SDL_Keymod current_modifiers = SDL_KMOD_NONE;
POINT mouse_position{};
bool focused = false;
float wheel_remainder = 0;

void reset_held_input()
{
    std::memset(gfKeyState, 0, sizeof(gfKeyState));
    gfShiftState = gfCtrlState = gfAltState = 0;
    gfLeftButtonState = gfRightButtonState = FALSE;
    guiLeftButtonRepeatTimer = guiRightButtonRepeatTimer = 0;
    current_modifiers = SDL_KMOD_NONE;
    wheel_remainder = 0;
}
void set_mouse_position(float x, float y)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(input_window, &width, &height) || width <= 0 || height <= 0)
        return;
    mouse_position.x = std::clamp(int(std::floor(x * 640 / width)), 0, 639);
    mouse_position.y = std::clamp(int(std::floor(y * 480 / height)), 0, 479);
    gusMouseXPos = mouse_position.x;
    gusMouseYPos = mouse_position.y;
}
UINT32 packed_mouse_position()
{
    return (UINT32(mouse_position.y) << 16) | UINT32(mouse_position.x);
}
void set_modifiers(SDL_Keymod value)
{
    const SDL_Keymod masks[] = {SDL_KMOD_SHIFT, SDL_KMOD_CTRL, SDL_KMOD_ALT};
    const UINT keys[] = {VK_SHIFT, VK_CONTROL, VK_MENU};
    for (size_t i = 0; i < 3; ++i)
    {
        bool before = (current_modifiers & masks[i]) != 0, after = (value & masks[i]) != 0;
        if (before != after)
        {
            if (after)
                KeyDown(keys[i], 1);
            else
                KeyUp(keys[i], 0xc0000001u);
        }
    }
    current_modifiers = value;
}
UINT game_key(SDL_Keycode key, bool& extended)
{
    if (key >= SDLK_A && key <= SDLK_Z)
        return key - SDLK_A + 'A';
    if (key >= SDLK_0 && key <= SDLK_9)
        return key;
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return key - SDLK_F1 + 0x70;
    if (key >= SDLK_F13 && key <= SDLK_F24)
        return key - SDLK_F13 + 0x7c;
    switch (key)
    {
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
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return VK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return VK_CONTROL;
    case SDLK_LALT:
    case SDLK_RALT:
        return VK_MENU;
    case SDLK_CAPSLOCK:
        return 0x14;
    case SDLK_PAUSE:
        return 0x13;
    case SDLK_PRINTSCREEN:
        return 0x2c;
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
    case SDLK_KP_0:
        return 0x60;
    case SDLK_KP_1:
        return 0x61;
    case SDLK_KP_2:
        return 0x62;
    case SDLK_KP_3:
        return 0x63;
    case SDLK_KP_4:
        return 0x64;
    case SDLK_KP_5:
        return 0x65;
    case SDLK_KP_6:
        return 0x66;
    case SDLK_KP_7:
        return 0x67;
    case SDLK_KP_8:
        return 0x68;
    case SDLK_KP_9:
        return 0x69;
    case SDLK_KP_MULTIPLY:
        return 0x6a;
    case SDLK_KP_PLUS:
        return 0x6b;
    case SDLK_KP_MINUS:
        return 0x6d;
    case SDLK_KP_PERIOD:
        return 0x6e;
    case SDLK_KP_DIVIDE:
        extended = true;
        return 0x6f;
    case SDLK_KP_ENTER:
        extended = true;
        return VK_RETURN;
    case SDLK_PAGEUP:
        extended = true;
        return VK_PRIOR;
    case SDLK_PAGEDOWN:
        extended = true;
        return VK_NEXT;
    case SDLK_END:
        extended = true;
        return VK_END;
    case SDLK_HOME:
        extended = true;
        return VK_HOME;
    case SDLK_LEFT:
        extended = true;
        return VK_LEFT;
    case SDLK_UP:
        extended = true;
        return VK_UP;
    case SDLK_RIGHT:
        extended = true;
        return VK_RIGHT;
    case SDLK_DOWN:
        extended = true;
        return VK_DOWN;
    case SDLK_INSERT:
        extended = true;
        return VK_INSERT;
    case SDLK_DELETE:
        extended = true;
        return VK_DELETE;
    default:
        return 0;
    }
}

} // namespace

void SetInputWindow(SDL_Window* window)
{
    restore_pending = false;
    reset_held_input();
    input_window = window;
    focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
    mouse_position = {};
}
void GetGameMousePosition(POINT* point)
{
    if (point)
        *point = focused ? mouse_position : POINT{};
}
bool SetGameCursorRect(const RECT* rect)
{
    if (!input_window)
        return false;
    if (!rect)
        return SDL_SetWindowMouseRect(input_window, nullptr);
    if (!focused || rect->left < 0 || rect->top < 0 || rect->right <= rect->left ||
        rect->bottom <= rect->top || rect->right > 640 || rect->bottom > 480)
        return false;
    int width, height;
    if (!SDL_GetWindowSize(input_window, &width, &height))
        return false;
    int left = rect->left * width / 640, top = rect->top * height / 480;
    SDL_Rect native{left, top, rect->right * width / 640 - left,
                    rect->bottom * height / 480 - top};
    return SDL_SetWindowMouseRect(input_window, &native);
}
bool WarpGameMouse(SDL_Window* window, int x, int y)
{
    if (!window || window != input_window)
        return false;
    int width, height;
    if (!SDL_GetWindowSize(window, &width, &height))
        return false;
    x = std::clamp(x, 0, 639);
    y = std::clamp(y, 0, 479);
    SDL_WarpMouseInWindow(window, float(x * width) / 640, float(y * height) / 480);
    set_mouse_position(float(x * width) / 640, float(y * height) / 480);
    return true;
}
void HandleInputEvent(const SDL_Event& event)
{
    if (!input_window || SDL_GetWindowFromEvent(&event) != input_window)
        return;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        focused = true;
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        focused = false;
        reset_held_input();
        return;
    }
    if (!focused)
        return;
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        set_modifiers(event.key.mod);
        bool extended = false;
        UINT key = game_key(event.key.key, extended);
        if (!key || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)
            break;
        UINT32 flags = 1 | (extended ? 0x01000000u : 0);
        if (event.key.repeat)
            flags |= 0x40000000u;
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            KeyDown(key, flags);
            gfSGPInputReceived = TRUE;
        }
        else
            KeyUp(key, flags | 0xc0000000u);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        set_mouse_position(event.motion.x, event.motion.y);
        gfSGPInputReceived = TRUE;
        if (gfTrackMousePos)
            QueueEvent(MOUSE_POS, 0, packed_mouse_position());
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        set_mouse_position(event.button.x, event.button.y);
        bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            gfLeftButtonState = down;
            QueueEvent(down ? LEFT_BUTTON_DOWN : LEFT_BUTTON_UP, 0, packed_mouse_position());
        }
        else if (event.button.button == SDL_BUTTON_RIGHT)
        {
            gfRightButtonState = down;
            QueueEvent(down ? RIGHT_BUTTON_DOWN : RIGHT_BUTTON_UP, 0, packed_mouse_position());
        }
        gfSGPInputReceived = TRUE;
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        set_mouse_position(event.wheel.mouse_x, event.wheel.mouse_y);
        float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        wheel_remainder += direction * event.wheel.y;
        while (std::abs(wheel_remainder) >= 1)
        {
            int delta = wheel_remainder > 0 ? 120 : -120;
            wheel_remainder -= delta > 0 ? 1.f : -1.f;
            QueueEvent(MOUSE_WHEEL, UINT32(uint16_t(delta)) << 16, packed_mouse_position());
        }
        break;
    }
    }
}

// GLOBAL: WIZ8 0x006505a0
BOOLEAN gfLoadAtStartup = FALSE;
// GLOBAL: WIZ8 0x006505a1
BOOLEAN gfUsingBoundsChecker = FALSE;
// GLOBAL: WIZ8 0x006505a4
std::string gzStringDataOverride;
// GLOBAL: WIZ8 0x006505a8
BOOLEAN gfCapturingVideo = FALSE;
// GLOBAL: WIZ8 0x006f0630
BOOLEAN gfApplicationActive = FALSE;
// GLOBAL: WIZ8 0x006f0628
BOOLEAN gfProgramIsRunning = FALSE;
// GLOBAL: WIZ8 0x006505a9
BOOLEAN gfGameInitialized = FALSE;
// GLOBAL: WIZ8 0x006505ac
CHAR8 gzErrorMsg[2048] = "";
// GLOBAL: WIZ8 0x00650dac
BOOLEAN gfIgnoreMessages = FALSE;
// GLOBAL: WIZ8 0x005ff450
UINT8 gbPixelDepth = PIXEL_DEPTH;


// FUNCTION: WIZ8 0x00401920
[[noreturn]] void ShutdownWithErrorBox(const CHAR8* message)
{
    SDL_strlcpy(gzErrorMsg, message, sizeof(gzErrorMsg));
    gfIgnoreMessages = TRUE;
    throw std::runtime_error(gzErrorMsg);
}


void HandleGameEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT)
    {
        gfProgramIsRunning = FALSE;
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ghWindow &&
        SDL_GetWindowFromEvent(&event) == reinterpret_cast<SDL_Window*>(ghWindow))
    {
        gfProgramIsRunning = FALSE;
        return;
    }
    if (gfIgnoreMessages)
        return;
    HandleInputEvent(event);
    if (!ghWindow || SDL_GetWindowFromEvent(&event) != reinterpret_cast<SDL_Window*>(ghWindow))
        return;
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_RESIZED:
        if (event.window.data1 > 0 && event.window.data2 > 0)
            VideoResizeWindow();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (restore_pending)
        {
            if (!VideoInspectorIsEnabled())
            {
                RestoreVideoManager();
                RestoreVideoSurfaces();
            }
            MoveTimer(TIMER_RESUME);
            restore_pending = false;
        }
        gfApplicationActive = TRUE;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!VideoInspectorIsEnabled())
            SuspendVideoManager();
        MoveTimer(TIMER_SUSPEND);
        gfApplicationActive = FALSE;
        restore_pending = true;
        break;
    }
}
bool PumpGameEvents(bool wait)
{
    SDL_Event event{};
    bool received = false;
    if (wait && SDL_WaitEventTimeout(&event, 10))
    {
        HandleGameEvent(event);
        received = true;
    }
    while (SDL_PollEvent(&event))
    {
        HandleGameEvent(event);
        received = true;
    }
    return received;
}

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

#include "platform_events.h"

void w8_set_error(DWORD error);
namespace
{
struct Window
{
    SDL_Window* native;
    SDL_WindowID id;
    w8_native::WindowProcedure procedure;
    int width, height;
    POINT mouse{0, 0};
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    bool focused;
    float wheel_remainder = 0;
};
struct Timer
{
    HWND window;
    UINT_PTR id;
    UINT interval;
    uint64_t deadline;
    TIMERPROC callback;
};
std::vector<Window> windows;
std::vector<Timer> timers;
std::deque<MSG> messages;
UINT_PTR next_timer_id = 1;
uint64_t generation = 0, observed_generation = 0;
uint64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
HWND window_handle(const Window& window) { return reinterpret_cast<HWND>(window.native); }
Window* find_window(HWND handle)
{
    for (auto& window : windows)
        if (window_handle(window) == handle)
            return &window;
    return nullptr;
}
Window* find_window(SDL_WindowID id)
{
    for (auto& window : windows)
        if (window.id == id)
            return &window;
    return nullptr;
}
bool main_thread()
{
    if (!SDL_IsMainThread() || !(SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS))
    {
        w8_set_error(ERROR_ACCESS_DENIED);
        return false;
    }
    return true;
}
void enqueue(Window* window, UINT kind, WPARAM wparam = 0, LPARAM lparam = 0)
{
    MSG message{};
    message.hwnd = window ? window_handle(*window) : nullptr;
    message.message = kind;
    message.wParam = wparam;
    message.lParam = lparam;
    message.time = GetTickCount();
    if (window)
        message.pt = window->mouse;
    messages.push_back(message);
    ++generation;
}
LPARAM packed_position(const POINT& point)
{
    return LPARAM((uint32_t(uint16_t(point.y)) << 16) | uint16_t(point.x));
}
void mouse_position(Window& window, float x, float y)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window.native, &width, &height) || width <= 0 || height <= 0)
        return;
    window.mouse.x =
        std::clamp<int>(int(std::floor(x * window.width / width)), 0, window.width - 1);
    window.mouse.y =
        std::clamp<int>(int(std::floor(y * window.height / height)), 0, window.height - 1);
}
void modifiers(Window& window, SDL_Keymod value)
{
    const SDL_Keymod masks[] = {SDL_KMOD_SHIFT, SDL_KMOD_CTRL, SDL_KMOD_ALT};
    const UINT keys[] = {VK_SHIFT, VK_CONTROL, VK_MENU};
    for (size_t i = 0; i < 3; ++i)
    {
        bool before = (window.modifiers & masks[i]) != 0, after = (value & masks[i]) != 0;
        if (before != after)
            enqueue(&window, after ? WM_KEYDOWN : WM_KEYUP, keys[i],
                    after ? 1 : LPARAM(0xc0000001u));
    }
    window.modifiers = value;
}
UINT virtual_key(SDL_Keycode key, bool& extended)
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
void translate(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT)
    {
        enqueue(nullptr, WM_QUIT);
        return;
    }
    /* SDL places windowID at the same offset for the window, key and mouse
       events handled here. Read the named member for each event family. */
    SDL_WindowID id;
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        id = event.key.windowID;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        id = event.motion.windowID;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        id = event.button.windowID;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        id = event.wheel.windowID;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_RESIZED:
        id = event.window.windowID;
        break;
    default:
        return;
    }
    Window* window = find_window(id);
    if (!window)
        return;
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        window->focused = true;
        enqueue(window, WM_ACTIVATEAPP, 1);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        window->focused = false;
        window->modifiers = SDL_KMOD_NONE;
        window->wheel_remainder = 0;
        enqueue(window, WM_ACTIVATEAPP, 0);
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        enqueue(window, WM_CLOSE);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        enqueue(
            window, WM_SIZE, 0,
            LPARAM((uint32_t(uint16_t(event.window.data2)) << 16) | uint16_t(event.window.data1)));
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        if (!window->focused)
            break;
        modifiers(*window, event.key.mod);
        bool extended = false;
        UINT key = virtual_key(event.key.key, extended);
        if (!key || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)
            break;
        uint32_t flags = 1 | (extended ? 0x01000000u : 0);
        if (event.key.repeat)
            flags |= 0x40000000u;
        if (event.type == SDL_EVENT_KEY_UP)
            flags |= 0xc0000000u;
        bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;
        enqueue(window,
                event.type == SDL_EVENT_KEY_DOWN ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN)
                                                 : (alt ? WM_SYSKEYUP : WM_KEYUP),
                key, LPARAM(flags));
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (!window->focused)
            break;
        mouse_position(*window, event.motion.x, event.motion.y);
        enqueue(window, WM_MOUSEMOVE, 0, packed_position(window->mouse));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (!window->focused)
            break;
        mouse_position(*window, event.button.x, event.button.y);
        bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        UINT kind;
        if (event.button.button == SDL_BUTTON_LEFT)
            kind = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            kind = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
        else
            break;
        enqueue(window, kind, 0, packed_position(window->mouse));
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        if (!window->focused)
            break;
        mouse_position(*window, event.wheel.mouse_x, event.wheel.mouse_y);
        float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        window->wheel_remainder += direction * event.wheel.y * WHEEL_DELTA;
        /* Retain fractional scroll and send complete detents: the recovered
           consumer divides the signed high word by 120. */
        while (std::abs(window->wheel_remainder) >= WHEEL_DELTA)
        {
            int delta = window->wheel_remainder > 0 ? WHEEL_DELTA : -WHEEL_DELTA;
            window->wheel_remainder -= delta;
            enqueue(window, WM_MOUSEWHEEL, WPARAM(uint32_t(uint16_t(delta)) << 16),
                    packed_position(window->mouse));
        }
        break;
    }
    }
}
void due_timers()
{
    uint64_t now = now_ms();
    for (auto& timer : timers)
    {
        if (timer.deadline > now)
            continue;
        bool pending = std::any_of(messages.begin(), messages.end(),
                                   [&](const MSG& message)
                                   {
                                       return message.message == WM_TIMER &&
                                              message.hwnd == timer.window &&
                                              message.wParam == timer.id;
                                   });
        if (!pending)
        {
            MSG message{};
            message.hwnd = timer.window;
            message.message = WM_TIMER;
            message.wParam = timer.id;
            message.lParam = reinterpret_cast<LPARAM>(timer.callback);
            message.time = GetTickCount();
            messages.push_back(message);
            ++generation;
        }
        timer.deadline = now + timer.interval;
    }
}
void collect()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
        translate(event);
    due_timers();
}
bool matches(const MSG& message, HWND window, UINT first, UINT last)
{
    if (message.message == WM_QUIT)
        return true;
    if (window == reinterpret_cast<HWND>(intptr_t(-1)))
    {
        if (message.hwnd)
            return false;
    }
    else if (window && message.hwnd != window)
        return false;
    return (!first && !last) || (message.message >= first && message.message <= last);
}
bool validate(MSG* message, HWND window, UINT first, UINT last)
{
    if (!main_thread())
        return false;
    if (!message || (first && last < first))
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (window && window != reinterpret_cast<HWND>(intptr_t(-1)) && !find_window(window))
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return false;
    }
    return true;
}
void wait_for_event()
{
    uint64_t now = now_ms(), delay = 1000;
    for (const auto& timer : timers)
        delay = std::min(delay, timer.deadline > now ? timer.deadline - now : 0);
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, int(delay)))
        translate(event);
    due_timers();
}
} // namespace
namespace w8_native
{
HWND attach_window(SDL_Window* native, WindowProcedure procedure, int width, int height)
{
    if (!main_thread() || !native || width <= 0 || height <= 0 || width > 65535 || height > 65535)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SDL_WindowID id = SDL_GetWindowID(native);
    if (!id || find_window(id))
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    windows.push_back({native,
                       id,
                       procedure,
                       width,
                       height,
                       {0, 0},
                       SDL_KMOD_NONE,
                       (SDL_GetWindowFlags(native) & SDL_WINDOW_INPUT_FOCUS) != 0,
                       0});
    return window_handle(windows.back());
}
void detach_window(HWND window)
{
    if (!main_thread())
        return;
    timers.erase(std::remove_if(timers.begin(), timers.end(),
                                [&](const Timer& timer) { return timer.window == window; }),
                 timers.end());
    messages.erase(std::remove_if(messages.begin(), messages.end(),
                                  [&](const MSG& message) { return message.hwnd == window; }),
                   messages.end());
    windows.erase(std::remove_if(windows.begin(), windows.end(),
                                 [&](const Window& item) { return window_handle(item) == window; }),
                  windows.end());
}
void post_quit(int code)
{
    if (main_thread())
        enqueue(nullptr, WM_QUIT, WPARAM(code));
}
} // namespace w8_native
BOOL W8PeekMessage(LPMSG output, HWND window, UINT first, UINT last, UINT remove)
{
    if (!validate(output, window, first, last))
        return 0;
    if (remove != PM_NOREMOVE && remove != PM_REMOVE)
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    collect();
    observed_generation = generation;
    auto found = std::find_if(messages.begin(), messages.end(), [&](const MSG& message)
                              { return matches(message, window, first, last); });
    if (found == messages.end())
        return 0;
    *output = *found;
    if (remove == PM_REMOVE)
        messages.erase(found);
    return 1;
}
BOOL W8GetMessage(LPMSG output, HWND window, UINT first, UINT last)
{
    if (!validate(output, window, first, last))
        return -1;
    for (;;)
    {
        if (W8PeekMessage(output, window, first, last, PM_REMOVE))
            return output->message != WM_QUIT;
        wait_for_event();
        if (!main_thread())
            return -1;
    }
}
BOOL W8WaitMessage()
{
    if (!main_thread())
        return 0;
    collect();
    while (messages.empty() || generation == observed_generation)
    {
        wait_for_event();
        if (!main_thread())
            return 0;
    }
    observed_generation = generation;
    return 1;
}
BOOL W8TranslateMessage(const MSG* message)
{
    if (!message || !main_thread())
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* SGP's KeyChange/RedirectToString handle text through the recovered key
       table. No WM_CHAR consumer exists in this application. */
    return message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
           message->message == WM_SYSKEYDOWN || message->message == WM_SYSKEYUP;
}
LRESULT W8DispatchMessage(const MSG* message)
{
    if (!message || !main_thread())
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Window* window = message->hwnd ? find_window(message->hwnd) : nullptr;
    if (message->hwnd && !window)
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (message->message == WM_TIMER && message->lParam)
    {
        /* Dispatch the captured callback, including messages already queued
           when KillTimer was called, matching the Win32 message protocol. */
        reinterpret_cast<TIMERPROC>(message->lParam)(message->hwnd, WM_TIMER, message->wParam,
                                                     GetTickCount());
        return 0;
    }
    return window && window->procedure ? window->procedure(message->hwnd, message->message,
                                                           message->wParam, message->lParam)
                                       : 0;
}
UINT_PTR W8SetTimer(HWND window, UINT_PTR id, UINT interval, TIMERPROC callback)
{
    if (!main_thread())
        return 0;
    if (window && !find_window(window))
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    interval = std::clamp<UINT>(interval, 10, 0x7fffffffu);
    for (auto& timer : timers)
    {
        if (timer.window == window && timer.id == id && id)
        {
            timer.interval = interval;
            timer.deadline = now_ms() + interval;
            timer.callback = callback;
            return timer.id;
        }
    }
    if (!window || !id)
    {
        do
        {
            id = next_timer_id++;
        } while (!id || std::any_of(timers.begin(), timers.end(), [&](const Timer& timer)
                                    { return timer.window == window && timer.id == id; }));
    }
    timers.push_back({window, id, interval, now_ms() + interval, callback});
    return id;
}
BOOL W8KillTimer(HWND window, UINT_PTR id)
{
    if (!main_thread())
        return 0;
    auto timer = std::find_if(timers.begin(), timers.end(), [&](const Timer& timer)
                              { return timer.window == window && timer.id == id; });
    if (timer == timers.end())
        return 0;
    timers.erase(timer);
    return 1;
}
void W8GetMousePosition(POINT* point)
{
    if (!point || !main_thread())
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return;
    }
    *point = {0, 0};
    for (const auto& window : windows)
        if (window.focused)
        {
            *point = window.mouse;
            return;
        }
}
BOOL W8ClipCursor(const RECT* rect)
{
    if (!main_thread())
        return 0;
    Window* selected = nullptr;
    for (auto& window : windows)
        if (window.focused)
        {
            selected = &window;
            break;
        }
    if (!rect)
    {
        for (auto& window : windows)
        {
            if (!SDL_SetWindowMouseRect(window.native, nullptr))
            {
                w8_set_error(ERROR_NOT_SUPPORTED);
                return 0;
            }
        }
        return 1;
    }
    if (!selected)
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (rect->left < 0 || rect->top < 0 || rect->right <= rect->left || rect->bottom <= rect->top ||
        rect->right > selected->width || rect->bottom > selected->height)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    int width, height;
    if (!SDL_GetWindowSize(selected->native, &width, &height))
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    int left = rect->left * width / selected->width, top = rect->top * height / selected->height;
    SDL_Rect native{left, top, rect->right * width / selected->width - left,
                    rect->bottom * height / selected->height - top};
    if (!SDL_SetWindowMouseRect(selected->native, &native))
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return 1;
}
BOOL W8MinimizeWindow(HWND window)
{
    if (!main_thread())
        return 0;
    Window* selected = find_window(window);
    if (!selected)
    {
        w8_set_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!SDL_MinimizeWindow(selected->native))
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return 1;
}

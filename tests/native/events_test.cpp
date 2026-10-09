/* SDL -> MSG -> the recovered SGP queue/clock. Capture entry points below
   record calls for this test; they are not a native video implementation. */
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>

#include "platform_events.h"
#include "native/input_events.h"
#include "input.h"
#include "timer.h"
#include "Video2.h"

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s (error %u, SDL: %s)\n", __LINE__, #expression,            \
                    W8GetLastError(), SDL_GetError());                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

HWND ghWindow = nullptr;
static int screenshots = 0, captures = 0, callbacks = 0, window_timers = 0;
static std::thread::id dispatch_thread;
static UINT_PTR callback_id;
void PrintScreen() { ++screenshots; }
void VideoCaptureToggle() { ++captures; }
static LRESULT dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    CHECK(std::this_thread::get_id() == dispatch_thread);
    if (message == WM_TIMER)
        ++window_timers;
    return NativeInputWindowProcedure(window, message, wparam, lparam);
}
static void CALLBACK tick(HWND, UINT kind, UINT_PTR id, DWORD time)
{
    CHECK(kind == WM_TIMER && std::this_thread::get_id() == dispatch_thread);
    CHECK(uint32_t(GetTickCount() - time) < 1000);
    ++callbacks;
    callback_id = id;
}
static SDL_WindowID id;
static void push(SDL_Event event) { CHECK(SDL_PushEvent(&event)); }
static void focus(Uint32 kind)
{
    SDL_Event event{};
    event.type = kind;
    event.window.windowID = id;
    push(event);
}
static void key(Uint32 kind, SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE,
                bool repeat = false)
{
    SDL_Event event{};
    event.type = kind;
    event.key.windowID = id;
    event.key.key = code;
    event.key.mod = modifiers;
    event.key.repeat = repeat;
    push(event);
}
static void mouse(Uint32 kind, Uint8 button, float x, float y)
{
    SDL_Event event{};
    event.type = kind;
    if (kind == SDL_EVENT_MOUSE_MOTION)
    {
        event.motion.windowID = id;
        event.motion.x = x;
        event.motion.y = y;
    }
    else
    {
        event.button.windowID = id;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
    }
    push(event);
}
static void wheel(float amount, bool flipped = false)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = id;
    event.wheel.y = amount;
    event.wheel.mouse_x = 640;
    event.wheel.mouse_y = 480;
    event.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
    push(event);
}
static void drain_messages()
{
    MSG message;
    while (W8PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
    {
        CHECK(message.message != WM_QUIT);
        W8TranslateMessage(&message);
        W8DispatchMessage(&message);
    }
}
static std::vector<InputAtom> input()
{
    std::vector<InputAtom> result;
    InputAtom atom;
    while (DequeueEvent(&atom))
        result.push_back(atom);
    return result;
}
static void expect(const InputAtom& atom, UINT16 kind, UINT32 parameter, UINT16 state = 0)
{
    CHECK(atom.usEvent == kind && atom.usParam == parameter && atom.usKeyState == state);
}
int main()
{
    /* No GPU is needed to validate events and timers. */
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow("Whizzardry input test", 1280, 960, SDL_WINDOW_HIDDEN);
    CHECK(window);
    dispatch_thread = std::this_thread::get_id();
    id = SDL_GetWindowID(window);
    ghWindow = w8_native::attach_window(window, dispatch, 640, 480);
    CHECK(ghWindow && uintptr_t(ghWindow) == uintptr_t(window));
    CHECK(InitializeInputManager());
    drain_messages();
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 640, 480);
    drain_messages();
    CHECK(input().empty());
    CHECK(gusMouseXPos == 320 && gusMouseYPos == 240);

    CHECK(w8_native::warp_mouse(ghWindow, 123, 321));
    POINT position;
    W8GetMousePosition(&position);
    CHECK(position.x == 123 && position.y == 321);
    CHECK(w8_native::warp_mouse(ghWindow, -10, 600));
    W8GetMousePosition(&position);
    CHECK(position.x == 0 && position.y == 479);
    CHECK(w8_native::warp_mouse(ghWindow, 320, 240));
    drain_messages();

    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT, true);
    key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_LSHIFT);
    drain_messages();
    auto events = input();
    CHECK(events.size() == 3 && gfShiftState == 0 && gfKeyState['A'] == 0);
    expect(events[0], KEY_DOWN, 'A', SHIFT_DOWN);
    expect(events[1], KEY_REPEAT, 'A', SHIFT_DOWN);
    expect(events[2], KEY_UP, 'A', SHIFT_DOWN);
    CHECK(events[0].uiParam == ((240u << 16) | 320u));

    key(SDL_EVENT_KEY_DOWN, SDLK_LEFT);
    key(SDL_EVENT_KEY_UP, SDLK_LEFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_KP_1);
    key(SDL_EVENT_KEY_UP, SDLK_KP_1);
    key(SDL_EVENT_KEY_DOWN, SDLK_KP_ENTER);
    key(SDL_EVENT_KEY_UP, SDLK_KP_ENTER);
    key(SDL_EVENT_KEY_DOWN, SDLK_F1);
    key(SDL_EVENT_KEY_UP, SDLK_F1);
    drain_messages();
    events = input();
    CHECK(events.size() == 8);
    expect(events[0], KEY_DOWN, VK_LEFT);
    expect(events[2], KEY_DOWN, 0x61);
    expect(events[4], KEY_DOWN, 0x6c);
    expect(events[6], KEY_DOWN, 0x70);

    key(SDL_EVENT_KEY_DOWN, SDLK_LCTRL, SDL_KMOD_LCTRL);
    key(SDL_EVENT_KEY_DOWN, SDLK_RCTRL, SDL_KMOD_CTRL);
    key(SDL_EVENT_KEY_UP, SDLK_LCTRL, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_DOWN, SDLK_B, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_B, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_RCTRL);
    drain_messages();
    events = input();
    CHECK(events.size() == 2 && !gfCtrlState);
    expect(events[0], KEY_DOWN, 'B', CTRL_DOWN);
    expect(events[1], KEY_UP, 'B', CTRL_DOWN);

    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN);
    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN, SDL_KMOD_LCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_LCTRL);
    drain_messages();
    CHECK(screenshots == 1 && captures == 1 && input().empty());

    UINT16 buffer[16]{};
    StringInput descriptor{};
    descriptor.pString = buffer;
    descriptor.usMaxStringLength = 15;
    descriptor.fInsertMode = 1;
    gpCurrentStringDescriptor = &descriptor;
    gfCurrentStringInputState = 1;
    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_B);
    key(SDL_EVENT_KEY_UP, SDLK_B);
    key(SDL_EVENT_KEY_DOWN, SDLK_BACKSPACE);
    key(SDL_EVENT_KEY_UP, SDLK_BACKSPACE);
    key(SDL_EVENT_KEY_DOWN, SDLK_RETURN);
    key(SDL_EVENT_KEY_UP, SDLK_RETURN);
    drain_messages();
    CHECK(buffer[0] == 'A' && buffer[1] == 0 && !gfCurrentStringInputState);
    CHECK(descriptor.usLastCharacter == VK_RETURN);
    CHECK(input().empty());
    gpCurrentStringDescriptor = nullptr;

    gfTrackMousePos = 1;
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 1279, 959);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    drain_messages();
    events = input();
    CHECK(events.size() == 2 && gfLeftButtonState);
    expect(events[0], MOUSE_POS, 0);
    CHECK(events[0].uiParam == ((479u << 16) | 639u));
    expect(events[1], LEFT_BUTTON_DOWN, 0);
    guiLeftButtonRepeatTimer = GetTickCount() - 1;
    events = input();
    CHECK(events.size() == 1);
    expect(events[0], LEFT_BUTTON_REPEAT, 0);
    CHECK(events[0].uiParam == ((240u << 16) | 320u));
    guiSingleClickTimer = GetTickCount() - 1000;
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480);
    drain_messages();
    events = input();
    CHECK(events.size() == 1 && !gfLeftButtonState);
    expect(events[0], LEFT_BUTTON_UP, 0);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480);
    drain_messages();
    events = input();
    CHECK(events.size() == 3);
    expect(events[0], LEFT_BUTTON_DOWN, 0);
    expect(events[1], LEFT_BUTTON_UP, 0);
    expect(events[2], LEFT_BUTTON_DBL_CLK, 0);

    wheel(0.25f);
    wheel(0.5f);
    drain_messages();
    CHECK(input().empty());
    wheel(0.25f);
    wheel(2, true);
    drain_messages();
    events = input();
    CHECK(events.size() == 3);
    CHECK(GetMouseWheelDeltaValue(events[0].usParam) == 1);
    CHECK(GetMouseWheelDeltaValue(events[1].usParam) == -1 &&
          GetMouseWheelDeltaValue(events[2].usParam) == -1);

    key(SDL_EVENT_KEY_DOWN, SDLK_C, SDL_KMOD_LALT);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 640, 480);
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    key(SDL_EVENT_KEY_DOWN, SDLK_D);
    drain_messages();
    CHECK(!gfKeyState['C'] && !gfKeyState['D'] && !gfAltState && !gfRightButtonState &&
          !guiRightButtonRepeatTimer);
    CHECK(input().size() == 2);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_messages();

    /* Peek without removing and range/window filters keep queued messages. */
    key(SDL_EVENT_KEY_DOWN, SDLK_E);
    key(SDL_EVENT_KEY_UP, SDLK_E);
    MSG message{}, same{};
    CHECK(W8PeekMessage(&message, ghWindow, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE));
    CHECK(W8PeekMessage(&same, ghWindow, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE));
    CHECK(message.wParam == 'E' && same.time == message.time);
    CHECK(input().empty());
    CHECK(W8GetMessage(&message, ghWindow, WM_KEYDOWN, WM_KEYDOWN) == 1);
    W8DispatchMessage(&message);
    drain_messages();
    CHECK(input().size() == 2);
    CHECK(W8GetMessage(&message, reinterpret_cast<HWND>(1), 0, 0) == -1);
    CHECK(W8GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(!W8PeekMessage(nullptr, nullptr, 0, 0, PM_REMOVE));

    /* A previously peeked message is old for WaitMessage. A worker only pushes
       SDL events; dispatch and the recovered queue stay on the main thread. */
    key(SDL_EVENT_KEY_DOWN, SDLK_F);
    CHECK(W8PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE));
    std::thread producer(
        []
        {
            SDL_Delay(20);
            key(SDL_EVENT_KEY_UP, SDLK_F);
        });
    CHECK(W8WaitMessage());
    producer.join();
    CHECK(W8PeekMessage(&message, nullptr, WM_KEYUP, WM_KEYUP, PM_NOREMOVE));
    drain_messages();
    CHECK(input().size() == 2);

    CHECK(InitializeClockManager());
    CHECK(W8GetMessage(&message, nullptr, WM_TIMER, WM_TIMER) == 1);
    UINT32 initial_time = GetClock();
    W8DispatchMessage(&message);
    CHECK(uint32_t(GetClock() - initial_time) > 0 && GetClock() < 1000);
    ShutdownClockManager();
    drain_messages();

    callbacks = 0;
    UINT_PTR timer = W8SetTimer(ghWindow, UINT_PTR(0x100000001ULL), 0, tick);
    CHECK(timer == 0x100000001ULL);
    CHECK(W8GetMessage(&message, ghWindow, WM_TIMER, WM_TIMER) == 1);
    CHECK(callbacks == 0 && message.wParam == timer);
    CHECK(W8KillTimer(ghWindow, timer));
    W8DispatchMessage(&message);
    CHECK(callbacks == 1 && callback_id == timer);
    CHECK(!W8KillTimer(ghWindow, timer));
    timer = W8SetTimer(nullptr, 0, 10, tick);
    CHECK(timer);
    CHECK(W8SetTimer(nullptr, timer, 15, tick) == timer);
    CHECK(W8GetMessage(&message, reinterpret_cast<HWND>(intptr_t(-1)), WM_TIMER, WM_TIMER) == 1);
    CHECK(!message.hwnd && message.wParam == timer);
    W8DispatchMessage(&message);
    CHECK(W8KillTimer(nullptr, timer));
    CHECK(callbacks == 2);
    timer = W8SetTimer(ghWindow, 88, 10, nullptr);
    CHECK(timer && W8GetMessage(&message, nullptr, WM_TIMER, WM_TIMER) == 1);
    W8DispatchMessage(&message);
    CHECK(window_timers == 1 && W8KillTimer(ghWindow, 88));
    drain_messages();

    /* Ring wrap, full-queue drop and FIFO are still recovered SGP behavior. */
    for (unsigned batch = 0; batch < 3; ++batch)
    {
        for (unsigned i = 0; i < 257; ++i)
            QueueEvent(KEY_DOWN, i, batch);
        events = input();
        CHECK(events.size() == 256);
        for (unsigned i = 0; i < 256; ++i)
            CHECK(events[i].usParam == i && events[i].uiParam == batch);
    }
    w8_native::post_quit(7);
    CHECK(W8GetMessage(&message, ghWindow, WM_KEYDOWN, WM_KEYUP) == 0 &&
          message.message == WM_QUIT && message.wParam == 7);
    w8_native::detach_window(ghWindow);
    CHECK(!W8SetTimer(ghWindow, 1, 10, tick));
    ShutdownInputManager();
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("ok: SDL messages feed recovered SGP input, strings, repeats, ring queue and clock");
}

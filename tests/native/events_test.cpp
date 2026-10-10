/* SDL -> the recovered SGP queue and main-thread clock. Capture entry points below
   record calls for this test; they are not a native video implementation. */
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>

#include "wiz8/application.h"
#include "wiz8/engine_code/GameData.h"
#include "native/input_events.h"
#include "input.h"
#include "timer.h"
#include "Video2.h"

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s (SDL: %s)\n", __LINE__, #expression,            \
                    SDL_GetError());                                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

HWND ghWindow = nullptr;
static int screenshots = 0, captures = 0, suspensions = 0, restorations = 0, resizes = 0;
static std::thread::id dispatch_thread;
static std::vector<int> clock_actions;
void PrintScreen() { ++screenshots; }
void VideoCaptureToggle() { ++captures; }
BOOLEAN VideoInspectorIsEnabled() { return FALSE; }
void SuspendVideoManager() { ++suspensions; }
BOOLEAN RestoreVideoManager() { ++restorations; return TRUE; }
BOOLEAN RestoreVideoSurfaces() { return TRUE; }
BOOLEAN VideoResizeWindow() { ++resizes; return TRUE; }
float MoveTimer(int action) { clock_actions.push_back(action); return 0; }
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
static void drain_events()
{
    CHECK(std::this_thread::get_id() == dispatch_thread);
    PumpGameEvents();
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
    gfProgramIsRunning = gfApplicationActive = TRUE;
    gfIgnoreMessages = FALSE;
    /* No GPU is needed to validate events and timers. */
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow("Whizzardry input test", 1280, 960, SDL_WINDOW_HIDDEN);
    CHECK(window);
    dispatch_thread = std::this_thread::get_id();
    id = SDL_GetWindowID(window);
    ghWindow = reinterpret_cast<HWND>(window);
    SetInputWindow(window);
    CHECK(ghWindow && uintptr_t(ghWindow) == uintptr_t(window));
    CHECK(InitializeInputManager());
    drain_events();
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 640, 480);
    drain_events();
    CHECK(input().empty());
    CHECK(gusMouseXPos == 320 && gusMouseYPos == 240);

    CHECK(WarpGameMouse(window, 123, 321));
    POINT position;
    GetGameMousePosition(&position);
    CHECK(position.x == 123 && position.y == 321);
    CHECK(WarpGameMouse(window, -10, 600));
    GetGameMousePosition(&position);
    CHECK(position.x == 0 && position.y == 479);
    CHECK(WarpGameMouse(window, 320, 240));
    drain_events();

    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT, true);
    key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_LSHIFT);
    drain_events();
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
    drain_events();
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
    drain_events();
    events = input();
    CHECK(events.size() == 2 && !gfCtrlState);
    expect(events[0], KEY_DOWN, 'B', CTRL_DOWN);
    expect(events[1], KEY_UP, 'B', CTRL_DOWN);

    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN);
    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN, SDL_KMOD_LCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_LCTRL);
    drain_events();
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
    drain_events();
    CHECK(buffer[0] == 'A' && buffer[1] == 0 && !gfCurrentStringInputState);
    CHECK(descriptor.usLastCharacter == VK_RETURN);
    CHECK(input().empty());
    gpCurrentStringDescriptor = nullptr;

    gfTrackMousePos = 1;
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 1279, 959);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    drain_events();
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
    drain_events();
    events = input();
    CHECK(events.size() == 1 && !gfLeftButtonState);
    expect(events[0], LEFT_BUTTON_UP, 0);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480);
    drain_events();
    events = input();
    CHECK(events.size() == 3);
    expect(events[0], LEFT_BUTTON_DOWN, 0);
    expect(events[1], LEFT_BUTTON_UP, 0);
    expect(events[2], LEFT_BUTTON_DBL_CLK, 0);

    wheel(0.25f);
    wheel(0.5f);
    drain_events();
    CHECK(input().empty());
    wheel(0.25f);
    wheel(2, true);
    drain_events();
    events = input();
    CHECK(events.size() == 3);
    CHECK(GetMouseWheelDeltaValue(events[0].usParam) == 1);
    CHECK(GetMouseWheelDeltaValue(events[1].usParam) == -1 &&
          GetMouseWheelDeltaValue(events[2].usParam) == -1);

    key(SDL_EVENT_KEY_DOWN, SDLK_C, SDL_KMOD_LALT);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 640, 480);
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    key(SDL_EVENT_KEY_DOWN, SDLK_D);
    drain_events();
    CHECK(!gfKeyState['C'] && !gfKeyState['D'] && !gfAltState && !gfRightButtonState &&
          !guiRightButtonRepeatTimer);
    CHECK(input().size() == 2);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();

    /* SDL can receive worker-produced events; game dispatch stays on the main thread. */
    key(SDL_EVENT_KEY_DOWN, SDLK_F);
    drain_events();
    std::thread producer(
        []
        {
            SDL_Delay(20);
            key(SDL_EVENT_KEY_UP, SDLK_F);
        });
    events = input();
    Uint64 deadline = SDL_GetTicks() + 1000;
    while (events.size() < 2 && SDL_GetTicks() < deadline)
    {
        PumpGameEvents(true);
        auto next = input();
        events.insert(events.end(), next.begin(), next.end());
    }
    producer.join();
    CHECK(events.size() == 2);
    expect(events[0], KEY_DOWN, 'F');
    expect(events[1], KEY_UP, 'F');

    CHECK(InitializeClockManager());
    CHECK(GetClock() == 0);
    PumpGameEvents(true);
    CHECK(GetClock() > 0 && GetClock() < 1000);
    UINT32 countdown = SetCountdownClock(100);
    CHECK(ClockIsTicking(countdown) <= 100 && ClockIsTicking(countdown) > 0);
    extern UINT32 guiCurrentTime;
    guiCurrentTime = 0xfffffff0u;
    countdown = SetCountdownClock(32);
    CHECK(countdown == 16 && ClockIsTicking(countdown) == 32);
    guiCurrentTime = 32;
    CHECK(ClockIsTicking(countdown) == 0);
    ShutdownClockManager();
    SDL_Delay(10);
    UpdateClockManager();
    CHECK(guiCurrentTime == 32);

    suspensions = restorations = 0;
    clock_actions.clear();
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    drain_events();
    CHECK(!gfApplicationActive && suspensions == 1);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();
    CHECK(gfApplicationActive && restorations == 1);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();
    CHECK(restorations == 1 && clock_actions.size() == 2);
    CHECK(clock_actions[0] == TIMER_SUSPEND && clock_actions[1] == TIMER_RESUME);
    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    resize.window.windowID = id;
    resize.window.data1 = 1280;
    resize.window.data2 = 960;
    resizes = 0;
    push(resize);
    drain_events();
    CHECK(resizes == 1);
    POINT point{};
    CHECK(WarpGameMouse(window, 320, 240));
    GetGameMousePosition(&point);
    CHECK(point.x == 320 && point.y == 240);
    drain_events();
    input();

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
    gfIgnoreMessages = TRUE;
    key(SDL_EVENT_KEY_DOWN, SDLK_Q);
    focus(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    drain_events();
    CHECK(!gfProgramIsRunning && input().empty());
    gfProgramIsRunning = TRUE;
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    push(quit);
    drain_events();
    CHECK(!gfProgramIsRunning);
    gfIgnoreMessages = FALSE;
    SetInputWindow(nullptr);

    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("ok: SDL events feed input, strings, repeats, focus, resize, quit and main-thread clock");
}

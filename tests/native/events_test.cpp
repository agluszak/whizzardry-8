#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>

#include "Video2.h"
#include "compat/native.h"
#include "input.h"
#include "timer.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/GameData.h"

SDL_Window* ghWindow = nullptr;
static int screenshots = 0, captures = 0, suspensions = 0, restorations = 0, resizes = 0;
static std::thread::id dispatch_thread;
static std::vector<int> clock_actions;
void PrintScreen()
{
    ++screenshots;
}
void VideoCaptureToggle()
{
    ++captures;
}
bool VideoInspectorIsEnabled()
{
    return false;
}
void SuspendVideoManager()
{
    ++suspensions;
}
bool RestoreVideoManager()
{
    ++restorations;
    return true;
}
bool VideoResizeWindow()
{
    ++resizes;
    return true;
}
float MoveTimer(int action)
{
    clock_actions.push_back(action);
    return 0;
}
static SDL_WindowID id;
static void push(SDL_Event event)
{
    REQUIRE(SDL_PushEvent(&event));
}
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
    event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
    event.key.mod = modifiers;
    event.key.repeat = repeat;
    push(event);
}
static void mouse(Uint32 kind, Uint8 button, float x, float y, Uint8 clicks = 1)
{
    SDL_Event event{};
    event.type = kind;
    if (kind == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = id;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.windowID = id;
        event.button.button = button;
        event.button.clicks = clicks;
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
    REQUIRE(std::this_thread::get_id() == dispatch_thread);
    PumpGameEvents();
}
static std::vector<InputEvent> input()
{
    std::vector<InputEvent> result;
    InputEvent atom;
    while (DequeueEvent(&atom)) result.push_back(atom);
    return result;
}
static void expect(const InputEvent& atom, InputEventType kind, std::uint16_t parameter,
                   std::uint16_t state = 0)
{
    REQUIRE((atom.type == kind && atom.key == parameter && atom.modifiers == state));
}
TEST_CASE("events_test")
{
    gfProgramIsRunning = gfApplicationActive = true;
    gfIgnoreMessages = false;
    /* No GPU is needed to validate events and timers. */
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow("Whizzardry input test", 1280, 960, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    dispatch_thread = std::this_thread::get_id();
    id = SDL_GetWindowID(window);
    ghWindow = reinterpret_cast<SDL_Window*>(window);
    SetInputWindow(window);
    REQUIRE((ghWindow && uintptr_t(ghWindow) == uintptr_t(window)));
    REQUIRE(InitializeInputManager());
    drain_events();
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 640, 480);
    drain_events();
    REQUIRE(input().empty());
    REQUIRE((GetGameMousePosition().x == 320 && GetGameMousePosition().y == 240));

    REQUIRE(WarpGameMouse(window, 123, 321));
    SDL_Point position = GetGameMousePosition();
    REQUIRE((position.x == 123 && position.y == 321));
    REQUIRE(WarpGameMouse(window, -10, 600));
    position = GetGameMousePosition();
    REQUIRE((position.x == 0 && position.y == 479));
    REQUIRE(WarpGameMouse(window, 320, 240));
    drain_events();

    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT, true);
    key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_LSHIFT);
    drain_events();
    auto events = input();
    REQUIRE((events.size() == 3 && !(GetInputState().modifiers & SHIFT_DOWN) && !IsKeyHeld('A')));
    expect(events[0], InputEventType::KeyDown, 'A', SHIFT_DOWN);
    expect(events[1], InputEventType::KeyRepeat, 'A', SHIFT_DOWN);
    expect(events[2], InputEventType::KeyUp, 'A', SHIFT_DOWN);
    REQUIRE((events[0].position.x == 320 && events[0].position.y == 240));

    SetNativeTextInputEnabled(true);
    SDL_Event committed{};
    committed.type = SDL_EVENT_TEXT_INPUT;
    committed.text.windowID = SDL_GetWindowID(window);
    committed.text.text = "é中😀";
    REQUIRE(SDL_PushEvent(&committed));
    drain_events();
    events = input();
    REQUIRE(events.size() == 3);
    REQUIRE((events[0].text == 0x00e9 && events[1].text == 0x4e2d && events[2].text == 0x1f600));
    for (const auto& event : events)
        REQUIRE((event.type == InputEventType::Text && event.key == 0));
    SetNativeTextInputEnabled(false);
    REQUIRE(SDL_PushEvent(&committed));
    drain_events();
    REQUIRE(input().empty());

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
    REQUIRE(events.size() == 8);
    expect(events[0], InputEventType::KeyDown, VK_LEFT);
    expect(events[2], InputEventType::KeyDown, 0x61);
    expect(events[4], InputEventType::KeyDown, 0x6c);
    expect(events[6], InputEventType::KeyDown, 0x70);

    SDL_Event keypad_end{};
    keypad_end.type = SDL_EVENT_KEY_DOWN;
    keypad_end.key.windowID = id;
    keypad_end.key.key = SDLK_END;
    keypad_end.key.scancode = SDL_SCANCODE_KP_1;
    push(keypad_end);
    keypad_end.type = SDL_EVENT_KEY_UP;
    push(keypad_end);
    drain_events();
    events = input();
    REQUIRE(events.size() == 2);
    expect(events[0], InputEventType::KeyDown, 0x61);
    expect(events[1], InputEventType::KeyUp, 0x61);
    REQUIRE(TranslateCharacterToKey('-') == 0x6d);
    REQUIRE(TranslateKeyToCharacter('1', SHIFT_DOWN) == '!');
    REQUIRE(TranslateKeyToCharacter(0xde, SHIFT_DOWN) == '"');
    REQUIRE(KeyFromBindingName("NUM_ENTER") == VK_NUMPAD_ENTER);
    REQUIRE(KeyFromBindingName("q") == 'Q');
    REQUIRE(KeyFromBindingName("QQ") == 0);

    key(SDL_EVENT_KEY_DOWN, SDLK_LCTRL, SDL_KMOD_LCTRL);
    key(SDL_EVENT_KEY_DOWN, SDLK_RCTRL, SDL_KMOD_CTRL);
    key(SDL_EVENT_KEY_UP, SDLK_LCTRL, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_DOWN, SDLK_B, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_B, SDL_KMOD_RCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_RCTRL);
    drain_events();
    events = input();
    REQUIRE((events.size() == 2 && !(GetInputState().modifiers & CTRL_DOWN)));
    expect(events[0], InputEventType::KeyDown, 'B', CTRL_DOWN);
    expect(events[1], InputEventType::KeyUp, 'B', CTRL_DOWN);

    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN);
    key(SDL_EVENT_KEY_UP, SDLK_PRINTSCREEN, SDL_KMOD_LCTRL);
    key(SDL_EVENT_KEY_UP, SDLK_LCTRL);
    drain_events();
    REQUIRE((screenshots == 1 && captures == 1 && input().empty()));

    key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_DOWN, SDLK_B);
    key(SDL_EVENT_KEY_UP, SDLK_B);
    key(SDL_EVENT_KEY_DOWN, SDLK_BACKSPACE);
    key(SDL_EVENT_KEY_UP, SDLK_BACKSPACE);
    key(SDL_EVENT_KEY_DOWN, SDLK_RETURN);
    key(SDL_EVENT_KEY_UP, SDLK_RETURN);
    drain_events();
    events = input();
    REQUIRE(events.size() == 8);
    expect(events[0], InputEventType::KeyDown, 'A', SHIFT_DOWN);
    REQUIRE(TranslateKeyToCharacter(events[0].key, events[0].modifiers) == 'A');
    expect(events[2], InputEventType::KeyDown, 'B');
    expect(events[4], InputEventType::KeyDown, VK_BACK);
    expect(events[6], InputEventType::KeyDown, VK_RETURN);

    SetMouseMoveEventsEnabled(1);
    mouse(SDL_EVENT_MOUSE_MOTION, 0, 1279, 959);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    drain_events();
    events = input();
    REQUIRE((events.size() == 2 && IsLeftButtonHeld()));
    expect(events[0], InputEventType::MouseMove, 0);
    REQUIRE((events[0].position.x == 639 && events[0].position.y == 479));
    expect(events[1], InputEventType::LeftButtonDown, 0);
    REQUIRE(events[0].buttons == 0);
    REQUIRE(events[1].buttons == SDL_BUTTON_LMASK);
    REQUIRE(events[1].clicks == 1);
    GetInputState().left_repeat_at = w8_get_ticks() - 1;
    events = input();
    REQUIRE(events.size() == 1);
    expect(events[0], InputEventType::LeftButtonRepeat, 0);
    REQUIRE(events[0].buttons == SDL_BUTTON_LMASK);
    REQUIRE(events[0].clicks == 0);
    REQUIRE((events[0].position.x == 320 && events[0].position.y == 240));
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480);
    drain_events();
    events = input();
    REQUIRE((events.size() == 1 && !IsLeftButtonHeld()));
    expect(events[0], InputEventType::LeftButtonUp, 0);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480, 2);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480, 2);
    drain_events();
    events = input();
    REQUIRE(events.size() == 3);
    expect(events[0], InputEventType::LeftButtonDown, 0);
    expect(events[1], InputEventType::LeftButtonUp, 0);
    expect(events[2], InputEventType::LeftButtonDoubleClick, 0);
    REQUIRE(events[0].buttons == SDL_BUTTON_LMASK);
    REQUIRE(events[1].buttons == 0);
    REQUIRE(events[2].buttons == 0);
    for (const auto& event : events) REQUIRE(event.clicks == 2);

    // Closely spaced clicks are not double-clicks unless SDL identifies them.
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 480);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480);
    drain_events();
    events = input();
    REQUIRE(events.size() == 2);
    expect(events[0], InputEventType::LeftButtonDown, 0);
    expect(events[1], InputEventType::LeftButtonUp, 0);

    // Pumping ahead must not overwrite queued button states or coordinates.
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 40, 60);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 80, 100, 3);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 120, 140);
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, 160, 180, 3);
    drain_events();
    events = input();
    REQUIRE(events.size() == 4);
    REQUIRE((!IsLeftButtonHeld() && !IsRightButtonHeld()));
    REQUIRE(events[0].buttons == SDL_BUTTON_LMASK);
    REQUIRE(events[1].buttons == (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK));
    REQUIRE(events[2].buttons == SDL_BUTTON_RMASK);
    REQUIRE(events[3].buttons == 0);
    REQUIRE((events[1].clicks == 3 && events[3].clicks == 3));
    for (unsigned i = 0; i < events.size(); ++i) {
        REQUIRE(events[i].position.x == 20 + 20 * int(i));
        REQUIRE(events[i].position.y == 30 + 20 * int(i));
    }

    wheel(0.25f);
    wheel(0.5f);
    drain_events();
    REQUIRE(input().empty());
    wheel(0.25f);
    wheel(2, true);
    drain_events();
    events = input();
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].wheel == 1);
    REQUIRE((events[1].wheel == -1 && events[2].wheel == -1));

    key(SDL_EVENT_KEY_DOWN, SDLK_C, SDL_KMOD_LALT);
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 640, 480);
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    key(SDL_EVENT_KEY_DOWN, SDLK_D);
    drain_events();
    REQUIRE((!IsKeyHeld('C') && !IsKeyHeld('D') && !(GetInputState().modifiers & ALT_DOWN) &&
             !IsRightButtonHeld() && !GetInputState().right_repeat_at));
    REQUIRE(input().size() == 2);
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();

    /* SDL can receive worker-produced events; game dispatch stays on the main thread. */
    key(SDL_EVENT_KEY_DOWN, SDLK_F);
    drain_events();
    std::thread producer([] {
        SDL_Delay(20);
        key(SDL_EVENT_KEY_UP, SDLK_F);
    });
    events = input();
    Uint64 deadline = SDL_GetTicks() + 1000;
    while (events.size() < 2 && SDL_GetTicks() < deadline) {
        PumpGameEvents(true);
        auto next = input();
        events.insert(events.end(), next.begin(), next.end());
    }
    producer.join();
    REQUIRE(events.size() == 2);
    expect(events[0], InputEventType::KeyDown, 'F');
    expect(events[1], InputEventType::KeyUp, 'F');

    uint64_t countdown = w8_get_ticks() + 100;
    REQUIRE((TimeUntilDeadline(countdown) <= 100 && TimeUntilDeadline(countdown) > 0));
    REQUIRE(TimeUntilDeadline(w8_get_ticks() - 1) == 0);
    /* 64-bit deadlines never wrap: far-future stamps honestly report their
       remaining time instead of aliasing into the past. */
    countdown = w8_get_ticks() + 0xfffffff0u;
    REQUIRE((TimeUntilDeadline(countdown) > 0 && TimeUntilDeadline(countdown) <= 0xfffffff0u));

    suspensions = restorations = 0;
    clock_actions.clear();
    focus(SDL_EVENT_WINDOW_FOCUS_LOST);
    drain_events();
    REQUIRE((!gfApplicationActive && suspensions == 1));
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();
    REQUIRE((gfApplicationActive && restorations == 1));
    focus(SDL_EVENT_WINDOW_FOCUS_GAINED);
    drain_events();
    REQUIRE((restorations == 1 && clock_actions.size() == 2));
    REQUIRE((clock_actions[0] == TIMER_SUSPEND && clock_actions[1] == TIMER_RESUME));
    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    resize.window.windowID = id;
    resize.window.data1 = 1280;
    resize.window.data2 = 960;
    resizes = 0;
    push(resize);
    drain_events();
    REQUIRE(resizes == 1);
    SDL_Point point{};
    REQUIRE(WarpGameMouse(window, 320, 240));
    point = GetGameMousePosition();
    REQUIRE((point.x == 320 && point.y == 240));
    drain_events();
    input();

    /* Ring wrap, full-queue drop and FIFO are still recovered SGP behavior. */
    for (unsigned batch = 0; batch < 3; ++batch) {
        for (unsigned i = 0; i < 257; ++i)
            QueueEvent(InputEventType::KeyDown, {int(batch), 0}, std::uint16_t(i));
        events = input();
        REQUIRE(events.size() == 256);
        for (unsigned i = 0; i < 256; ++i)
            REQUIRE((events[i].key == i && events[i].position.x == int(batch)));
    }
    gfIgnoreMessages = true;
    key(SDL_EVENT_KEY_DOWN, SDLK_Q);
    focus(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    drain_events();
    REQUIRE((!gfProgramIsRunning && input().empty()));
    gfProgramIsRunning = true;
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    push(quit);
    drain_events();
    REQUIRE(!gfProgramIsRunning);
    gfIgnoreMessages = false;
    for (int round = 0; round < 3; ++round) {
        REQUIRE(InitializeInputManager());
        for (unsigned i = 0; i < 256; ++i)
            QueueEvent(InputEventType::KeyDown, {int(i) + 100, 0}, std::uint16_t(i));
        QueueEvent(InputEventType::KeyUp, {}, 999);
        events = input();
        REQUIRE(events.size() == 256);
        for (unsigned i = 0; i < events.size(); ++i) {
            expect(events[i], InputEventType::KeyDown, i);
            REQUIRE(events[i].position.x == int(i) + 100);
        }
    }
    for (unsigned queued : {254u, 255u, 256u}) {
        REQUIRE(InitializeInputManager());
        for (unsigned i = 0; i < queued; ++i)
            QueueEvent(InputEventType::KeyDown, {}, std::uint16_t(i));
        GetInputState().modifiers = SHIFT_DOWN;
        mouse(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 480, 2);
        drain_events();
        events = input();
        REQUIRE(events.size() == 256);
        for (unsigned i = 0; i < queued; ++i) expect(events[i], InputEventType::KeyDown, i);
        if (queued < 256) {
            expect(events[queued], InputEventType::LeftButtonUp, 0, SHIFT_DOWN);
            REQUIRE((events[queued].position.x == 320 && events[queued].position.y == 240));
        }
        if (queued == 254)
            expect(events.back(), InputEventType::LeftButtonDoubleClick, 0, SHIFT_DOWN);
        GetInputState().modifiers = 0;
        QueueEvent(InputEventType::KeyUp, {}, 999);
        events = input();
        REQUIRE(events.size() == 1);
        expect(events[0], InputEventType::KeyUp, 999);
    }
    key(SDL_EVENT_KEY_DOWN, SDLK_UNKNOWN);
    drain_events();
    REQUIRE(input().empty());
    SetInputWindow(nullptr);

    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("ok: SDL events feed input, strings, repeats, focus, resize, quit and main-thread clock");
}

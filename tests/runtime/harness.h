#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include "mgs_commands.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/application.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

/* Runtime scenarios against installed retail assets.

   The scenario drives the real game loop on this thread: it injects SDL input
   through the game's own event handler, steps one GameLoop at a time and
   checks state between frames. Nothing races the game, so a step either
   happens on the frame it is requested or never. Wall-clock limits only guard
   against hangs; synchronous UI effects use small frame budgets instead.

   Every game clock reads MonotonicMicroseconds, which WIZ8_TEST_HOOK replaces with a
   virtual clock advanced one 60 Hz frame per GameLoop: game time, movement and
   animation are the same on every run, and nothing waits for real time.

   CTest runs it under SDL's offscreen video driver with movies skipped
   through WIZ8_TEST_HOOK; the harness drops host input and focus events in
   case a visible driver is used. Exit code 77 means assets are absent. */

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct W8MonsterInfo;
class W8ButtonWidget;

[[noreturn]] void FailStep(const std::string& step, const std::string& detail);

/* Error-level log lines are test failures: the game reports broken assets and
   blits that way while carrying on. */
extern std::vector<std::string> g_logged_errors;

/* The game asked to quit; scenarios fail, random play just stops. */
struct GameExited : std::runtime_error {
    GameExited() : std::runtime_error("the game quit") {}
};

class GameHarness {
public:
    explicit GameHarness(bool play_movies, bool native_object = false);
    GameSession& games() { return application_->games(); }
    ~GameHarness();
    GameHarness(const GameHarness&) = delete;
    GameHarness& operator=(const GameHarness&) = delete;

    const fs::path& user_root() const { return user_.path; }

    /* One iteration of the shipped main loop. */
    void frame();

    /* Steps frames until `ready` holds. The time limit only catches hangs. */
    void wait(const char* step, const std::function<bool()>& ready, double seconds = 30);

    /* For effects the game applies synchronously to input: a few frames is
       always enough, and more would only hide a dropped event. */
    bool within_frames(int frames, const std::function<bool()>& done)
    {
        for (int i = 0; !done() && i < frames; ++i) frame();
        return done();
    }

    bool settled() const { return !IsScreenTransitionPending(); }

    void wait_screen(const char* step, W8ScreenId screen, double seconds = 30)
    {
        wait(step, [&] { return CurrentScreenState().id == screen && settled(); }, seconds);
    }

    /* Clicks a control the way a player would, after checking that a player
       could: it is shown and enabled, the pointer over its centre reaches it
       rather than anything above it, and the presented frame shows what the
       UI painted there. */
    void click(const char* step, const W8ButtonWidget& button, Uint8 which = SDL_BUTTON_LEFT);
    void click(const char* step, const HitTarget& target, Uint8 which = SDL_BUTTON_LEFT);
    /* For controls the scenario cannot reach as objects: clicks `point` with
       the same checks for the control found there. */
    void click(const char* step, SDL_Point point, Uint8 which = SDL_BUTTON_LEFT);

    void click_at(int x, int y)
    {
        mouse_button(x, y, true);
        frame();
        mouse_button(x, y, false);
        frame();
    }

    void right_click_at(int x, int y)
    {
        mouse_button(x, y, true, SDL_BUTTON_RIGHT);
        frame();
        mouse_button(x, y, false, SDL_BUTTON_RIGHT);
        frame();
    }

    void press_at(int x, int y)
    {
        mouse_button(x, y, true);
        frame();
    }
    void release_at(int x, int y)
    {
        mouse_button(x, y, false);
        frame();
    }

    void release_outside(SDL_Point point)
    {
        mouse_button(point.x, point.y, true);
        frame();
        mouse_button(0, 0, false);
        frame();
    }

    void move_pointer(int x, int y);

    void drag(SDL_Rect from, SDL_Rect to);

    void send_key(SDL_Scancode scancode, SDL_Keycode code, bool down, SDL_Keymod mod);

    void hold_key(SDL_Keycode code, bool down)
    {
        send_key(SDL_GetScancodeFromKey(code, nullptr), code, down, SDL_KMOD_NONE);
    }

    void key(SDL_Keycode code);

    // SDL text input may commit a whole word (paste or input method), not just
    // one ASCII character per key press. The dialogue test cares about the
    // submitted keyword, not the number of intermediate redraws.
    void enter_text(std::string_view text);

    /* Presses or releases a main-game command's key binding, with its
       modifiers, as SDL key events (bindings hold SDL keycodes). */
    void command_key(const MGSKeyBinding& binding, bool down);

    /* Presses and releases a command's key. The binding is copied first: the
       press can switch screens and with them the active key bindings. */
    void command(W8MGSCommand command);

    /* Copies a file into the writable user root before the game reads it. */
    void install(const fs::path& source, const fs::path& relative)
    {
        const fs::path target = user_.path / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }

    /* Pixels of the last rendered frame, or the CPU frame before renderer setup. */
    std::vector<unsigned> read_frame();

    /* Saves the current frame into WIZ8_RUNTIME_ARTIFACT_DIR, once per frame.
       Every failed assertion or exception during a scenario calls it. */
    void save_failure_frames();

    unsigned long frames() const { return frames_; }
    static constexpr unsigned long long frame_us = 16'667;

private:
    void mouse_button(int x, int y, bool down, Uint8 which = SDL_BUTTON_LEFT);
    void click_control(const char* step, SDL_Rect bounds, const std::function<bool()>& hovered,
                       Uint8 which, std::optional<SDL_Point> pointer = {});
    void check_modal_visible();

    /* Interactive means visible: once a modal input scope appears, the
       presented frame must soon show what the UI painted over its area. */
    std::vector<const HitTarget*> modal_targets_;
    struct AppearingModal {
        SDL_Rect area;
        int frames = 0;
    };
    std::optional<AppearingModal> appearing_modal_;

    ScopedTemporaryDirectory user_{"wiz8-runtime"};
    Clock::time_point started_ = Clock::now();
    std::optional<ScopedAssetRoots> roots_;
    std::optional<wiz8::Application> application_;
    SDL_Window* window_ = nullptr;
    unsigned long frames_ = 0;
    std::optional<unsigned long> failure_frame_;
};

/* The CPU frame buffer (the 2D interface) as XRGB pixels; empty on failure. */
std::vector<unsigned> FrameBufferPixels();

const char* ScreenName(int id);

SDL_Point MainMenuRegion(short item);

/* The formation panel's buttons; MGSFormation keeps them file-local. */
constexpr SDL_Point kFormationReset{242, 278};
constexpr SDL_Point kFormationAccept{360, 276};
constexpr SDL_Point kFormationCancel{394, 276};

/* With WIZ8_RUNTIME_ARTIFACT_DIR set (CTest sets one per scenario), frames the
   scenario checks are also saved there as PNGs for inspection. */
void Snapshot(const char* name, const std::vector<unsigned>& pixels);

/* Compares the presented frame with the golden signature
   WIZ8_RUNTIME_GOLDENS/<name>.png (see tests/runtime/fixtures/README.md).
   WIZ8_RUNTIME_UPDATE_GOLDENS=1 rewrites the golden instead. */
void ExpectScreen(GameHarness& game, const char* name);

void ReachMainMenu(GameHarness& game);

/* --- Main-game helpers ---------------------------------------------------- */

glm::vec3 PartyPosition();

float HorizontalDistance(const glm::vec3& a, const glm::vec3& b);

void WaitMainGame(GameHarness& game, const char* step);

/* Steps until the party stops moving (gravity and settling after loads). */
glm::vec3 Settle(GameHarness& game, const char* step);

/* Holds the forward key until the party is `distance` from where it started. */
glm::vec3 Walk(GameHarness& game, const char* step, float distance);

int PremadeSlot();

/* A new game with the checked-in character, bypassing party selection. */
void StartPremadeGame(GameHarness& game);

// The formation layer must keep gameplay dragging independent of its artwork.
/* The interface overlay's color at a point; zero where nothing is drawn. */
int OverlayPixel(SDL_Point point);

std::vector<fs::path> SaveFiles(const fs::path& user);

/* Quick-save through its key binding; returns the written save. */
fs::path QuickSave(GameHarness& game);

/* Quick-loads and waits until the main game is back and settled. */
void QuickLoad(GameHarness& game);

void ExpectAt(const char* step, const glm::vec3& expected);

/* Loads `save` through the main menu's Load panel and waits for the game. */
void LoadFromMenu(GameHarness& game, const fs::path& save);

/* Quick save, walk away, quick load: the party is back where it saved. */
/* Toggle combat and engage a live hostile monster, so the fight persists. */
W8MonsterInfo* EnterCombat(GameHarness& game);

/* Steers the party toward a monster with the arrow keys (forward is
   (sin yaw, cos yaw)) until it is within `stop` units or combat starts. */
void Approach(GameHarness& game, int location_id, float stop);

/* Directory a runtime scenario reads inputs from, named by an environment
   variable CTest sets (WIZ8_RUNTIME_FIXTURES, WIZ8_RUNTIME_DOWNLOADS, ...).
   Fails the test when it is unset or not an existing directory. */
fs::path RuntimeInput(const char* variable);

/* The shipped native VSG example that scenarios import with /NATIVEOBJECT. */
fs::path NativeDemoFile();

/* Directory a scenario writes to, named by an environment variable; it need not
   exist yet. Fails the test when the variable is unset. */
fs::path RuntimeOutput(const char* variable);

/* Adds the newest character to the party and starts the game. Defined with
   character creation in menus_test.cpp. */
void StartGame(GameHarness& game);

/* Skips the scenario without retail assets, and clears the logged errors. */
void RequireAssets();
/* Fails the scenario if the game logged any error. */
void CheckGameErrors();

template<class Scenario>
void RunScenario(Scenario scenario)
{
    RequireAssets();
    scenario();
    REQUIRE(g_renderer == nullptr);
    CheckGameErrors();
}

void DispatchGameEvent(const SDL_Event& event);

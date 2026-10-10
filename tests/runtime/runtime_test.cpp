/* Runtime scenarios against installed retail assets.

   The scenario drives the real game loop on this thread: it injects SDL input
   through the game's own event handler, steps one GameLoop at a time and
   checks state between frames. Nothing races the game, so a step either
   happens on the frame it is requested or never. Wall-clock limits only guard
   against hangs; synchronous UI effects use small frame budgets instead.

   Every game clock reads w8_clock_us, which WIZ8_TEST_HOOK replaces with a
   virtual clock advanced one 60 Hz frame per GameLoop: game time, movement and
   animation are the same on every run, and nothing waits for real time.

   Run under SDL's offscreen video driver (no window, no host input) with
   movies skipped through WIZ8_TEST_HOOK. Exit code 77 means assets are absent. */
#include "wiz8/application.h"
#include "native/input_events.h"
#include "temporary_directory.h"
#include "surrender/srGERD.h"
#include "surrender/srModelInstance.h"
#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/regions.h"
#include "wiz8/runtime_test_hooks.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/Screens.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

W8RuntimeTestHooks g_runtime_test_hooks;

namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[noreturn]] void Fail(const std::string& step, const std::string& detail)
{
    throw Failure(step + ": " + detail);
}
#define REQUIRE(step, condition)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
            Fail(step, #condition);                                                                \
    } while (false)

/* Error-level log lines are test failures: the game reports broken assets and
   blits that way while carrying on. */
std::vector<std::string> g_logged_errors;
SDL_LogOutputFunction g_default_log;
void* g_default_log_data;
void CaptureLog(void* data, int category, SDL_LogPriority priority, const char* message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR)
        g_logged_errors.emplace_back(message);
    g_default_log(g_default_log_data, category, priority, message);
}

const char* ScreenName(int id)
{
    static const char* names[] = {"intro",     "main-menu", "router",   "character", "please-wait",
                                  "party",     "camp",      "main-game", "automap",  "credits",
                                  "options",   "journal",   "exit"};
    return id >= 0 && id < int(std::size(names)) ? names[id] : "none";
}

bool RegionLive(unsigned region)
{
    if (region >= g_region_count || (g_regions[region].flags & W8_REGION_INPUT_DISABLED))
        return false;
    for (unsigned set = 0; set < g_region_set_count; ++set)
        if (g_region_sets[set].enabled && g_region_sets[set].first_region <= region &&
            region <= g_region_sets[set].last_region)
            return true;
    return false;
}

int RegionByCallback(W8RegionCallback callback)
{
    for (unsigned region = 0; region < g_region_count; ++region)
        if (g_regions[region].callback == callback && RegionLive(region))
            return int(region);
    return -1;
}

int RegionInSet(unsigned set, int help_text_id)
{
    if (!set || set >= g_region_set_count || !g_region_sets[set].enabled)
        return -1;
    for (unsigned region = g_region_sets[set].first_region;
         region <= g_region_sets[set].last_region && region < g_region_count; ++region)
        if ((help_text_id < 0 || g_regions[region].help_text_id == help_text_id) &&
            RegionLive(region))
            return int(region);
    return -1;
}

int ControlRegion(const W8TextControl* control)
{
    return control && control->m_region >= 0 && RegionLive(control->m_region) ? control->m_region
                                                                              : -1;
}

class Game
{
  public:
    explicit Game(bool play_movies)
    {
        const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
        if (!assets || !fs::is_directory(assets))
            throw std::invalid_argument("WIZ8_ASSET_ROOT is not set");
        user_ = make_temporary_directory("wiz8-runtime");
        auto roots = w8_native::path_roots();
        roots.assets = assets;
        roots.user = user_.string();
        w8_native::configure_paths(roots);
        auto config = wiz8::open_file("C:\\3DVideo.CFG", wiz8::OpenMode::replace);
        const std::string settings = "SDLGPU\n640\n480\n16\nSDL mixer spatial\n";
        config->write(settings.data(), settings.size());
        config->close();

        g_runtime_test_hooks = {};
        g_runtime_test_hooks.skip_movies = !play_movies;
        // Every clock the game reads advances one 60 Hz frame per GameLoop.
        g_runtime_test_hooks.virtual_clock = true;
        g_runtime_test_hooks.clock_us = 1'000'000;

        SDL_GetLogOutputFunction(&g_default_log, &g_default_log_data);
        SDL_SetLogOutputFunction(CaptureLog, nullptr);
        const std::string_view arguments[] = {"/WINDOW"};
        application_.emplace(arguments);
        window_ = reinterpret_cast<SDL_Window*>(ghWindow);
        REQUIRE("startup", window_);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = SDL_GetWindowID(window_);
        HandleGameEvent(focus);
    }
    ~Game()
    {
        application_.reset();
        SDL_SetLogOutputFunction(g_default_log, g_default_log_data);
        std::error_code ignored;
        fs::remove_all(user_, ignored);
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    const fs::path& user_root() const { return user_; }

    /* One iteration of the shipped main loop. */
    void frame()
    {
        PumpGameEvents(false);
        REQUIRE("frame", gfProgramIsRunning);
        GameLoop();
        gfSGPInputReceived = FALSE;
        ++frames_;
        g_runtime_test_hooks.clock_us += frame_us;
    }

    /* Steps frames until `ready` holds. The time limit only catches hangs. */
    void wait(const char* step, const std::function<bool()>& ready, double seconds = 30)
    {
        const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
        while (!ready())
        {
            if (Clock::now() > deadline)
                Fail(step, "timed out on screen " +
                               std::string(ScreenName(g_current_screen_state.id)) + " pending " +
                               ScreenName(g_pending_screen_state.id));
            frame();
        }
    }

    /* For effects the game applies synchronously to input: a few frames is
       always enough, and more would only hide a dropped event. */
    bool within_frames(int frames, const std::function<bool()>& done)
    {
        for (int i = 0; !done() && i < frames; ++i)
            frame();
        return done();
    }

    bool settled() const
    {
        return g_pending_screen_state.id == W8_SCREEN_NONE && !IsScreenTransitionPending();
    }

    void wait_screen(const char* step, W8ScreenId screen, double seconds = 30)
    {
        wait(step, [&] { return g_current_screen_state.id == screen && settled(); }, seconds);
    }

    void click(const char* step, int region)
    {
        if (region < 0)
            Fail(step, "target region is not live");
        const W8Region& bounds = g_regions[region];
        mouse_button((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2, true);
        frame();
        mouse_button((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2, false);
        frame();
    }

    void hold_key(SDL_Keycode code, bool down)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window_);
        event.key.key = code;
        event.key.down = down;
        HandleGameEvent(event);
    }

    void key(SDL_Keycode code)
    {
        hold_key(code, true);
        frame();
        hold_key(code, false);
        frame();
    }

    /* Pixels of the last rendered frame, read back from the GPU target. */
    std::vector<unsigned> read_frame()
    {
        auto* surface = g_gerd->lockBuffer();
        REQUIRE("read-frame", surface);
        std::vector<unsigned> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
        for (int y = 0; y < SCREEN_HEIGHT; ++y)
            for (int x = 0; x < SCREEN_WIDTH; ++x)
                pixels[y * SCREEN_WIDTH + x] = surface->getPixel(x, y) & 0xffffff;
        g_gerd->unlockBuffer();
        return pixels;
    }

    unsigned long frames() const { return frames_; }
    static constexpr unsigned long long frame_us = 16'667;

  private:
    void mouse_button(int x, int y, bool down)
    {
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        const float wx = (x + 0.5f) * width / 640, wy = (y + 0.5f) * height / 480;
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(window_);
        motion.motion.x = wx;
        motion.motion.y = wy;
        HandleGameEvent(motion);
        SDL_Event button{};
        button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        button.button.windowID = motion.motion.windowID;
        button.button.button = SDL_BUTTON_LEFT;
        button.button.down = down;
        button.button.clicks = 1;
        button.button.x = wx;
        button.button.y = wy;
        HandleGameEvent(button);
    }

    fs::path user_;
    std::optional<wiz8::Application> application_;
    SDL_Window* window_ = nullptr;
    unsigned long frames_ = 0;
};

/* With WIZ8_RUNTIME_SNAPSHOT_DIR set, frames the scenario checks are also
   saved there as BMPs for inspection. */
void Snapshot(const char* name, const std::vector<unsigned>& pixels)
{
    const char* directory = SDL_getenv("WIZ8_RUNTIME_SNAPSHOT_DIR");
    if (!directory)
        return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_XRGB8888,
                                                 const_cast<unsigned*>(pixels.data()),
                                                 SCREEN_WIDTH * 4);
    if (!surface)
        return;
    const std::string path = (fs::path(directory) / (std::string(name) + ".bmp")).string();
    SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
}

void ReachMainMenu(Game& game)
{
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    REQUIRE("main-menu", g_runtime_test_hooks.movie_frames_presented == 0);
}

/* --- Character creation through the real controls ------------------------ */

W8CharacterStatsPage* StatsPage()
{
    return g_character_screen ? static_cast<W8CharacterStatsPage*>(g_character_screen->m_pages[0])
                              : nullptr;
}

/* Spend a page's point pool through each entry's increment control until the
   page reports the allocation complete. */
void SpendPool(Game& game, const char* step, int page_index, const bool& complete)
{
    W8CharacterPage* page = g_character_screen->m_pages[page_index];
    REQUIRE(step, page && page->m_entries.count > 0);
    for (int index = 0; index < page->m_entries.count && !complete; ++index)
    {
        W8CharacterPageEntry* entry = page->m_entries.data[index];
        if (!entry || !entry->m_enabled || !entry->m_second || !entry->m_third ||
            (page_index == 2 && !entry->m_increment_allowed))
            continue;
        while (!complete && *entry->m_second < *entry->m_third)
        {
            const int before = *entry->m_second;
            game.click(step, ControlRegion(entry->m_increment));
            if (!game.within_frames(2, [&] { return complete || *entry->m_second != before; }))
                break; // The entry is capped by something else (race maximum).
        }
    }
    REQUIRE(step, complete);
}

/* Main menu -> party selection -> character screen -> saved character back on
   party selection. Returns the saved character's file name. */
std::string CreateCharacter(Game& game)
{
    ReachMainMenu(game);
    game.click("new-game", RegionByCallback(MainMenuNewGame));
    game.wait("party-selection", [&] {
        return g_current_screen_state.id == W8_SCREEN_PARTY_SELECTION && game.settled() &&
               RegionInSet(g_party_selection_left_action_region_set, -1) >= 0;
    });

    // "Create Character" is the first control of the left action panel.
    game.click("create-character", RegionInSet(g_party_selection_left_action_region_set, -1));
    game.wait("character-screen", [&] {
        return g_current_screen_state.id == W8_SCREEN_CHARACTER && game.settled() && StatsPage() &&
               g_character_screen->m_page_index == 0;
    });

    W8CharacterStatsPage* stats = StatsPage();
    REQUIRE("profession", stats->m_profession_row && stats->m_race_row && stats->m_gender_row);
    game.click("profession", ControlRegion(stats->m_profession_row->m_increment));
    REQUIRE("profession", game.within_frames(2, [&] { return stats->m_profession_row->m_index != -1; }));
    game.click("race", ControlRegion(stats->m_race_row->m_increment));
    REQUIRE("race", game.within_frames(2, [&] { return stats->m_race_row->m_index != -1; }));
    game.click("gender", ControlRegion(stats->m_gender_row->m_increment));
    REQUIRE("gender", game.within_frames(2, [&] { return stats->m_gender_row->m_index != -1; }));
    REQUIRE("attributes", game.within_frames(2, [&] {
        return stats->m_entries.count > 0 && stats->m_entries.data[0]->m_enabled;
    }));
    SpendPool(game, "attributes", 0, g_character_screen->m_creation_state.attributes_complete);

    // The first profession is a non-caster, so Next skips the spell page.
    game.click("to-skills", ControlRegion(g_character_screen->m_next));
    game.wait("skills", [&] {
        W8CharacterPage* page = g_character_screen->m_pages[2];
        return g_character_screen->m_page_index == 2 && page && page->m_entries.count > 0;
    });
    SpendPool(game, "skills", 2, g_character_screen->m_creation_state.skills_complete);

    game.click("to-personality", ControlRegion(g_character_screen->m_next));
    game.wait("personality", [&] {
        W8CharacterPage* page = g_character_screen->m_pages[3];
        return g_character_screen->m_page_index == 3 && page && !page->m_prepared;
    });
    // The page focuses its first (full name) field; Tab moves to the short
    // name, which names the saved file.
    for (SDL_Keycode key : {SDLK_P, SDLK_R, SDLK_O, SDLK_B, SDLK_E, SDLK_TAB, SDLK_V, SDLK_I})
        game.key(key);
    const W8Character& typed = g_character_screen->m_character;
    std::string name;
    for (char c : typed.name)
    {
        if (!c)
            break;
        name += char(c);
    }
    REQUIRE("name", !name.empty());

    game.click("commit", ControlRegion(g_character_screen->m_next));
    game.wait_screen("committed", W8_SCREEN_PARTY_SELECTION);
    const auto saved = game.user_root() / "Saves" / "Characters" / (name + ".CHR");
    if (!fs::is_regular_file(saved))
        Fail("saved", "missing " + saved.string());
    REQUIRE("saved", fs::file_size(saved) > 0);
    return name;
}

/* Adds the newest character to the party and starts the game. */
void StartGame(Game& game)
{
    CreateCharacter(game);
    // Return toggles the selected roster row (the new character) into the party.
    game.key(SDLK_RETURN);
    REQUIRE("add-to-party", game.within_frames(4, [] { return CountActiveCharacters() == 1; }));

    /* Confirm walks the screen's own steps: a party of fewer than six asks
       for confirmation, then the options page's Confirm runs the opening. */
    for (int press = 0; press < 3 && g_current_screen_state.id == W8_SCREEN_PARTY_SELECTION &&
                        game.settled();
         ++press)
    {
        game.click("start", RegionInSet(g_party_selection_bottom_action_region_set, 0x6cb));
        if (g_captured_region_index == 0x138)
            game.key(SDLK_RETURN);
    }
    game.wait_screen("main-game", W8_SCREEN_MAIN_GAME, 60);
    REQUIRE("main-game", g_runtime_test_hooks.movie_frames_presented == 0);
    W8World* world = GetWorld();
    REQUIRE("main-game", world && world->octree && world->camera && world->level);
}

/* --- Scenarios ----------------------------------------------------------- */

void VideoScenario()
{
    Game game(true);
    // The startup logo movie plays on the intro screen.
    game.wait("logo", [] { return g_runtime_test_hooks.movie_frames_presented >= 1; });
    REQUIRE("logo", g_current_screen_state.id == W8_SCREEN_INTRO);
    unsigned lit = 0;
    game.wait("logo-content", [&] {
        if (g_runtime_test_hooks.movie_frames_presented > 150)
            Fail("logo-content", "movie frames are black");
        for (unsigned pixel : game.read_frame())
            lit += (pixel & 0xf0f0f0) != 0;
        return lit > 1000;
    });
    const unsigned logo_frames = g_runtime_test_hooks.movie_frames_presented;
    // Escape moves on to the title movie, which also plays...
    game.key(SDLK_ESCAPE);
    game.wait("title", [&] {
        return g_runtime_test_hooks.movie_frames_presented >= logo_frames + 5;
    });
    REQUIRE("title", g_current_screen_state.id == W8_SCREEN_INTRO);
    // ...and Escape again leaves for the main menu.
    game.key(SDLK_ESCAPE);
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    std::printf("video: %u movie frames presented\n", g_runtime_test_hooks.movie_frames_presented);
}

void CharacterScenario()
{
    Game game(false);
    const std::string name = CreateCharacter(game);
    std::printf("character: saved %s.CHR after %lu frames\n", name.c_str(), game.frames());
}

void GameStartScenario()
{
    Game game(false);
    StartGame(game);
    W8WorldCameraState start{};
    GetWorldCameraState(GetWorld(), &start);
    // Hold the forward key until the camera has moved, through the real binding.
    game.hold_key(SDLK_UP, true);
    W8WorldCameraState now{};
    game.wait("walk", [&] {
        GetWorldCameraState(GetWorld(), &now);
        const float dx = now.position.x - start.position.x, dz = now.position.z - start.position.z;
        return std::sqrt(dx * dx + dz * dz) > 1.0f;
    }, 20);
    game.hold_key(SDLK_UP, false);
    std::printf("game-start: main game after %lu frames, walked to (%g %g %g)\n", game.frames(),
                now.position.x, now.position.y, now.position.z);
}

void WorldRenderScenario()
{
    Game game(false);
    StartGame(game);
    W8World* world = GetWorld();
    unsigned enabled = 0;
    for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i)
        if (world->psrMeshes[i] && !world->psrMeshes[i]->testFlag(srNode::FLAG_DISABLE))
            ++enabled;
    REQUIRE("meshes", enabled > 0);

    // The same scene and UI with only the world pass switched off is the control.
    REQUIRE("world", g_world_render_enabled && !g_world_blacked_out);
    RenderFrame();
    srGERD::Statistics with_world{};
    g_gerd->getStatistics(with_world);
    const unsigned polygons = world->level->m_submitted_polygons;
    const auto visible = game.read_frame();
    g_world_render_enabled = 0;
    RenderFrame();
    srGERD::Statistics without_world{};
    g_gerd->getStatistics(without_world);
    const auto control = game.read_frame();
    g_world_render_enabled = 1;

    Snapshot("world", visible);
    Snapshot("world-control", control);
    unsigned changed = 0, viewport = 0;
    for (int y = g_viewport.top; y < g_viewport.bottom; ++y)
        for (int x = g_viewport.left; x < g_viewport.right; ++x, ++viewport)
            changed += visible[y * SCREEN_WIDTH + x] != control[y * SCREEN_WIDTH + x];
    std::printf("world: %u meshes, %u polygons, draws %u/%u, triangles %u/%u, %u/%u viewport "
                "pixels from the world\n",
                enabled, polygons, with_world.draw_calls, without_world.draw_calls,
                with_world.input_triangles, without_world.input_triangles, changed, viewport);
    REQUIRE("world", polygons > 0);
    REQUIRE("world", with_world.draw_calls > without_world.draw_calls);
    REQUIRE("world", with_world.input_triangles > without_world.input_triangles);
    // The sky is a separate pass and stays in the control frame; the world
    // (ground, cliffs, water at the Monastery start) covers about half the view.
    REQUIRE("world", changed > viewport / 4);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s video|character|game-start|world\n", argv[0]);
        return 2;
    }
    const std::string scenario = argv[1];
    try
    {
        if (scenario == "video")
            VideoScenario();
        else if (scenario == "character")
            CharacterScenario();
        else if (scenario == "game-start")
            GameStartScenario();
        else if (scenario == "world")
            WorldRenderScenario();
        else
            Fail("arguments", "unknown scenario " + scenario);
    }
    catch (const std::invalid_argument& skipped)
    {
        std::fprintf(stderr, "%s: skipped: %s\n", scenario.c_str(), skipped.what());
        return 77;
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "%s: FAILED at %s\n", scenario.c_str(), failure.what());
        return 1;
    }
    if (!g_logged_errors.empty())
    {
        for (const auto& message : g_logged_errors)
            std::fprintf(stderr, "%s: logged error: %s\n", scenario.c_str(), message.c_str());
        return 1;
    }
    std::printf("%s: passed\n", scenario.c_str());
    return 0;
}

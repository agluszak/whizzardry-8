#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>

#include "wiz8/ui_layer.h"

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
#include "catch2/catch_message.hpp"
#include "conditions.h"
#include "input.h"
#include "layouts/gameplay_databases.h"
#include "layouts/party_formation.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "local_code/CharGeneration.h"
#include "local_code/Factions.h"
#include "local_code/SpellEffect.h"
#include "mgs_commands.h"
#include "scoped_asset_roots.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "temporary_directory.h"
#include "wiz8/application.h"
#include "wiz8/asset_paths.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/npc_dialogue_view.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSSpellIcons.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/runtime_test_hooks.h"
#include "wiz8/spell_ids.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

W8RuntimeTestHooks g_runtime_test_hooks;

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

[[noreturn]] void Fail(const std::string& step, const std::string& detail)
{
    throw std::runtime_error(step + ": " + detail);
}

/* Error-level log lines are test failures: the game reports broken assets and
   blits that way while carrying on. */
std::vector<std::string> g_logged_errors;
SDL_LogOutputFunction g_default_log;
void* g_default_log_data;
void CaptureLog(void* data, int category, SDL_LogPriority priority, const char* message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) g_logged_errors.emplace_back(message);
    g_default_log(g_default_log_data, category, priority, message);
}

const char* ScreenName(int id)
{
    static const char* names[] = {"intro",   "main-menu", "router",    "character", "please-wait",
                                  "party",   "camp",      "main-game", "automap",   "credits",
                                  "options", "journal",   "exit"};
    return id >= 0 && id < int(std::size(names)) ? names[id] : "none";
}

SDL_Point MainMenuRegion(short item)
{
    static constexpr std::array<SDL_Point, 6> centers = {{
        {320, 160},
        {320, 209},
        {320, 257},
        {320, 306},
        {320, 357},
        {320, 445},
    }};
    REQUIRE(item >= 0);
    REQUIRE(static_cast<std::size_t>(item) < centers.size());
    REQUIRE(UiTargetAt(centers[item]) != nullptr);
    return centers[item];
}

class Game {
public:
    explicit Game(bool play_movies)
    {
        const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
        if (!assets || !fs::is_directory(assets))
            throw std::invalid_argument("WIZ8_ASSET_ROOT is not set");
        auto roots = w8_native::path_roots();
        roots.assets = assets;
        roots.user = user_.path.string();
        roots_.emplace(roots);
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
        // The game seeds rand() from the clock. Catch's seed makes a run
        // repeatable: CTest fixes it, and failures print it.
        std::srand(Catch::getSeed());
        window_ = reinterpret_cast<SDL_Window*>(ghWindow);
        REQUIRE(window_);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = SDL_GetWindowID(window_);
        HandleGameEvent(focus);
    }
    ~Game()
    {
        application_.reset();
        SDL_SetLogOutputFunction(g_default_log, g_default_log_data);
        if (SDL_getenv("WIZ8_RUNTIME_KEEP_USER_ROOT")) {
            std::fprintf(stderr, "kept user root %s\n", user_.path.string().c_str());
            user_.keep();
        }
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    const fs::path& user_root() const { return user_.path; }

    /* One iteration of the shipped main loop. */
    void frame()
    {
        PumpGameEvents(false);
        REQUIRE(gfProgramIsRunning);
        GameLoop();
        GetInputState().received = false;
        ++frames_;
        g_runtime_test_hooks.clock_us += frame_us;
    }

    /* Steps frames until `ready` holds. The time limit only catches hangs. */
    void wait(const char* step, const std::function<bool()>& ready, double seconds = 30)
    {
        const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
        while (!ready()) {
            if (Clock::now() > deadline)
                Fail(step, "timed out after " + std::to_string(frames_) + " frames on screen " +
                               std::string(ScreenName(g_current_screen_state.id)) + " pending " +
                               ScreenName(g_pending_screen_state.id));
            frame();
        }
    }

    /* For effects the game applies synchronously to input: a few frames is
       always enough, and more would only hide a dropped event. */
    bool within_frames(int frames, const std::function<bool()>& done)
    {
        for (int i = 0; !done() && i < frames; ++i) frame();
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

    void click(const char* step, SDL_Point point)
    {
        if (!UiTargetAt(point)) Fail(step, "target is not live");
        click_at(point.x, point.y);
    }

    void click_at(int x, int y)
    {
        mouse_button(x, y, true);
        frame();
        mouse_button(x, y, false);
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

    void move_pointer(int x, int y)
    {
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(window_);
        motion.motion.x = (x + 0.5f) * width / 640;
        motion.motion.y = (y + 0.5f) * height / 480;
        HandleGameEvent(motion);
        frame();
    }

    void drag(SDL_Rect from, SDL_Rect to)
    {
        mouse_button(from.x + from.w / 2, from.y + from.h / 2, true);
        frame();
        // Leaving the source while held starts the gameplay drag.
        move_pointer(to.x + to.w / 2, to.y + to.h / 2);
        mouse_button(to.x + to.w / 2 - g_cursor_hotspot_x, to.y + to.h / 2 - g_cursor_hotspot_y,
                     false);
        frame();
    }

    void send_key(SDL_Scancode scancode, SDL_Keycode code, bool down, SDL_Keymod mod)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window_);
        event.key.scancode = scancode;
        event.key.key = code;
        event.key.mod = mod;
        event.key.down = down;
        HandleGameEvent(event);
    }

    void hold_key(SDL_Keycode code, bool down)
    {
        send_key(SDL_GetScancodeFromKey(code, nullptr), code, down, SDL_KMOD_NONE);
    }

    void key(SDL_Keycode code)
    {
        hold_key(code, true);
        if (NativeTextInputEnabled() && code >= 32 && code < 127) {
            const char text[] = {static_cast<char>(code), 0};
            SDL_Event committed{};
            committed.type = SDL_EVENT_TEXT_INPUT;
            committed.text.windowID = SDL_GetWindowID(window_);
            committed.text.text = text;
            HandleGameEvent(committed);
        }
        frame();
        hold_key(code, false);
        frame();
    }

    // SDL text input may commit a whole word (paste or input method), not just
    // one ASCII character per key press. The dialogue test cares about the
    // submitted keyword, not the number of intermediate redraws.
    void enter_text(std::string_view text)
    {
        REQUIRE(NativeTextInputEnabled());
        const std::string committed(text);
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = SDL_GetWindowID(window_);
        event.text.text = committed.c_str();
        HandleGameEvent(event);
        frame();
    }

    /* Presses or releases the key bound to a main-game command, with its
       modifiers, as SDL key events (bindings hold virtual-key codes). */
    void command_key(W8MGSCommand command, bool down)
    {
        const int index = g_mgs_keyboard ? g_mgs_keyboard->FindBinding(command) : -1;
        const MGSKeyBinding* binding = index >= 0 ? g_mgs_keyboard->GetBinding(index) : nullptr;
        if (!binding || !binding->key)
            Fail("command", "no key bound to command " + std::to_string(int(command)));
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
        for (int candidate = 1; candidate < SDL_SCANCODE_COUNT && scancode == SDL_SCANCODE_UNKNOWN;
             ++candidate) {
            const auto code = static_cast<SDL_Scancode>(candidate);
            if (GameKeyFromSdl(code, SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false)) ==
                binding->key)
                scancode = code;
        }
        if (scancode == SDL_SCANCODE_UNKNOWN)
            Fail("command", "no SDL key produces virtual key " + std::to_string(binding->key));
        const struct {
            unsigned short flag;
            SDL_Scancode scancode;
            SDL_Keycode key;
            SDL_Keymod mod;
        } modifiers[] = {{SHIFT_DOWN, SDL_SCANCODE_LSHIFT, SDLK_LSHIFT, SDL_KMOD_LSHIFT},
                         {CTRL_DOWN, SDL_SCANCODE_LCTRL, SDLK_LCTRL, SDL_KMOD_LCTRL},
                         {ALT_DOWN, SDL_SCANCODE_LALT, SDLK_LALT, SDL_KMOD_LALT}};
        SDL_Keymod mod = SDL_KMOD_NONE;
        for (const auto& modifier : modifiers)
            if (binding->modifiers & modifier.flag) mod |= modifier.mod;
        const SDL_Keycode key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        if (down) {
            for (const auto& modifier : modifiers)
                if (binding->modifiers & modifier.flag)
                    send_key(modifier.scancode, modifier.key, true, mod);
            send_key(scancode, key, true, mod);
        } else {
            send_key(scancode, key, false, mod);
            for (const auto& modifier : modifiers)
                if (binding->modifiers & modifier.flag)
                    send_key(modifier.scancode, modifier.key, false, SDL_KMOD_NONE);
        }
    }

    void command(W8MGSCommand command)
    {
        command_key(command, true);
        frame();
        command_key(command, false);
        frame();
    }

    /* Copies a file into the writable user root before the game reads it. */
    void install(const fs::path& source, const fs::path& relative)
    {
        const fs::path target = user_.path / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
    }

    /* Pixels of the last rendered frame, or the CPU frame before renderer setup. */
    std::vector<unsigned> read_frame()
    {
        if (!g_gerd) {
            // Camp renders through SGP's CPU surface, without a world renderer.
            const auto& surface = GetFrameBufferSurface();
            std::vector<unsigned> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
            REQUIRE(SDL_ConvertPixels(SCREEN_WIDTH, SCREEN_HEIGHT, surface.format, surface.pixels,
                                      surface.pitch, SDL_PIXELFORMAT_XRGB8888, pixels.data(),
                                      SCREEN_WIDTH * sizeof(unsigned)));
            return pixels;
        }
        auto* surface = g_gerd->lockBuffer();
        REQUIRE(surface);
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

    ScopedTemporaryDirectory user_{"wiz8-runtime"};
    std::optional<ScopedAssetRoots> roots_;
    std::optional<wiz8::Application> application_;
    SDL_Window* window_ = nullptr;
    unsigned long frames_ = 0;
};

/* With WIZ8_RUNTIME_SNAPSHOT_DIR set, frames the scenario checks are also
   saved there as BMPs for inspection. */
void Snapshot(const char* name, const std::vector<unsigned>& pixels)
{
    const char* directory = SDL_getenv("WIZ8_RUNTIME_SNAPSHOT_DIR");
    if (!directory) return;
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_XRGB8888,
                              const_cast<unsigned*>(pixels.data()), SCREEN_WIDTH * 4);
    if (!surface) return;
    const std::string path = (fs::path(directory) / (std::string(name) + ".bmp")).string();
    SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
}

void ReachMainMenu(Game& game)
{
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    REQUIRE(g_runtime_test_hooks.movie_frames_presented == 0);
}

/* The menu's hit targets must be screen-owned, survive screen re-entry, and
   never activate from an unpaired mouse release. */
void MainMenuUiScenario()
{
    Game game(false);
    ReachMainMenu(game);
    const auto verify_controls = [] {
        for (short item = 0; item < 6; ++item) REQUIRE(UiTargetAt(MainMenuRegion(item)) != nullptr);
    };
    verify_controls();
    REQUIRE(!g_main_menu_has_save_games);
    game.click("disabled-load", MainMenuRegion(2));
    REQUIRE(g_current_screen_state.id == W8_SCREEN_MAIN_MENU);
    REQUIRE(game.settled());
    game.release_outside(MainMenuRegion(1));
    REQUIRE(g_current_screen_state.id == W8_SCREEN_MAIN_MENU);
    REQUIRE(game.settled());
    game.click("credits", MainMenuRegion(3));
    game.wait_screen("credits", W8_SCREEN_CREDITS);
    game.click_at(320, 240);
    game.wait_screen("main-menu-return", W8_SCREEN_MAIN_MENU);
    verify_controls();
}

/* --- Character creation through the real controls ------------------------ */

W8CharacterStatsPage* StatsPage()
{
    return g_character_screen
               ? static_cast<W8CharacterStatsPage*>(g_character_screen->m_pages[0].get())
               : nullptr;
}

/* Spend a page's point pool through each entry's increment control until the
   page reports the allocation complete. */
void SpendPool(Game& game, const char* step, int page_index, const bool& complete)
{
    W8CharacterPage* page = g_character_screen->m_pages[page_index].get();
    REQUIRE((page && !page->m_entries.empty()));
    for (int index = 0; index < static_cast<int>(page->m_entries.size()) && !complete; ++index) {
        W8CharacterPageEntry* entry = page->m_entries[index].get();
        if (!entry || !entry->m_enabled || !entry->m_second || !entry->m_third ||
            (page_index == 2 && !entry->m_increment_allowed))
            continue;
        while (!complete && *entry->m_second < *entry->m_third) {
            const int before = *entry->m_second;
            INFO(step);
            const auto bounds = entry->m_increment->Bounds();
            game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
            if (!game.within_frames(2, [&] { return complete || *entry->m_second != before; }))
                break; // The entry is capped by something else (race maximum).
        }
    }
    REQUIRE(complete);
}

/* Main menu -> party selection -> character screen -> saved character back on
   party selection. Returns the saved character's file name. */
/* "Create Character" is the first control of the party selection screen's left
   action panel at (0x145, 0x137). */
constexpr SDL_Point kCreateCharacterButton{0x145 + 8, 0x137 + 8};

std::string CreateCharacter(Game& game)
{
    ReachMainMenu(game);
    game.click("new-game", MainMenuRegion(1));
    game.wait("party-selection", [&] {
        return g_current_screen_state.id == W8_SCREEN_PARTY_SELECTION && game.settled() &&
               UiTargetAt(kCreateCharacterButton) != nullptr;
    });
    game.click("create-character", kCreateCharacterButton);
    game.wait("character-screen", [&] {
        return g_current_screen_state.id == W8_SCREEN_CHARACTER && game.settled() && StatsPage() &&
               g_character_screen->m_page_index == 0;
    });

    W8CharacterStatsPage* stats = StatsPage();
    REQUIRE((stats->m_profession_row && stats->m_race_row && stats->m_gender_row));
    {
        const auto bounds = stats->m_profession_row->m_increment->Bounds();
        game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    }
    REQUIRE(game.within_frames(2, [&] { return stats->m_profession_row->m_index != -1; }));
    {
        const auto bounds = stats->m_race_row->m_increment->Bounds();
        game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    }
    REQUIRE(game.within_frames(2, [&] { return stats->m_race_row->m_index != -1; }));
    {
        const auto bounds = stats->m_gender_row->m_increment->Bounds();
        game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    }
    REQUIRE(game.within_frames(2, [&] { return stats->m_gender_row->m_index != -1; }));
    REQUIRE((game.within_frames(
        2, [&] { return !stats->m_entries.empty() && stats->m_entries[0]->m_enabled; })));
    SpendPool(game, "attributes", 0, g_character_screen->m_creation_state.attributes_complete);

    // The first profession is a non-caster, so Next skips the spell page.
    game.click_at(616, 464);
    game.wait("skills", [&] {
        W8CharacterPage* page = g_character_screen->m_pages[2].get();
        return g_character_screen->m_page_index == 2 && page && !page->m_entries.empty();
    });
    SpendPool(game, "skills", 2, g_character_screen->m_creation_state.skills_complete);

    game.click_at(616, 464);
    game.wait("personality", [&] {
        W8CharacterPage* page = g_character_screen->m_pages[3].get();
        return g_character_screen->m_page_index == 3 && page && !page->m_prepared;
    });
    // The page focuses its first (full name) field; Tab moves to the short
    // name, which names the saved file.
    for (SDL_Keycode key : {SDLK_P, SDLK_R, SDLK_O, SDLK_B, SDLK_E, SDLK_TAB, SDLK_V, SDLK_I})
        game.key(key);
    const W8Character& typed = g_character_screen->m_character;
    const std::string name = typed.name;
    REQUIRE(!name.empty());

    game.click_at(616, 464);
    game.wait_screen("committed", W8_SCREEN_PARTY_SELECTION);
    const auto saved = game.user_root() / "Saves" / "Characters" / (name + ".CHR");
    if (!fs::is_regular_file(saved)) Fail("saved", "missing " + saved.string());
    REQUIRE(fs::file_size(saved) > 0);
    return name;
}

/* Adds the newest character to the party and starts the game. */
void StartGame(Game& game)
{
    CreateCharacter(game);
    // Return toggles the selected roster row (the new character) into the party.
    game.key(SDLK_RETURN);
    REQUIRE(game.within_frames(4, [] { return CountActiveCharacters() == 1; }));

    /* Confirm walks the screen's own steps: a party of fewer than six asks
       for confirmation, then the options page's Confirm runs the opening. */
    for (int press = 0;
         press < 3 && g_current_screen_state.id == W8_SCREEN_PARTY_SELECTION && game.settled();
         ++press) {
        game.click_at(430, 447);
        if (UiModalActive()) game.key(SDLK_RETURN);
    }
    game.wait_screen("main-game", W8_SCREEN_MAIN_GAME, 60);
    REQUIRE(g_runtime_test_hooks.movie_frames_presented == 0);
    W8World* world = GetWorld();
    REQUIRE((world && world->octree && world->camera && world->level));
}

/* --- Main-game helpers ---------------------------------------------------- */

srVector3T<float> PartyPosition()
{
    W8WorldCameraState state{};
    state = cameraState(*GetWorld());
    return state.position;
}

float HorizontalDistance(const srVector3T<float>& a, const srVector3T<float>& b)
{
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void WaitMainGame(Game& game, const char* step)
{
    game.wait_screen(step, W8_SCREEN_MAIN_GAME, 60);
    W8World* world = GetWorld();
    REQUIRE((world && world->octree && world->camera && world->level));
}

/* Steps until the party stops moving (gravity and settling after loads). */
srVector3T<float> Settle(Game& game, const char* step)
{
    srVector3T<float> last = PartyPosition();
    for (int frame = 0, still = 0; frame < 600; ++frame) {
        game.frame();
        const srVector3T<float> now = PartyPosition();
        const float dy = now.y - last.y;
        still = HorizontalDistance(now, last) < 0.01f && std::abs(dy) < 0.01f ? still + 1 : 0;
        last = now;
        if (still >= 5) return now;
    }
    Fail(step, "party never settled");
}

/* Holds the forward key until the party is `distance` from where it started. */
srVector3T<float> Walk(Game& game, const char* step, float distance)
{
    const srVector3T<float> start = PartyPosition();
    game.hold_key(SDLK_UP, true);
    game.wait(step, [&] { return HorizontalDistance(PartyPosition(), start) > distance; }, 20);
    game.hold_key(SDLK_UP, false);
    return Settle(game, step);
}

int PremadeSlot()
{
    REQUIRE(CountActiveCharacters() == 1);
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (g_status.buffers.XChar[slot].fOccupied) {
            REQUIRE(g_status.buffers.Char[slot].fInParty);
            return slot;
        }
    Fail("party", "no occupied slot");
}

/* A new game with the checked-in character, bypassing party selection. */
void StartPremadeGame(Game& game)
{
    game.install(fs::path(WIZ8_RUNTIME_FIXTURES) / "vi.CHR", "Saves/Characters/vi.CHR");
    ReachMainMenu(game);
    ResetForNewGame();
    W8Character character{};
    REQUIRE(LoadCharacter("vi.CHR", &character, -1));
    REQUIRE((character.record_version == 1 && character.uiExpLevel == 1));
    REQUIRE(character.name == "vi");
    REQUIRE((character.iProfession == W8_PROFESSION_FIGHTER && character.iRace == W8_RACE_HUMAN &&
             character.gender == W8_GENDER_MALE));
    REQUIRE((character.hp_current == 18 && character.experience == 0));
    REQUIRE(AddCharacterToParty(character, -1) >= 0);
    REQUIRE(CountActiveCharacters() == 1);
    RunNewGameOpeningSequence(false, {});
    WaitMainGame(game, "premade-main-game");
}

// The formation layer must keep gameplay dragging independent of its artwork.
/* The interface overlay's color at a point; zero where nothing is drawn. */
int OverlayPixel(SDL_Point point)
{
    Uint8 r, g, b, a;
    REQUIRE(SDL_ReadSurfacePixel(&GetFrameBufferSurface(), point.x, point.y, &r, &g, &b, &a));
    return r | g | b;
}

void FormationUiScenario()
{
    Game game(false);
    StartPremadeGame(game);
    const int slot = PremadeSlot();
    constexpr std::array<SDL_Point, 15> cell_offsets{{
        {90, 28},
        {61, 37},
        {119, 37},
        {152, 90},
        {143, 61},
        {143, 119},
        {90, 152},
        {119, 143},
        {61, 143},
        {28, 90},
        {37, 119},
        {37, 61},
        {90, 72},
        {72, 97},
        {108, 97},
    }};
    const auto center = [&](int cell) {
        return SDL_Point{214 + cell_offsets[cell].x + 16, 60 + cell_offsets[cell].y + 16};
    };
    const int original_row = g_status.formation.positions[slot].bQuadrant;
    const int destination_row = (original_row + 1) % 5;
    const auto destination = center(destination_row * 3);
    const auto begin_drag = [&] {
        const auto& position = gXStatus.edited_formation.positions[slot];
        const auto source = center(position.bQuadrant * 3 + position.bQuadrantSlot);
        game.press_at(source.x, source.y);
        REQUIRE(g_ui.captured != 0);
        for (int frame = 0; frame < 5; ++frame) game.frame();
        REQUIRE(g_ui.captured != 0); // Below the 250 ms hold threshold.
        REQUIRE(game.within_frames(60, [] { return g_ui.captured == 0; }));
        REQUIRE(gXStatus.iCurrentCursor == W8_CURSOR_INVALID_TARGET);
    };
    const auto drop_at = [&](SDL_Point point) {
        game.release_at(point.x - g_cursor_hotspot_x, point.y - g_cursor_hotspot_y);
    };

    OpenFormationPanel();
    game.frame();
    REQUIRE(gXStatus.fReviewCharacterMode);
    begin_drag();
    drop_at(destination);
    REQUIRE(gXStatus.edited_formation.positions[slot].bQuadrant == destination_row);
    REQUIRE(g_status.formation.positions[slot].bQuadrant == original_row);

    // Reset preserves the live formation; dropping on the panel edge cancels.
    game.click_at(242, 278);
    REQUIRE(gXStatus.edited_formation.positions[slot].bQuadrant == original_row);
    begin_drag();
    drop_at({220, 70});
    REQUIRE(gXStatus.edited_formation.positions[slot].bQuadrant == original_row);

    begin_drag();
    drop_at(destination);
    game.click_at(360, 276); // Accept, with teardown deferred past button dispatch.
    REQUIRE_FALSE(gXStatus.fReviewCharacterMode);
    REQUIRE(g_status.formation.positions[slot].bQuadrant == destination_row);
    REQUIRE(g_ui.captured == 0);

    OpenFormationPanel();
    game.frame();
    REQUIRE(gXStatus.fReviewCharacterMode);
    REQUIRE(OverlayPixel({320, 180}));
    game.click_at(394, 276); // Cancel after re-entry.
    REQUIRE_FALSE(gXStatus.fReviewCharacterMode);
    REQUIRE(!OverlayPixel({320, 180})); // Closing erases the panel.
    REQUIRE(g_status.formation.positions[slot].bQuadrant == destination_row);
}

std::vector<fs::path> SaveFiles(const fs::path& user)
{
    std::vector<fs::path> saves;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(user / "Saves", ignored))
        if (entry.is_regular_file() && entry.path().extension() == ".SAV" &&
            entry.path().stem() != "CurrentGame")
            saves.push_back(entry.path());
    return saves;
}

/* Quick-save through its key binding; returns the written save. */
fs::path QuickSave(Game& game)
{
    REQUIRE(SaveFiles(game.user_root()).empty());
    game.command(W8_MGS_COMMAND_QUICK_SAVE);
    const auto saves = SaveFiles(game.user_root());
    REQUIRE((saves.size() == 1 && fs::file_size(saves[0]) > 0));
    REQUIRE(GetSaveGameLevel(saves[0].stem().string().c_str()) == int(g_status.current_level));
    return saves[0];
}

void ExpectAt(const char* step, const srVector3T<float>& expected)
{
    const srVector3T<float> now = PartyPosition();
    const float distance = HorizontalDistance(now, expected);
    std::printf("%s: at (%g %g %g), expected (%g %g %g)\n", step, now.x, now.y, now.z, expected.x,
                expected.y, expected.z);
    if (!std::isfinite(distance) || !std::isfinite(now.y) || distance > 1.0f ||
        std::abs(now.y - expected.y) > 1.0f)
        Fail(step, "party restored " + std::to_string(distance) + " units from the save");
}

/* --- Scenarios ----------------------------------------------------------- */

void VideoScenario(bool intro_seen)
{
    Game game(true);
    // The asset root can contain a returning player's Wiz8.CFG.
    // Set the scenario explicitly after startup loads that configuration.
    g_settings.intro_seen = intro_seen;
    // The startup logo movie plays on the intro screen.
    game.wait("logo", [] { return g_runtime_test_hooks.movie_frames_presented >= 1; });
    REQUIRE(g_current_screen_state.id == W8_SCREEN_INTRO);
    unsigned lit = 0;
    game.wait("logo-content", [&] {
        if (g_runtime_test_hooks.movie_frames_presented > 150)
            Fail("logo-content", "movie frames are black");
        for (unsigned pixel : game.read_frame()) lit += (pixel & 0xf0f0f0) != 0;
        return lit > 1000;
    });
    const unsigned logo_frames = g_runtime_test_hooks.movie_frames_presented;
    game.key(SDLK_ESCAPE);
    if (!intro_seen) {
        // On a first launch, skipping the logo starts the title movie.
        game.wait("title",
                  [&] { return g_runtime_test_hooks.movie_frames_presented >= logo_frames + 5; });
        REQUIRE(g_current_screen_state.id == W8_SCREEN_INTRO);
        game.key(SDLK_ESCAPE);
    }
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    REQUIRE(g_settings.intro_seen);
    if (intro_seen) REQUIRE(g_runtime_test_hooks.movie_frames_presented == logo_frames);
    std::printf("video: %u movie frames presented\n", g_runtime_test_hooks.movie_frames_presented);
}

void GameStartScenario()
{
    Game game(false);
    StartGame(game);
    W8WorldCameraState start{};
    start = cameraState(*GetWorld());
    // Hold the forward key until the camera has moved, through the real binding.
    game.hold_key(SDLK_UP, true);
    W8WorldCameraState now{};
    game.wait(
        "walk",
        [&] {
            now = cameraState(*GetWorld());
            const float dx = now.position.x - start.position.x,
                        dz = now.position.z - start.position.z;
            return std::sqrt(dx * dx + dz * dz) > 1.0f;
        },
        20);
    game.hold_key(SDLK_UP, false);
    std::printf("game-start: main game after %lu frames, walked to (%g %g %g)\n", game.frames(),
                now.position.x, now.position.y, now.position.z);
}

void InventoryChestScenario()
{
    const float motion_clamp = g_camera_motion_clamp;
    const float motion_divisor = g_camera_motion_divisor;
    Game game(false);
    StartPremadeGame(game);
    Settle(game, "settle-before-chest");
    Trigger* chest = nullptr;
    for (auto* trigger : GetWorld()->triggers)
        if (trigger->initial_action == 3 && trigger->inline_action_data[0]) {
            trigger->GenerateItemGroup();
            if (trigger->world_item_group && trigger->world_item_group->next) {
                chest = trigger;
                break;
            }
        }
    REQUIRE(chest);
    // Request the same picker that an opened chest raises. This isolates loot
    // handling from the chest's lock/trap skill rolls and prop animation.
    chest->flags |= W8_TRIGGER_ITEM_PICKER;
    game.wait("chest-picker", [] { return g_modal_owner != nullptr; });
    game.frame();
    REQUIRE(gXStatus.world_update_blocked);
    auto* picker = static_cast<W8TriggerItemPickerDialog*>(g_modal_owner.get());
    REQUIRE_FALSE(picker->items.empty());
    const auto click_button = [&](int index) {
        auto& button = *picker->m_buttons[index];
        game.click_at(button.Bounds().x + button.Bounds().w / 2,
                      button.Bounds().y + button.Bounds().h / 2);
    };
    // A full pool can still absorb part of an ammunition stack. The picker
    // must retain only the remainder, including after a second failed attempt.
    const auto pool = std::vector<W8ItemInstance>(std::begin(g_status.party_item_pool),
                                                  std::end(g_status.party_item_pool));
    const auto pool_count = g_status.party_item_count;
    const auto original_item = picker->items.front()->item;
    const auto picker_count = picker->items.size();
    int stack_id = -1, nonstack_id = -1;
    for (unsigned i = 0; i < gXStatus.uiItemsInDatabase; ++i) {
        const auto& record = g_item_records[i];
        if (record.quantity_kind == W8_ITEM_QUANTITY_STACK && record.maximum_quantity >= 5)
            stack_id = i;
        else if (record.quantity_kind != W8_ITEM_QUANTITY_STACK)
            nonstack_id = i;
        if (stack_id >= 0 && nonstack_id >= 0) break;
    }
    REQUIRE(stack_id >= 0);
    REQUIRE(nonstack_id >= 0);
    auto& remainder = picker->items.front()->item;
    ReplaceOrCreateItem(&remainder, stack_id, true, false, false);
    remainder.stack_count = 5;
    W8ItemInstance filler{};
    ReplaceOrCreateItem(&filler, nonstack_id, true, false, false);
    std::fill(std::begin(g_status.party_item_pool), std::end(g_status.party_item_pool), filler);
    g_status.party_item_count = 500;
    g_status.party_item_pool[0] = remainder;
    const auto maximum = g_item_records[stack_id].maximum_quantity;
    g_status.party_item_pool[0].stack_count = maximum - 2;
    std::fill(picker->flags.begin(), picker->flags.end(), 0);
    picker->flags.front() = 1;
    click_button(1);
    REQUIRE(picker->items.size() == picker_count);
    REQUIRE(g_status.party_item_count == 500);
    REQUIRE(g_status.party_item_pool[0].stack_count == maximum);
    REQUIRE(remainder.stack_count == 3);
    click_button(1);
    REQUIRE(remainder.stack_count == 3);
    remainder = original_item;
    std::copy(pool.begin(), pool.end(), std::begin(g_status.party_item_pool));
    g_status.party_item_count = pool_count;
    RedistributePartyEncumbrance();
    std::fill(picker->flags.begin(), picker->flags.end(), 0);
    click_button(0); // Select all.
    click_button(1); // Take selected items into the party pool.
    game.wait("close-chest-picker", [] { return g_modal_owner == nullptr; });
    REQUIRE(g_modal_owner == nullptr);
    REQUIRE_FALSE(gXStatus.world_update_blocked);
    REQUIRE((chest->flags & W8_TRIGGER_ITEM_PICKER) == 0);
    REQUIRE(g_camera_motion_clamp == motion_clamp);
    REQUIRE(g_camera_motion_divisor == motion_divisor);
    Walk(game, "walk-after-chest", 5);
    int potion_slot = -1;
    for (int i = 0; i < 500; ++i)
        if (g_status.party_item_pool[i].iItemNo >= 0 &&
            g_item_records[g_status.party_item_pool[i].iItemNo].unidentified_name_index == 102) {
            potion_slot = i;
            break;
        }
    REQUIRE(potion_slot >= 0);
    // Exercise the purple-potion icon with the unidentified overlay as well.
    g_status.party_item_pool[potion_slot].identified = false;
    game.command(W8_MGS_COMMAND_INVENTORY);
    game.wait_screen("inventory", W8_SCREEN_CAMP);
    REQUIRE(g_item_video_objects.LoadedImageCount() > 2);
    REQUIRE(g_camera_motion_clamp == motion_clamp);
    REQUIRE(g_camera_motion_divisor == motion_divisor);
    REQUIRE(g_pending_main_menu_message.empty());
    int potion_cell = -1;
    for (unsigned i = 0; i < std::min(8u, g_camp_screen->item_list_count); ++i)
        if (g_camp_screen->item_list[i] == potion_slot) potion_cell = i;
    REQUIRE(potion_cell >= 0);
    game.click_at(535 + (potion_cell & 1) * 49, 219 + (potion_cell >> 1) * 57);
    REQUIRE(g_status.item_in_cursor);
    game.click_at(34, 219);
    REQUIRE_FALSE(g_status.item_in_cursor);
    game.click_at(320, 170); // Keep the cursor outside the grids for pixel comparisons.
    const auto rendered = game.read_frame();
    const auto& surface = GetFrameBufferSurface();
    std::vector<unsigned> expected(SCREEN_WIDTH * SCREEN_HEIGHT);
    REQUIRE(SDL_ConvertPixels(SCREEN_WIDTH, SCREEN_HEIGHT, surface.format, surface.pixels,
                              surface.pitch, SDL_PIXELFORMAT_XRGB8888, expected.data(),
                              SCREEN_WIDTH * sizeof(unsigned)));
    unsigned mismatches = 0;
    for (int y = 192; y < 417; ++y)
        for (int x : {11, 60, 512, 561})
            for (int column = x; column < x + 46; ++column)
                for (unsigned shift : {0u, 8u, 16u}) {
                    const auto index = y * SCREEN_WIDTH + column;
                    mismatches += std::abs(int(rendered[index] >> shift & 255) -
                                           int(expected[index] >> shift & 255)) > 1;
                }
    REQUIRE(mismatches == 0);
    Snapshot("inventory", rendered);
    game.key(SDLK_ESCAPE);
    game.wait_screen("close-inventory", W8_SCREEN_MAIN_GAME);
    Walk(game, "walk-after-inventory", 5);
    const auto start = PartyPosition();
    game.hold_key(SDLK_DOWN, true);
    game.wait(
        "walk-back-after-inventory", [&] { return HorizontalDistance(PartyPosition(), start) > 5; },
        20);
    game.hold_key(SDLK_DOWN, false);
    W8WorldCameraState before{}, after{};
    before = cameraState(*GetWorld());
    game.hold_key(SDLK_LEFT, true);
    game.wait(
        "turn-left-after-inventory",
        [&] {
            after = cameraState(*GetWorld());
            return std::abs(after.yaw[0] - before.yaw[0]) > 0.1f;
        },
        20);
    game.hold_key(SDLK_LEFT, false);
}

void CheckWorldRender(Game& game)
{
    W8World* world = GetWorld();
    unsigned enabled = 0;
    for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i)
        if (world->psrMeshes[i] && !world->psrMeshes[i]->testFlag(srNode::FLAG_DISABLE)) ++enabled;
    REQUIRE(enabled > 0);

    // The same scene and UI with only the world pass switched off is the control.
    REQUIRE((g_world_render_enabled && !g_world_blacked_out));
    RenderFrame();
    const auto with_world = g_gerd->getStatistics();
    const unsigned polygons = world->level->m_submitted_polygons;
    const auto visible = game.read_frame();
    g_world_render_enabled = 0;
    RenderFrame();
    const auto without_world = g_gerd->getStatistics();
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
    REQUIRE(polygons > 0);
    REQUIRE(with_world.draw_calls > without_world.draw_calls);
    REQUIRE(with_world.input_triangles > without_world.input_triangles);
    // The sky is a separate pass and stays in the control frame; the world
    // (ground, cliffs, water at the Monastery start) covers about half the view.
    REQUIRE(changed > viewport / 4);
}

// Exercise the screen-template behaviours that generic widget dispatch cannot replace.
void CheckGameplayUi(Game& game)
{
    const int slot = PremadeSlot();
    const auto original_formation = g_status.formation;
    OpenFormationPanel();
    game.frame();
    const auto position = g_status.formation.positions[slot];
    const int source = position.bQuadrant * 3 + position.bQuadrantSlot;
    REQUIRE((source >= 0 && source < 15));
    const int destination = ((position.bQuadrant + 1) % 5) * 3;
    constexpr std::array<SDL_Point, 15> cells{{
        {90, 28},
        {61, 37},
        {119, 37},
        {152, 90},
        {143, 61},
        {143, 119},
        {90, 152},
        {119, 143},
        {61, 143},
        {28, 90},
        {37, 119},
        {37, 61},
        {90, 72},
        {72, 97},
        {108, 97},
    }};
    game.drag({214 + cells[source].x, 60 + cells[source].y, 32, 32},
              {214 + cells[destination].x, 60 + cells[destination].y, 32, 32});
    REQUIRE(gXStatus.edited_formation.positions[slot].bQuadrant == destination / 3);
    game.click_at(360, 276);
    REQUIRE_FALSE(gXStatus.fReviewCharacterMode);
    REQUIRE(g_status.formation.positions[slot].bQuadrant == destination / 3);
    g_status.formation = original_formation;
    RefreshFormationBoard();

    // Give the fixture a real learned spell and mana; exercise pointer routing,
    // realm tooltips and power selection with the licensed spell data.
    auto& character = g_status.buffers.Char[slot];
    const auto original_character = character;
    character.iProfession = W8_PROFESSION_MAGE;
    character.skills[W8_SKILL_FIRE_MAGIC].level = 100;
    LearnSpell(&character, W8_SPELL_LIGHT, false);
    const auto realm = g_spell_records[W8_SPELL_LIGHT].realm;
    character.sp_max[realm] = character.iSPLeft[realm] = 100;
    REQUIRE(OpenSpellCastingView(slot));
    game.frame();
    const auto realm_bounds = gpSCSV->realm_buttons[realm]->Bounds();
    game.move_pointer(0, 200);
    VideoRemoveToolTip();
    game.move_pointer(realm_bounds.x + 5, realm_bounds.y + 5);
    ResetUiHelp(false);
    REQUIRE(HasScreenTransitionObjects());
    game.click_at(realm_bounds.x + realm_bounds.w / 2, realm_bounds.y + realm_bounds.h / 2);
    REQUIRE(gpSCSV->iSpellRealm == realm);
    REQUIRE(gpSCSV->uiSpellsInList == 1);
    game.click_at(250, 372); // First spell in the ordinary text-box list.
    REQUIRE(gpSCSV->uiSpellToCast == W8_SPELL_LIGHT);
    game.click_at(0x1f4, 0x172);
    if (gpSCSV) {
        REQUIRE(gpSCSV->iSpellPower == 1);
        game.click_at(0x247, 0x179);
        REQUIRE(!gpSCSV);
    } else {
        // Target-free spells may commit in the same frame as power selection.
        REQUIRE(g_status.buffers.XChar[slot].spell_id == W8_SPELL_LIGHT);
        REQUIRE(g_status.buffers.XChar[slot].spell_detail.spell.power_level == 2);
    }
    character = original_character;

    const auto original_effect = g_status.effect_slots[0];
    g_status.effect_slots[0].active = true;
    g_status.effect_slots[0].effect_id = W8_SPELL_LIGHT;
    g_status.effect_slots[0].amount = 1;
    g_status.effect_slots[0].duration = 600;
    RefreshSpellIconHudRows();
    game.frame();
    game.move_pointer(0, 200);
    VideoRemoveToolTip();
    game.move_pointer(0x277, 9);
    ResetUiHelp(false);
    REQUIRE(HasScreenTransitionObjects());
    g_status.effect_slots[0] = original_effect;
    RefreshSpellIconHudRows();

    const auto original_experience = character.experience;
    const auto original_notice = g_status.buffers.XChar[slot].portrait_advance;
    character.experience = character.experience_goal;
    g_status.buffers.XChar[slot].portrait_advance = true;
    EnablePortraitAdvanceRegions();
    UpdatePortraitAdvanceButtons();
    game.click_at(((slot & 1) ? 0x23b : 0) + 0x22, (slot / 2) * 0x55 + 0x4f);
    game.wait_screen("level-up-character", W8_SCREEN_CHARACTER);
    REQUIRE(g_character_screen->m_mode == 2);
    REQUIRE(g_character_screen->m_original == &g_status.buffers.Char[slot]);
    game.key(SDLK_ESCAPE);
    game.key(SDLK_RETURN); // Confirm abandoning the level-up edit.
    game.wait_screen("cancel-level-up", W8_SCREEN_MAIN_GAME);
    character.experience = original_experience;
    g_status.buffers.XChar[slot].portrait_advance = original_notice;
    UpdatePortraitAdvanceButtons();
}

// Load and render the retail level, evict resident textures, then reload the
// saved level through the shipped loading screen. Leave gameplay resident at
// scope exit so Application shutdown exercises the same owners as gameplay.
void ResourceLifecycleScenario()
{
    Game game(false);
    StartPremadeGame(game);
    // Rendering and resource recycling use the same live world and GPU session.
    CheckWorldRender(game);
    CheckGameplayUi(game);
    // Automap buttons own their sprite sheets. Re-entering the screen must
    // rebuild all sixteen controls after the previous owners are destroyed.
    for (int visit = 0; visit != 2; ++visit) {
        game.command(W8_MGS_COMMAND_AUTOMAP);
        game.wait_screen("automap-buttons", W8_SCREEN_AUTOMAP);
        REQUIRE(g_automap_buttons);
        for (int index = 0; index != 16; ++index) {
            REQUIRE(g_automap_buttons->buttons[index]);
            REQUIRE(g_automap_buttons->buttons[index]->Bounds().w > 0);
            REQUIRE(g_automap_buttons->buttons[index]->Bounds().h > 0);
            g_automap_buttons->buttons[index]->m_dirty = true;
            g_automap_buttons->buttons[index]->Draw(GetFrameBufferSurface());
        }
        game.key(SDLK_ESCAPE);
        game.wait_screen("automap-buttons-released", W8_SCREEN_MAIN_GAME);
        REQUIRE(!g_automap_buttons);
    }
    game.command(W8_MGS_COMMAND_OPTIONS);
    game.wait_screen("options-menu", W8_SCREEN_OPTIONS);
    game.click_at(30, 250);
    REQUIRE(g_options_screen->m_selected_panel == 2);
    game.click_at(580, 430);
    REQUIRE(g_options_screen->m_panel[2]->m_current == 1);
    game.click_at(290, 430);
    REQUIRE(g_options_screen->m_panel[2]->m_current == 0);
    game.press_at(30, 60);
    game.release_at(200, 190);
    REQUIRE(g_options_screen->m_selected_panel == 2);
    game.click_at(30, 60); // Artwork hotspot selects the same panel as its button.
    REQUIRE(g_options_screen->m_selected_panel == 0);
    const float combat_speed = g_options_values.combat_speed;
    game.click_at(480, 50);
    const float clicked_speed = g_options_values.combat_speed;
    REQUIRE(clicked_speed < 0.5f);
    game.press_at(480, 50);
    game.move_pointer(520, 50);
    REQUIRE(g_options_values.combat_speed > clicked_speed);
    game.release_at(200, 190); // Captured release outside ends the drag.
    const float released_speed = g_options_values.combat_speed;
    game.move_pointer(490, 50);
    REQUIRE(g_options_values.combat_speed == released_speed);
    game.click_at(30, 250); // An inactive panel must not retain slider input.
    game.click_at(480, 50);
    REQUIRE(g_options_values.combat_speed == released_speed);
    g_options_values.combat_speed = combat_speed;
    game.click_at(20, 445);
    game.wait_screen("options-menu-return", W8_SCREEN_MAIN_GAME);
    // Exercise a second screen's input, teardown and return-stack lifecycle.
    const std::size_t return_depth = g_screen_return_stack.size();
    game.command(W8_MGS_COMMAND_JOURNAL);
    game.wait_screen("journal", W8_SCREEN_JOURNAL);
    REQUIRE(g_screen_return_stack.size() == return_depth + 1);
    REQUIRE(g_screen_return_stack.back().id == W8_SCREEN_MAIN_GAME);
    // Exercise the ordinary toggle and close buttons.
    game.click_at(540, 450);
    game.click_at(540, 450);
    game.click_at(600, 450);
    game.wait_screen("journal-return", W8_SCREEN_MAIN_GAME);
    REQUIRE(g_screen_return_stack.size() == return_depth);
    // Returning re-arms the interface buttons; the roof buttons switch views.
    const auto click_roof = [&](int index) {
        const auto& bounds = g_main_game_state->hud_buttons.roof[index].Bounds();
        game.click("roof-button", {bounds.x + bounds.w / 2, bounds.y + bounds.h / 2});
    };
    click_roof(2);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_RADAR);
    click_roof(0);
    REQUIRE(g_settings.main_ui_mode == W8_MAIN_UI_MODE_PORTRAITS);
    REQUIRE((GetWorld() && GetWorld()->level));

    const int level = g_status.current_level;
    const auto anchor = Walk(game, "settle-before-save", 2.0f);
    QuickSave(game);
    { // One save/load cycle covers eviction, reconstruction and live teardown.
        auto* world = GetWorld();
        REQUIRE((world && world->octree && world->level));
        unsigned linked = 0, animated = 0;
        for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i) {
            auto* instance = world->psrMeshes[i];
            if (!instance) continue;
            auto* model = static_cast<stMeshModel*>(instance->getModel());
            if (model && !model->parts.empty()) ++linked;
            if (model && MeshHasAnimatedTexture(model)) ++animated;
        }
        std::printf("resource-lifecycle: %u linked heads, %u animated textures\n", linked,
                    animated);
        REQUIRE(linked > 0);
        REQUIRE(animated > 0);
        for (int frame = 0; frame != 4; ++frame) game.frame();
        RenderFrame();
        REQUIRE(world->level->m_submitted_polygons > 0);
        REQUIRE(g_gerd->getTextureCacheUsed() > 0);
        g_gerd->invalidateTextureCache();
        RenderFrame();
        REQUIRE(g_gerd->getTextureCacheUsed() > 0);
        REQUIRE(world->level->m_submitted_polygons > 0);
        game.command(W8_MGS_COMMAND_QUICK_LOAD);
        bool loading = false;
        game.wait("reload-level", [&] {
            loading = loading || g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT;
            return loading && g_current_screen_state.id == W8_SCREEN_MAIN_GAME && game.settled();
        });
        Settle(game, "reload-level");
        REQUIRE(g_status.current_level == level);
        ExpectAt("reloaded-position", anchor);
    }
    RenderFrame();
    REQUIRE(g_gerd->getTextureCacheUsed() > 0);
}

/* The UI load test consumes the save written by the quick-save regression.
   Reusing that real saved game avoids another full runtime startup. */
void WriteSaveFixture(const fs::path& output, const fs::path& save, const srVector3T<float>& anchor)
{
    fs::create_directories(output);
    fs::copy_file(save, output / "Fixture.SAV", fs::copy_options::overwrite_existing);
    std::ofstream metadata(output / "Fixture.txt");
    metadata << std::setprecision(std::numeric_limits<float>::max_digits10) << anchor.x << ' '
             << anchor.y << ' ' << anchor.z << ' ' << g_status.current_level << ' '
             << g_status.buffers.Char[PremadeSlot()].experience << ' '
             << g_status.buffers.Char[PremadeSlot()].hp_current << '\n';
    metadata.close();
    REQUIRE(metadata);
    std::printf("save-fixture: wrote %s (%ju bytes)\n", (output / "Fixture.SAV").string().c_str(),
                std::uintmax_t(fs::file_size(output / "Fixture.SAV")));
}

/* Quick save, walk away, quick load: the party is back where it saved. */
/* Toggle combat and engage a live hostile monster, so the fight persists. */
W8MonsterInfo* EnterCombat(Game& game)
{
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    REQUIRE(game.within_frames(4, [] { return gXStatus.fCombatMode != 0; }));
    REQUIRE(g_combat_state);
    W8MonsterInfo* monster = GetNextMonsterInfo(true);
    while (monster && (!monster->fActive || !monster->p3D || !monster->hp_current ||
                       monster->ubDisposition != W8_DISPOSITION_HOSTILE)) {
        monster = GetNextMonsterInfo(false);
    }
    REQUIRE(monster);
    if (!monster->fInCombat) {
        MonsterInfoEnterCombat(monster);
    }
    return monster;
}

void QuickSaveScenario()
{
    Game game(false);
    StartPremadeGame(game);
    const srVector3T<float> anchor = Walk(game, "walk", 2.0f);
    const int level = g_status.current_level;
    const unsigned experience = g_status.buffers.Char[PremadeSlot()].experience;
    const auto health = g_status.buffers.Char[PremadeSlot()].hp_current;
    REQUIRE(health > 1);
    const fs::path save = QuickSave(game);
    WriteSaveFixture(WIZ8_RUNTIME_SAVE_FIXTURE, save, anchor);
    Walk(game, "walk-away", 4.0f);
    REQUIRE(HorizontalDistance(PartyPosition(), anchor) > 2.0f);
    // Change a persistent field as well as the camera: loading must replace
    // the character record, not just reposition the party.
    g_status.buffers.Char[PremadeSlot()].experience = experience + 123;
    g_status.buffers.Char[PremadeSlot()].hp_current = health - 1;
    // Loading ends the fight while the main game is suspended.
    EnterCombat(game);

    game.command(W8_MGS_COMMAND_QUICK_LOAD);
    bool loading = false;
    game.wait("quick-load", [&] {
        loading = loading || g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT;
        return loading && g_current_screen_state.id == W8_SCREEN_MAIN_GAME && game.settled();
    });
    Settle(game, "quick-load");
    ExpectAt("quick-load", anchor);
    REQUIRE(CountActiveCharacters() == 1);
    REQUIRE(g_status.current_level == level);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].experience == experience);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].hp_current == health);
    REQUIRE(!gXStatus.fCombatMode);
    std::printf("quick-save: %s round-tripped after %lu frames\n", save.filename().string().c_str(),
                game.frames());
}

/* Main menu -> Load Game -> the fixture save -> its party and position. */
void LoadGameScenario(const fs::path& fixture)
{
    /* Construct Game first so a missing WIZ8_ASSET_ROOT skips rather than
       failing on the absent fixture when save-fixture itself skipped. */
    Game game(false);
    srVector3T<float> anchor{};
    int level = -1;
    unsigned experience = 0;
    unsigned health = 0;
    std::ifstream metadata(fixture / "Fixture.txt");
    REQUIRE(metadata >> anchor.x >> anchor.y >> anchor.z >> level >> experience >> health);
    REQUIRE((std::isfinite(anchor.x) && std::isfinite(anchor.y) && std::isfinite(anchor.z)));
    REQUIRE(fs::is_regular_file(fixture / "Fixture.SAV"));
    game.install(fixture / "Fixture.SAV", "Saves/Fixture.SAV");
    ReachMainMenu(game);
    game.click("load-game", MainMenuRegion(2));
    W8OptionsSaveLoadPanel* panel = nullptr;
    game.wait("load-panel", [&] {
        if (g_current_screen_state.id != W8_SCREEN_OPTIONS || !game.settled() ||
            !g_options_screen || g_options_screen->m_selected_panel != 4)
            return false;
        // Panel set 4 holds the single load panel (W8OptionsSaveLoadPanel 11).
        W8OptionsPanelSet* set = g_options_screen->m_panel[4].get();
        if (!set || set->m_current < 0 ||
            static_cast<std::size_t>(set->m_current) >= set->m_panels.size())
            return false;
        panel = static_cast<W8OptionsSaveLoadPanel*>(set->m_panels[set->m_current].get());
        return panel->m_panel == 11 && !panel->m_rows.empty();
    });
    // The only save is the first row; select it, then press Load.
    game.click_at(300, 40);
    REQUIRE(panel->m_selected == 0);
    game.click_at(540, 445);
    WaitMainGame(game, "loaded");
    Settle(game, "loaded");
    ExpectAt("loaded", anchor);
    REQUIRE(CountActiveCharacters() == 1);
    REQUIRE(g_status.current_level == level);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].experience == experience);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].hp_current == health);
    std::printf("load-game: loaded the fixture after %lu frames\n", game.frames());
}

/* --- NPC dialogue ---------------------------------------------------------- */

/* The conversation waits for the player: nothing spoken, queued or asked. */
bool DialogueAwaitingPlayer()
{
    return gXStatus.fNpcDialogueMode && !IsNpcScriptSessionActive() &&
           !g_npc_dialogue.has_queued_actions() &&
           g_npc_dialogue.reply() == W8NpcReplyState::none && g_modal_owner == nullptr;
}

/* A voiced line ends only when its stream drains, which the offscreen run
   does not wait for; skip each one with Escape, as the player can. */
void WaitDialogue(Game& game, const char* step, const std::function<bool()>& ready)
{
    game.wait(step, [&] {
        if (ready()) return true;
        if (IsNpcVoicePlaying() && g_modal_owner == nullptr) {
            game.key(SDLK_ESCAPE);
        }
        return false;
    });
}

void StartConversation(Game& game, const char* step, W8NpcState* npc)
{
    REQUIRE(!gXStatus.fNpcDialogueMode);
    ForwardNpcScriptNotice(npc, nullptr, -1, false);
    WaitDialogue(game, step, [&] {
        return g_npc_interaction_state->dialogue_npc == npc && DialogueAwaitingPlayer();
    });
}

/* Types a keyword into the transcript's input field and submits it. */
void Ask(Game& game, std::string_view keyword)
{
    REQUIRE(DialogueAwaitingPlayer());
    game.key(SDLK_RETURN);
    REQUIRE((TextFieldGroup::Focused() && TextFieldGroup::Focused()->Editing()));
    game.enter_text(keyword);
    REQUIRE(TextFieldGroup::Focused()->Text(0) == keyword);
    game.key(SDLK_RETURN);
}

/* Reply buttons sit at y=214 (price) or y=219 (options), above the modal body. */
void ChooseFirstReply(Game& game, const char* step)
{
    WaitDialogue(game, step, [] { return g_modal_owner != nullptr; });
    HitTarget* first = nullptr;
    for (const int y : {214, 219}) {
        for (int x = 0; x < 640; ++x) {
            auto* target = UiTargetAt({x, y});
            if (target && target->bounds().h <= 24 &&
                (!first || target->bounds().x < first->bounds().x))
                first = target;
        }
    }
    REQUIRE(first != nullptr);
    const auto bounds = first->bounds();
    game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    game.wait(step, [] { return g_modal_owner == nullptr; });
}

/* Buys one paid answer: the price prompt, "yes", the gold and the fact. */
void BuyAnswer(Game& game, const char* step, W8FactId fact)
{
    WaitDialogue(game, step, [] {
        return g_modal_owner != nullptr && g_npc_dialogue.price_check().has_value();
    });
    const unsigned gold = g_status.party_gold;
    const int price = g_npc_dialogue.price_check()->price;
    CAPTURE(step, gold, price);
    REQUIRE((price > 0 && unsigned(price) <= gold));
    REQUIRE(GetFact(fact) == 0);
    ChooseFirstReply(game, step);
    WaitDialogue(game, step, DialogueAwaitingPlayer);
    REQUIRE(g_status.party_gold == gold - unsigned(price));
    REQUIRE(GetFact(fact) != 0);
}

/* Escape asks for the farewell; once it has been said, Escape dismisses it. */
void SayFarewell(Game& game, const char* step)
{
    REQUIRE(DialogueAwaitingPlayer());
    game.key(SDLK_ESCAPE);
    game.wait(step, [&] {
        if (!gXStatus.fNpcDialogueMode) return true;
        if (IsNpcScriptSessionActive()) game.key(SDLK_ESCAPE);
        return false;
    });
    WaitMainGame(game, step);
    REQUIRE(!g_npc_dialogue.has_queued_actions());
}

/* A conversation with Burz, the merchant beside the starting area: the
   messenger keyword defers a yes/no option list behind its question, both
   paid answers charge the party, and the farewell ends the session so a
   second visit starts afresh. */
void NpcDialogueScenario()
{
    Game game(false);
    StartPremadeGame(game);
    Settle(game, "settle");
    W8NpcState* burz = FindNpcStateByName("Burz");
    REQUIRE(burz);
    g_status.party_gold = 200;

    StartConversation(game, "greeting", burz);
    Ask(game, "messenger");
    ChooseFirstReply(game, "messenger-options");
    BuyAnswer(game, "messenger-price", W8_FACT_QUEST_BURZ_MARTEN_TRYNTON);
    Ask(game, "ship");
    BuyAnswer(game, "ship-price", W8_FACT_QUEST_BURZ_REPORT_SHIP);
    SayFarewell(game, "farewell");
    Walk(game, "walk-after-farewell", 2.0f);

    StartConversation(game, "second-greeting", burz);
    // Keyword popups own their editor and restore the transcript's focus.
    auto* transcript_fields = TextFieldGroup::Focused();
    REQUIRE(transcript_fields);
    W8NpcQuoteEntry keyword_request{};
    keyword_request.kind = W8_NPC_ENTRY_KEYWORD_INPUT;
    OpenNpcDialog(&keyword_request, 0);
    game.frame();
    REQUIRE(TextFieldGroup::Focused() != transcript_fields);
    game.enter_text("cancelled");
    game.key(SDLK_ESCAPE);
    REQUIRE_FALSE(g_modal_owner);
    REQUIRE(TextFieldGroup::Focused() == transcript_fields);
    OpenNpcDialog(&keyword_request, 0);
    game.frame();
    game.enter_text("hello");
    REQUIRE(TextFieldGroup::Focused()->Text(0) == "hello");
    game.key(SDLK_RETURN);
    REQUIRE_FALSE(g_modal_owner);
    REQUIRE(TextFieldGroup::Focused() == transcript_fields);
    WaitDialogue(game, "keyword-reply", DialogueAwaitingPlayer);
    SayFarewell(game, "second-farewell");
    std::printf("npc-dialogue: two visits after %lu frames\n", game.frames());
}

/* Dismiss the quit prompt, then combat through its key bindings and End Turn
   button: enter, run a defended round, kill a monster, leave. */
void CombatScenario()
{
    Game game(false);
    StartPremadeGame(game);
    Settle(game, "settle");
    // A closed dialog erases itself from the interface overlay.
    game.key(SDLK_ESCAPE);
    REQUIRE(game.within_frames(4, [] { return g_modal_owner && OverlayPixel({320, 240}); }));
    game.key(SDLK_ESCAPE);
    REQUIRE(game.within_frames(4, [] { return !g_modal_owner && !OverlayPixel({320, 240}); }));
    W8MonsterInfo* monster = EnterCombat(game);
    const unsigned experience = g_status.buffers.Char[PremadeSlot()].experience;
    const int kills = g_status.buffers.Char[PremadeSlot()].kill_count;
    const unsigned reward = GetMonsterExperience(GetMonsterDataForInfo(monster));
    REQUIRE(reward > 0);

    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (g_status.buffers.XChar[slot].fOccupied)
            ChooseAction(slot, W8_ACTION_DEFEND, -1, nullptr, false, 1);
    const unsigned round = g_combat_state->round_count;
    // Interface buttons take input again after the level load.
    const auto& end_turn = g_main_game_state->hud_buttons.combat_stance[0].Bounds();
    game.click("end-turn", {end_turn.x + end_turn.w / 2, end_turn.y + end_turn.h / 2});
    REQUIRE((g_combat_state && g_combat_state->round_count == round + 1));
    game.wait("round", [&] {
        REQUIRE(g_combat_state);
        return !g_combat_state->execution_active;
    });
    REQUIRE((gXStatus.fCombatMode && monster->fInCombat));
    W8TargetSource attacker{};
    attacker.iType = W8_TARGET_SOURCE_CHARACTER;
    attacker.iChar = PremadeSlot();
    attacker.iMonsterID = -1;
    const int monster_id = monster->location_id;
    CAPTURE(monster_id, round, game.frames());
    ApplyDamageToMonster(monster, monster->hp_current, &attacker, false, 1, 0, nullptr, false);
    // Resolve by ID after death/cleanup rather than relying on a retained pointer.
    const W8MonsterInfo* dead = FindMonster(monster_id, false);
    REQUIRE((dead && !dead->fActive && dead->hp_current == 0 &&
             dead->uiCondition[W8_CONDITION_DEAD] != 0));

    if (gXStatus.fCombatMode) game.command(W8_MGS_COMMAND_START_COMBAT_ROUND);
    game.wait("leave-combat", [] { return gXStatus.fCombatMode == 0; }, 10);
    REQUIRE(!g_combat_state);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].experience == experience + reward);
    REQUIRE(g_status.buffers.Char[PremadeSlot()].kill_count == kills + 1);
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    REQUIRE(game.within_frames(4, [] { return gXStatus.fCombatMode != 0; }));
    REQUIRE((g_combat_state && g_combat_state->round_count == 0));
    REQUIRE(g_combat_state->attack_report.reports.empty());
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    game.wait("leave-combat-again", [] { return gXStatus.fCombatMode == 0; }, 10);
    REQUIRE(!g_combat_state);
    // Shutting down mid-fight ends it before the interface is released.
    EnterCombat(game);
    std::printf("combat: round, kill and exit after %lu frames\n", game.frames());
}

} // namespace

static void RequireAssets()
{
    const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
    if (!assets || !fs::is_directory(assets)) SKIP("WIZ8_ASSET_ROOT is not set");
    g_logged_errors.clear();
}

static void CheckGameErrors()
{
    for (const auto& message : g_logged_errors) UNSCOPED_INFO(message);
    REQUIRE(g_logged_errors.empty());
}

template<class Scenario>
void RunScenario(Scenario scenario)
{
    RequireAssets();
    scenario();
    CheckGameErrors();
}

TEST_CASE("main-menu-ui", "[runtime]")
{
    RunScenario(MainMenuUiScenario);
}

TEST_CASE("formation-ui", "[runtime]")
{
    RunScenario(FormationUiScenario);
}

TEST_CASE("video", "[runtime]")
{
    RunScenario([&] { VideoScenario(false); });
}

TEST_CASE("video-seen", "[runtime]")
{
    RunScenario([&] { VideoScenario(true); });
}

TEST_CASE("game-start", "[runtime]")
{
    RunScenario([&] { GameStartScenario(); });
}

TEST_CASE("resource-lifecycle", "[runtime]")
{
    RunScenario([&] { ResourceLifecycleScenario(); });
}

TEST_CASE("quick-save", "[runtime]")
{
    RunScenario([&] { QuickSaveScenario(); });
}

TEST_CASE("combat", "[runtime]")
{
    RunScenario([&] { CombatScenario(); });
}

TEST_CASE("inventory-chest", "[runtime]")
{
    RunScenario([&] { InventoryChestScenario(); });
}

TEST_CASE("npc-dialogue", "[runtime]")
{
    RunScenario(NpcDialogueScenario);
}

TEST_CASE("load-game", "[runtime]")
{
    RunScenario([&] { LoadGameScenario(WIZ8_RUNTIME_SAVE_FIXTURE); });
}

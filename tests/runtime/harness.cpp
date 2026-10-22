#include "harness.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_test_run_info.hpp>
#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/vector_float3.hpp>

#include "../native/surface_pixels.h"
#include "temporary_directory.h"
#include "wiz8/audio/audio_test.h"
#include "wiz8/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/render/renderer.h"
#include "wiz8/runtime_test_hooks.h"
#include "wiz8/world/level_session.h"

W8RuntimeTestHooks g_runtime_test_hooks;

std::vector<std::string> g_logged_errors;

namespace {
SDL_LogOutputFunction g_default_log;
void* g_default_log_data;
void CaptureLog(void* data, int category, SDL_LogPriority priority, const char* message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) g_logged_errors.emplace_back(message);
    g_default_log(g_default_log_data, category, priority, message);
}

fs::path RuntimeVariable(const char* variable)
{
    const char* value = SDL_getenv(variable);
    if (!value || !*value) FailStep(variable, "environment variable is not set");
    return value;
}

/* Where CTest collects a scenario's images; empty when unset. */
fs::path ArtifactDirectory()
{
    const char* directory = SDL_getenv("WIZ8_RUNTIME_ARTIFACT_DIR");
    return directory ? directory : "";
}

/* Writes XRGB pixels `width` wide as a PNG and prints where. */
void SaveImage(const fs::path& path, const std::vector<unsigned>& pixels, int width)
{
    if (pixels.empty()) return;
    std::error_code ignored;
    fs::create_directories(path.parent_path(), ignored);
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, int(pixels.size()) / width, SDL_PIXELFORMAT_XRGB8888,
                              const_cast<unsigned*>(pixels.data()), width * 4);
    if (surface && SDL_SavePNG(surface, path.string().c_str()))
        std::fprintf(stderr, "runtime: wrote %s\n", path.string().c_str());
    else
        std::fprintf(stderr, "runtime: could not write %s: %s\n", path.string().c_str(),
                     SDL_GetError());
    SDL_DestroySurface(surface);
}

/* XRGB pixels of a PNG with the given size; empty if it is missing or differs. */
std::vector<unsigned> LoadImage(const fs::path& path, int width, int height)
{
    std::vector<unsigned> pixels;
    SDL_Surface* image = SDL_LoadPNG(path.string().c_str());
    if (image && image->w == width && image->h == height) {
        pixels.resize(std::size_t(width) * height);
        if (!SDL_ConvertPixels(width, height, image->format, image->pixels, image->pitch,
                               SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4))
            pixels.clear();
    }
    SDL_DestroySurface(image);
    return pixels;
}

/* The harness whose frame a failed assertion shows. */
GameHarness* g_live_game = nullptr;

/* Saves the game's frame when an assertion fails while a game runs. Thrown
   failures (FailStep, REQUIRE, unexpected exceptions) unwind the harness
   before Catch reports them, so its destructor saves those. */
class FailureFrames : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        // Leave only this run's images.
        std::error_code ignored;
        for (const auto& entry : fs::directory_iterator(ArtifactDirectory(), ignored))
            if (entry.path().extension() == ".png") fs::remove(entry.path(), ignored);
    }

    void assertionEnded(const Catch::AssertionStats& stats) override
    {
        if (!stats.assertionResult.isOk() && g_live_game) g_live_game->save_failure_frames();
    }
};
} // namespace

CATCH_REGISTER_LISTENER(FailureFrames)

[[noreturn]] void FailStep(const std::string& step, const std::string& detail)
{
    throw std::runtime_error(step + ": " + detail);
}

fs::path RuntimeInput(const char* variable)
{
    const fs::path path = RuntimeVariable(variable);
    if (!fs::is_directory(path)) FailStep(variable, "not an existing directory: " + path.string());
    return path;
}

fs::path RuntimeOutput(const char* variable)
{
    return RuntimeVariable(variable);
}

GameHarness::GameHarness(bool play_movies)
{
    const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
    if (!assets || !fs::is_directory(assets))
        throw std::invalid_argument("WIZ8_ASSET_ROOT is not set");
    auto roots = w8_native::path_roots();
    roots.assets = assets;
    roots.user = user_.path.string();
    roots_.emplace(roots);
    wiz8::create_directory(wiz8::user_path("config"));
    auto config = wiz8::open_file(wiz8::user_path("config/3dvideo.cfg"), wiz8::OpenMode::replace);
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
    w8_native::audio_offline_for_test(true);
    application_.emplace(arguments);
    REQUIRE(g_renderer);
    // Games draw from a stream seeded by the clock. Catch's seed makes a run
    // repeatable: CTest fixes it, and failures print it.
    application_->games().seed(Catch::getSeed());
    window_ = reinterpret_cast<SDL_Window*>(ghWindow);
    REQUIRE(window_);
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    focus.window.windowID = SDL_GetWindowID(window_);
    HandleGameEvent(focus);
    g_live_game = this;
}

GameHarness::~GameHarness()
{
    if (std::uncaught_exceptions() > 0) save_failure_frames();
    g_live_game = nullptr;
    application_.reset();
    w8_native::audio_offline_for_test(false);
    SDL_SetLogOutputFunction(g_default_log, g_default_log_data);
    if (SDL_getenv("WIZ8_RUNTIME_KEEP_USER_ROOT")) {
        std::fprintf(stderr, "kept user root %s\n", user_.path.string().c_str());
        user_.keep();
    }
}

namespace {
/* Input and focus a desktop session can deliver to a visible test window. */
bool IsHostInput(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return true;
    default:
        // Keyboard, text, mouse, joystick, gamepad, touch and pen events.
        return (event.type >= SDL_EVENT_KEY_DOWN && event.type < SDL_EVENT_CLIPBOARD_UPDATE) ||
               (event.type >= SDL_EVENT_PEN_PROXIMITY_IN &&
                event.type < SDL_EVENT_CAMERA_DEVICE_ADDED);
    }
}
} // namespace

void GameHarness::frame()
{
    // The scenario is the only input source. Window state still reaches the
    // game, but host input and focus changes never steer or suspend a run.
    SDL_Event event{};
    while (SDL_PollEvent(&event))
        if (!IsHostInput(event)) HandleGameEvent(event);
    if (!gfProgramIsRunning) throw GameExited();
    GameLoop();
    // Run the real mixer on the same 60 Hz clock as movement and animation.
    // This advances stream positions, loops, fades and completion callbacks
    // without depending on how fast the host audio device consumes samples.
    std::array<float, 735 * 2> audio{}; // 44,100 stereo sample frames / 60.
    if (!w8_native::audio_render_for_test(audio.data(), audio.size() / 2))
        FailStep("audio", "offline mixer did not advance");
    ++frames_;
    g_runtime_test_hooks.clock_us += frame_us;
    if (frames_ % 120 == 0)
        std::fprintf(stderr, "runtime: %lu frames, screen %s, %.1fs elapsed\n", frames_,
                     ScreenName(CurrentScreenState().id),
                     std::chrono::duration<double>(Clock::now() - started_).count());
}

void GameHarness::wait(const char* step, const std::function<bool()>& ready, double seconds)
{
    const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
    while (!ready()) {
        if (Clock::now() > deadline)
            FailStep(step, "timed out after " + std::to_string(frames_) + " frames on screen " +
                               std::string(ScreenName(CurrentScreenState().id)) + " pending " +
                               ScreenName(PendingScreen()));
        frame();
    }
}

void GameHarness::move_pointer(int x, int y)
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

void GameHarness::drag(SDL_Rect from, SDL_Rect to)
{
    mouse_button(from.x + from.w / 2, from.y + from.h / 2, true);
    frame();
    // Leaving the source while held starts the gameplay drag.
    move_pointer(to.x + to.w / 2, to.y + to.h / 2);
    mouse_button(to.x + to.w / 2 - g_cursor_hotspot_x, to.y + to.h / 2 - g_cursor_hotspot_y, false);
    frame();
}

void GameHarness::send_key(SDL_Scancode scancode, SDL_Keycode code, bool down, SDL_Keymod mod)
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

void GameHarness::key(SDL_Keycode code)
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

void GameHarness::enter_text(std::string_view text)
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

void GameHarness::command_key(const MGSKeyBinding& binding, bool down)
{
    const SDL_Scancode scancode = SDL_GetScancodeFromKey(binding.key, nullptr);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        FailStep("command", "no SDL scancode for binding " + std::to_string(binding.key));
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
        if (binding.modifiers & modifier.flag) mod |= modifier.mod;
    const SDL_Keycode key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    if (down) {
        for (const auto& modifier : modifiers)
            if (binding.modifiers & modifier.flag)
                send_key(modifier.scancode, modifier.key, true, mod);
        send_key(scancode, key, true, mod);
    } else {
        send_key(scancode, key, false, mod);
        for (const auto& modifier : modifiers)
            if (binding.modifiers & modifier.flag)
                send_key(modifier.scancode, modifier.key, false, SDL_KMOD_NONE);
    }
}

void GameHarness::command(W8MGSCommand command)
{
    const int index = g_mgs_keyboard.FindBinding(command);
    const MGSKeyBinding* bound = index >= 0 ? g_mgs_keyboard.GetBinding(index) : nullptr;
    if (!bound || !bound->key)
        FailStep("command", "no key bound to command " + std::to_string(int(command)));
    const MGSKeyBinding binding = *bound;
    command_key(binding, true);
    frame();
    command_key(binding, false);
    frame();
}

std::vector<unsigned> FrameBufferPixels()
{
    const auto& surface = GetFrameBufferSurface();
    std::vector<unsigned> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    if (!SDL_ConvertPixels(SCREEN_WIDTH, SCREEN_HEIGHT, surface.format, surface.pixels,
                           surface.pitch, SDL_PIXELFORMAT_XRGB8888, pixels.data(),
                           SCREEN_WIDTH * sizeof(unsigned)))
        pixels.clear();
    return pixels;
}

std::vector<unsigned> GameHarness::presented_frame() const
{
    // Camp renders through SGP's CPU surface, without a world renderer.
    if (!g_renderer) return FrameBufferPixels();
    // Also runs while a failure unwinds the harness, so it must not throw.
    SDL_Surface* surface = nullptr;
    try {
        surface = g_renderer->captureLastFrame();
    } catch (const std::exception&) {
    }
    if (!surface) return {};
    // The target matches the window's pixel size; sample it in screen coordinates.
    std::vector<unsigned> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] =
                ReadARGB(*surface, x * surface->w / SCREEN_WIDTH, y * surface->h / SCREEN_HEIGHT) &
                0xffffff;
    return pixels;
}

std::vector<unsigned> GameHarness::read_frame()
{
    auto pixels = presented_frame();
    REQUIRE(!pixels.empty());
    return pixels;
}

void GameHarness::save_failure_frames()
{
    if (failure_frame_ == frames_) return;
    failure_frame_ = frames_;
    const fs::path directory = ArtifactDirectory();
    if (directory.empty()) return;
    const std::string stem = "failure-frame" + std::to_string(frames_);
    std::fprintf(stderr, "runtime: the game's frame at the failure:\n");
    SaveImage(directory / (stem + ".png"), presented_frame(), SCREEN_WIDTH);
    // With a renderer, the CPU frame buffer holds the 2D interface on its own.
    if (g_renderer) SaveImage(directory / (stem + "-ui.png"), FrameBufferPixels(), SCREEN_WIDTH);
}

void GameHarness::mouse_button(int x, int y, bool down, Uint8 which)
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
    button.button.button = which;
    button.button.down = down;
    button.button.clicks = 1;
    button.button.x = wx;
    button.button.y = wy;
    HandleGameEvent(button);
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

void Snapshot(const char* name, const std::vector<unsigned>& pixels)
{
    const fs::path directory = ArtifactDirectory();
    if (!directory.empty())
        SaveImage(directory / (std::string(name) + ".png"), pixels, SCREEN_WIDTH);
}

namespace {
/* A golden is the frame box-averaged over 8x8 cells: 80x60 RGB. */
constexpr int kCell = 8, kSignatureWidth = SCREEN_WIDTH / kCell,
              kSignatureHeight = SCREEN_HEIGHT / kCell;
/* A cell differs when a channel moved by more than this... */
constexpr int kCellTolerance = 4;
/* ...and a frame differs when more cells than this do. */
constexpr int kMaxDifferingCells = 4;

int Channel(unsigned pixel, int shift)
{
    return int(pixel >> shift & 255);
}

std::vector<unsigned> Signature(const std::vector<unsigned>& frame)
{
    std::vector<unsigned> signature(kSignatureWidth * kSignatureHeight);
    for (int cy = 0; cy < kSignatureHeight; ++cy)
        for (int cx = 0; cx < kSignatureWidth; ++cx) {
            unsigned cell = 0;
            for (int shift : {0, 8, 16}) {
                int sum = 0;
                for (int y = cy * kCell; y < (cy + 1) * kCell; ++y)
                    for (int x = cx * kCell; x < (cx + 1) * kCell; ++x)
                        sum += Channel(frame[y * SCREEN_WIDTH + x], shift);
                cell |= unsigned((sum + kCell * kCell / 2) / (kCell * kCell)) << shift;
            }
            signature[cy * kSignatureWidth + cx] = cell;
        }
    return signature;
}

int CellDifference(unsigned a, unsigned b)
{
    int worst = 0;
    for (int shift : {0, 8, 16})
        worst = std::max(worst, std::abs(Channel(a, shift) - Channel(b, shift)));
    return worst;
}

/* A signature at screen size, for viewing next to the frame. */
std::vector<unsigned> Enlarged(const std::vector<unsigned>& signature)
{
    std::vector<unsigned> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] = signature[y / kCell * kSignatureWidth + x / kCell];
    return pixels;
}

/* Goldens come from Mesa's software Vulkan driver, which CTest selects. */
bool RendersWithLavapipe()
{
    const char* drivers = SDL_getenv("VK_DRIVER_FILES");
    return drivers && std::string_view(drivers).contains("lvp_icd");
}
} // namespace

void ExpectScreen(GameHarness& game, const char* name)
{
    const auto frame = game.read_frame();
    Snapshot(name, frame);
    if (g_renderer && !RendersWithLavapipe()) {
        std::printf("%s: golden comparison skipped: not rendering with lavapipe\n", name);
        return;
    }
    const char* goldens = SDL_getenv("WIZ8_RUNTIME_GOLDENS");
    if (!goldens) {
        std::printf("%s: golden comparison skipped: WIZ8_RUNTIME_GOLDENS is not set\n", name);
        return;
    }
    const auto actual = Signature(frame);
    const fs::path golden = fs::path(goldens) / (std::string(name) + ".png");
    if (SDL_getenv("WIZ8_RUNTIME_UPDATE_GOLDENS")) {
        SaveImage(golden, actual, kSignatureWidth);
        return;
    }
    const auto expected = LoadImage(golden, kSignatureWidth, kSignatureHeight);
    INFO(name << ": golden " << golden.string());
    REQUIRE(!expected.empty());
    std::vector<int> differences(actual.size()), differing;
    std::vector<unsigned> diff(actual.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        differences[i] = CellDifference(actual[i], expected[i]);
        // Differing cells are red, the others grey by how far they moved.
        diff[i] = differences[i] > kCellTolerance
                      ? 0xff0000
                      : unsigned(std::min(differences[i] * 16, 255)) * 0x010101;
        if (differences[i] > kCellTolerance) differing.push_back(int(i));
    }
    std::ranges::sort(differing, std::greater{}, [&](int i) { return differences[i]; });
    std::string worst;
    for (int i : differing | std::views::take(5))
        worst += " (" + std::to_string(i % kSignatureWidth * kCell) + "," +
                 std::to_string(i / kSignatureWidth * kCell) +
                 ")=" + std::to_string(differences[i]);
    std::printf("%s: %zu of %zu cells differ by more than %d (largest difference %d)%s\n", name,
                differing.size(), actual.size(), kCellTolerance, std::ranges::max(differences),
                worst.c_str());
    if (differing.size() > kMaxDifferingCells) {
        const fs::path directory = ArtifactDirectory();
        if (!directory.empty())
            for (const auto& [suffix, image] :
                 {std::pair{"expected", &expected}, {"actual", &actual}, {"diff", &diff}})
                SaveImage(directory / (std::string(name) + "-golden-" + suffix + ".png"),
                          Enlarged(*image), SCREEN_WIDTH);
    }
    INFO("worst cells (x,y)=difference:" << worst);
    CHECK(differing.size() <= kMaxDifferingCells);
}

void ReachMainMenu(GameHarness& game)
{
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    REQUIRE(g_runtime_test_hooks.movie_frames_presented == 0);
}

glm::vec3 PartyPosition()
{
    CameraPose state{};
    state = partyPose(*GetWorld());
    return state.position;
}

float HorizontalDistance(const glm::vec3& a, const glm::vec3& b)
{
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void WaitMainGame(GameHarness& game, const char* step)
{
    game.wait_screen(step, W8_SCREEN_MAIN_GAME, 60);
    World* world = GetWorld();
    REQUIRE((world && world->compiledIndex() && world->camera && world->scene));
}

glm::vec3 Settle(GameHarness& game, const char* step)
{
    glm::vec3 last = PartyPosition();
    for (int frame = 0, still = 0; frame < 600; ++frame) {
        game.frame();
        const glm::vec3 now = PartyPosition();
        const float dy = now.y - last.y;
        still = HorizontalDistance(now, last) < 0.01f && std::abs(dy) < 0.01f ? still + 1 : 0;
        last = now;
        if (still >= 5) return now;
    }
    FailStep(step, "party never settled");
}

glm::vec3 Walk(GameHarness& game, const char* step, float distance)
{
    const glm::vec3 start = PartyPosition();
    game.hold_key(SDLK_UP, true);
    game.wait(step, [&] { return HorizontalDistance(PartyPosition(), start) > distance; }, 20);
    game.hold_key(SDLK_UP, false);
    return Settle(game, step);
}

int PremadeSlot()
{
    REQUIRE(CountActiveCharacters() == 1);
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (CurrentGame().party.members[slot].has_value()) {
            REQUIRE(CurrentGame().party.characters[slot].fInParty);
            return slot;
        }
    FailStep("party", "no occupied slot");
}

void StartPremadeGame(GameHarness& game)
{
    game.install(RuntimeInput("WIZ8_RUNTIME_FIXTURES") / "vi.CHR", "saves/characters/vi.CHR");
    ReachMainMenu(game);
    BeginNewGame(game.games());
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

int OverlayPixel(SDL_Point point)
{
    Uint8 r, g, b, a;
    REQUIRE(SDL_ReadSurfacePixel(&GetFrameBufferSurface(), point.x, point.y, &r, &g, &b, &a));
    return r | g | b;
}

std::vector<fs::path> SaveFiles(const fs::path& user)
{
    std::vector<fs::path> saves;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(user / "saves", ignored))
        if (entry.is_regular_file() && entry.path().extension() == ".SAV" &&
            entry.path().stem() != "CurrentGame")
            saves.push_back(entry.path());
    return saves;
}

fs::path QuickSave(GameHarness& game)
{
    const auto before = SaveFiles(game.user_root());
    game.command(W8_MGS_COMMAND_QUICK_SAVE);
    auto saves = SaveFiles(game.user_root());
    std::erase_if(saves, [&](const fs::path& save) { return std::ranges::contains(before, save); });
    REQUIRE((saves.size() == 1 && fs::file_size(saves[0]) > 0));
    REQUIRE(GetSaveGameLevel(saves[0].stem().string().c_str()) == int(GetLoadedLevelID()));
    return saves[0];
}

void QuickLoad(GameHarness& game)
{
    game.command(W8_MGS_COMMAND_QUICK_LOAD);
    bool loading = false;
    game.wait("quick-load", [&] {
        loading = loading || CurrentScreenState().id == W8_SCREEN_PLEASE_WAIT;
        return loading && CurrentScreenState().id == W8_SCREEN_MAIN_GAME && game.settled();
    });
    Settle(game, "quick-load");
}

void ExpectAt(const char* step, const glm::vec3& expected)
{
    const glm::vec3 now = PartyPosition();
    const float distance = HorizontalDistance(now, expected);
    std::printf("%s: at (%g %g %g), expected (%g %g %g)\n", step, now.x, now.y, now.z, expected.x,
                expected.y, expected.z);
    if (!std::isfinite(distance) || !std::isfinite(now.y) || distance > 1.0f ||
        std::abs(now.y - expected.y) > 1.0f)
        FailStep(step, "party restored " + std::to_string(distance) + " units from the save");
}

void LoadFromMenu(GameHarness& game, const fs::path& save)
{
    game.install(save, fs::path("saves") / (save.stem().string() + ".SAV"));
    ReachMainMenu(game);
    game.click("load-game", MainMenuRegion(2));
    W8OptionsSaveLoadPanel* panel = nullptr;
    game.wait("load-panel", [&] {
        if (CurrentScreenState().id != W8_SCREEN_OPTIONS || !game.settled() ||
            !FindScreen<OptionsScreen>() || FindScreen<OptionsScreen>()->m_selected_panel != 4)
            return false;
        // Panel set 4 holds the single load panel (W8OptionsSaveLoadPanel 11).
        W8OptionsPanelSet* set = FindScreen<OptionsScreen>()->m_panel[4].get();
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
}

W8MonsterInfo* EnterCombat(GameHarness& game)
{
    game.command(W8_MGS_COMMAND_TOGGLE_COMBAT);
    REQUIRE(game.within_frames(4, [] { return CurrentGame().combat.has_value() != 0; }));
    REQUIRE(CurrentGame().combat);
    W8MonsterInfo* monster = nullptr;
    for (const auto& candidate : CurrentLevelPopulation().monster_roster.live()) {
        if (candidate->fActive && candidate->entity.find() && candidate->hp_current &&
            candidate->ubDisposition == W8_DISPOSITION_HOSTILE) {
            monster = candidate.get();
            break;
        }
    }
    REQUIRE(monster);
    if (!monster->pCombat) {
        MonsterInfoEnterCombat(monster);
    }
    return monster;
}

void Approach(GameHarness& game, int location_id, float stop)
{
    W8MonsterInfo* monster = FindMonster(location_id, false);
    if (!monster || !monster->entity)
        FailStep("approach", "no monster " + std::to_string(location_id));
    SDL_Keycode held = SDLK_UNKNOWN;
    const auto hold = [&](SDL_Keycode key) {
        if (held == key) return;
        if (held != SDLK_UNKNOWN) game.hold_key(held, false);
        if (key != SDLK_UNKNOWN) game.hold_key(key, true);
        held = key;
    };
    for (int frame = 0; frame < 60 * 120 && !CurrentGame().combat.has_value(); ++frame) {
        const CameraPose camera = partyPose(*GetWorld());
        const glm::vec3 target = monster->entity->nav.GetPosition();
        if (HorizontalDistance(target, camera.position) < stop) break;
        const float wanted = std::atan2(target.x - camera.position.x, target.z - camera.position.z);
        const float turn = std::remainder(wanted - camera.orientation.yaw, 2 * glm::pi<float>());
        hold(std::abs(turn) < 0.15f ? SDLK_UP : turn > 0 ? SDLK_RIGHT : SDLK_LEFT);
        game.frame();
    }
    hold(SDLK_UNKNOWN);
    game.frame();
}

void RequireAssets()
{
    const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
    if (!assets || !fs::is_directory(assets)) SKIP("WIZ8_ASSET_ROOT is not set");
    g_logged_errors.clear();
}

void CheckGameErrors()
{
    for (const auto& message : g_logged_errors) UNSCOPED_INFO(message);
    REQUIRE(g_logged_errors.empty());
}

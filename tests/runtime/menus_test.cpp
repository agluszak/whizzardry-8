#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include "harness.h"
#include "wiz8/cursor.h"
#include "wiz8/game.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/pointer.h"
#include "wiz8/runtime_test_hooks.h"

namespace {

/* The menu's hit targets must be screen-owned, survive screen re-entry, and
   never activate from an unpaired mouse release. */
void MainMenuUiScenario()
{
    GameHarness game(false);
    ReachMainMenu(game);
    const auto verify_controls = [] {
        for (short item = 0; item < 6; ++item) REQUIRE(UiTargetAt(MainMenuRegion(item)) != nullptr);
    };
    verify_controls();
    REQUIRE(!FindScreen<MainMenuScreen>()->has_save_games);
    game.click("disabled-load", MainMenuRegion(2));
    REQUIRE(CurrentScreenState().id == W8_SCREEN_MAIN_MENU);
    REQUIRE(game.settled());
    game.release_outside(MainMenuRegion(1));
    REQUIRE(CurrentScreenState().id == W8_SCREEN_MAIN_MENU);
    REQUIRE(game.settled());
    game.click("credits", MainMenuRegion(3));
    game.wait_screen("credits", W8_SCREEN_CREDITS);
    game.click_at(320, 240); // The credits register no control; any click leaves.
    game.wait_screen("main-menu-return", W8_SCREEN_MAIN_MENU);
    verify_controls();
}

/* --- Character creation through the real controls ------------------------ */

W8CharacterStatsPage* StatsPage()
{
    return FindScreen<CharacterScreen>()
               ? static_cast<W8CharacterStatsPage*>(FindScreen<CharacterScreen>()->m_pages[0].get())
               : nullptr;
}

/* Spend a page's point pool through each entry's increment control until the
   page reports the allocation complete. */
void SpendPool(GameHarness& game, const char* step, int page_index, const bool& complete)
{
    W8CharacterPage* page = FindScreen<CharacterScreen>()->m_pages[page_index].get();
    REQUIRE((page && !page->m_entries.empty()));
    for (int index = 0; index < static_cast<int>(page->m_entries.size()) && !complete; ++index) {
        W8CharacterPageEntry* entry = page->m_entries[index].get();
        if (!entry || !entry->m_enabled || !entry->m_second || !entry->m_third ||
            (page_index == 2 && !entry->m_increment_allowed))
            continue;
        while (!complete && *entry->m_second < *entry->m_third) {
            const int before = *entry->m_second;
            game.click(step, *entry->m_increment);
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

/* Button labels must be legible text on the button art. Palette entry 0 of the
   UI fonts is a colour key; painting it, as a background or as the text colour,
   turns a label into a solid red bar with no light text pixels. */
void ExpectLabelDrawn(const char* step, const std::vector<unsigned>& frame, SDL_Rect area)
{
    int bright = 0;
    for (int y = area.y; y < area.y + area.h; ++y)
        for (int x = area.x; x < area.x + area.w; ++x) {
            const unsigned pixel = frame[y * SCREEN_WIDTH + x];
            bright +=
                (pixel >> 16 & 0xff) > 0xc0 && (pixel >> 8 & 0xff) > 0xb0 && (pixel & 0xff) > 0x90;
        }
    INFO(step << ": " << bright << " text pixels");
    CHECK(bright >= 20);
}

/* The four left-panel actions of party selection, as {0x145, 0x137 + 26 * row}. */
void ExpectPartyActionLabels(const char* step, GameHarness& game, int rows)
{
    const auto frame = game.read_frame();
    for (int row = 0; row < rows; ++row)
        ExpectLabelDrawn(step, frame, {0x145 + 4, 0x137 + 26 * row + 3, 0xb4 - 8, 16});
}

std::string CreateCharacter(GameHarness& game)
{
    ReachMainMenu(game);
    game.click("new-game", MainMenuRegion(1));
    game.wait("party-selection", [&] {
        return CurrentScreenState().id == W8_SCREEN_PARTY_SELECTION && game.settled() &&
               UiTargetAt(kCreateCharacterButton) != nullptr;
    });
    Snapshot("party-selection", game.read_frame());
    ExpectPartyActionLabels("party-selection labels", game, 2);
    game.click("create-character", kCreateCharacterButton);
    game.wait("character-screen", [&] {
        return CurrentScreenState().id == W8_SCREEN_CHARACTER && game.settled() && StatsPage() &&
               FindScreen<CharacterScreen>()->m_page_index == 0;
    });

    W8CharacterStatsPage* stats = StatsPage();
    Snapshot("character-screen", game.read_frame());
    REQUIRE((stats->m_profession_row && stats->m_race_row && stats->m_gender_row));
    game.click("profession", *stats->m_profession_row->m_increment);
    REQUIRE(game.within_frames(2, [&] { return stats->m_profession_row->m_index != -1; }));
    game.click("race", *stats->m_race_row->m_increment);
    REQUIRE(game.within_frames(2, [&] { return stats->m_race_row->m_index != -1; }));
    game.click("gender", *stats->m_gender_row->m_increment);
    REQUIRE(game.within_frames(2, [&] { return stats->m_gender_row->m_index != -1; }));
    REQUIRE((game.within_frames(
        2, [&] { return !stats->m_entries.empty() && stats->m_entries[0]->m_enabled; })));
    SpendPool(game, "attributes", 0,
              FindScreen<CharacterScreen>()->m_creation_state.attributes_complete);

    // The first profession is a non-caster, so Next skips the spell page.
    game.click("next-to-skills", *FindScreen<CharacterScreen>()->m_next);
    game.wait("skills", [&] {
        W8CharacterPage* page = FindScreen<CharacterScreen>()->m_pages[2].get();
        return FindScreen<CharacterScreen>()->m_page_index == 2 && page && !page->m_entries.empty();
    });
    SpendPool(game, "skills", 2, FindScreen<CharacterScreen>()->m_creation_state.skills_complete);

    game.click("next-to-personality", *FindScreen<CharacterScreen>()->m_next);
    game.wait("personality", [&] {
        W8CharacterPage* page = FindScreen<CharacterScreen>()->m_pages[3].get();
        return FindScreen<CharacterScreen>()->m_page_index == 3 && page && !page->m_prepared;
    });
    // The page focuses its first (full name) field; Tab moves to the short
    // name, which names the saved file.
    for (SDL_Keycode key : {SDLK_P, SDLK_R, SDLK_O, SDLK_B, SDLK_E, SDLK_TAB, SDLK_V, SDLK_I})
        game.key(key);
    const W8Character& typed = FindScreen<CharacterScreen>()->m_character;
    const std::string name = typed.name;
    REQUIRE(!name.empty());

    game.click("finish-character", *FindScreen<CharacterScreen>()->m_next);
    game.wait_screen("committed", W8_SCREEN_PARTY_SELECTION);
    const auto saved = game.user_root() / "saves" / "characters" / (name + ".CHR");
    if (!fs::is_regular_file(saved)) FailStep("saved", "missing " + saved.string());
    REQUIRE(fs::file_size(saved) > 0);
    return name;
}

/* Party selection's Confirm button, and the accept button of the party-size
   prompt it opens for a party of fewer than six. The screen and its prompt
   keep their controls private. */
constexpr SDL_Point kPartyConfirmButton{430, 447};
constexpr SDL_Point kPartySizePromptAccept{293, 306};

void FormationUiScenario()
{
    GameHarness game(false);
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
    const int original_row = CurrentGame().party.formation.rowOf(slot);
    const int destination_row = (original_row + 1) % 5;
    const auto destination = center(destination_row * 3);
    const auto begin_drag = [&] {
        const auto seat = CurrentGame().planned_formation.cellOf(slot);
        REQUIRE(seat);
        const auto source = center(seat->row * 3 + seat->column);
        game.press_at(source.x, source.y);
        REQUIRE(g_ui.captured != 0);
        for (int frame = 0; frame < 5; ++frame) game.frame();
        REQUIRE(g_ui.captured != 0); // Below the 250 ms hold threshold.
        REQUIRE(game.within_frames(60, [] { return g_ui.captured == 0; }));
        REQUIRE(ScreenPointer().shownSlot() == W8_CURSOR_INVALID_TARGET);
    };
    const auto drop_at = [&](SDL_Point point) {
        game.release_at(point.x - g_cursor_hotspot_x, point.y - g_cursor_hotspot_y);
    };

    OpenFormationPanel();
    game.frame();
    REQUIRE(FormationPanelOpen());
    begin_drag();
    drop_at(destination);
    REQUIRE(CurrentGame().planned_formation.rowOf(slot) == destination_row);
    REQUIRE(CurrentGame().party.formation.rowOf(slot) == original_row);

    // Reset preserves the live formation; dropping on the panel edge cancels.
    game.click("formation-reset", kFormationReset);
    REQUIRE(CurrentGame().planned_formation.rowOf(slot) == original_row);
    begin_drag();
    drop_at({220, 70});
    REQUIRE(CurrentGame().planned_formation.rowOf(slot) == original_row);

    begin_drag();
    drop_at(destination);
    // Accept, with teardown deferred past button dispatch.
    game.click("formation-accept", kFormationAccept);
    REQUIRE_FALSE(FormationPanelOpen());
    REQUIRE(CurrentGame().party.formation.rowOf(slot) == destination_row);
    REQUIRE(g_ui.captured == 0);

    OpenFormationPanel();
    game.frame();
    REQUIRE(FormationPanelOpen());
    REQUIRE(OverlayPixel({320, 180}));
    game.click("formation-cancel", kFormationCancel); // After re-entry.
    REQUIRE_FALSE(FormationPanelOpen());
    REQUIRE(!OverlayPixel({320, 180})); // Closing erases the panel.
    REQUIRE(CurrentGame().party.formation.rowOf(slot) == destination_row);
}

/* --- Scenarios ----------------------------------------------------------- */

void VideoScenario(bool intro_seen)
{
    GameHarness game(true);
    // The asset root can contain a returning player's Wiz8.CFG.
    // Set the scenario explicitly after startup loads that configuration.
    g_settings.intro_seen = intro_seen;
    // The startup logo movie plays on the intro screen.
    game.wait("logo", [] { return g_runtime_test_hooks.movie_frames_presented >= 1; });
    REQUIRE(CurrentScreenState().id == W8_SCREEN_INTRO);
    unsigned lit = 0;
    game.wait("logo-content", [&] {
        if (g_runtime_test_hooks.movie_frames_presented > 150)
            FailStep("logo-content", "movie frames are black");
        for (unsigned pixel : game.read_frame()) lit += (pixel & 0xf0f0f0) != 0;
        return lit > 1000;
    });
    const unsigned logo_frames = g_runtime_test_hooks.movie_frames_presented;
    game.key(SDLK_ESCAPE);
    if (!intro_seen) {
        // On a first launch, skipping the logo starts the title movie.
        game.wait("title",
                  [&] { return g_runtime_test_hooks.movie_frames_presented >= logo_frames + 5; });
        REQUIRE(CurrentScreenState().id == W8_SCREEN_INTRO);
        game.key(SDLK_ESCAPE);
    }
    game.wait_screen("main-menu", W8_SCREEN_MAIN_MENU);
    REQUIRE(g_settings.intro_seen);
    if (intro_seen) REQUIRE(g_runtime_test_hooks.movie_frames_presented == logo_frames);
    std::printf("video: %u movie frames presented\n", g_runtime_test_hooks.movie_frames_presented);
}

void GameStartScenario()
{
    GameHarness game(false);
    StartGame(game);
    CameraPose start{};
    start = partyPose(*GetWorld());
    // Hold the forward key until the camera has moved, through the real binding.
    game.hold_key(SDLK_UP, true);
    CameraPose now{};
    game.wait(
        "walk",
        [&] {
            now = partyPose(*GetWorld());
            const float dx = now.position.x - start.position.x,
                        dz = now.position.z - start.position.z;
            return std::sqrt(dx * dx + dz * dz) > 1.0f;
        },
        20);
    game.hold_key(SDLK_UP, false);
    std::printf("game-start: main game after %lu frames, walked to (%g %g %g)\n", game.frames(),
                now.position.x, now.position.y, now.position.z);
}

} // namespace

void StartGame(GameHarness& game)
{
    CreateCharacter(game);
    // Return toggles the selected roster row (the new character) into the party.
    game.key(SDLK_RETURN);
    REQUIRE(game.within_frames(4, [] { return CountActiveCharacters() == 1; }));
    game.frame();
    Snapshot("party-with-member", game.read_frame());
    ExpectPartyActionLabels("party-with-member labels", game, 4);

    /* Confirm walks the screen's own steps by mouse: a party of fewer than six
       asks for confirmation, then the options page's Confirm runs the opening. */
    game.click("confirm-party", kPartyConfirmButton);
    REQUIRE(game.within_frames(2, [] { return UiModalActive(); }));
    game.frame();
    Snapshot("party-size-prompt", game.read_frame());
    game.click("accept-party-size", kPartySizePromptAccept);
    REQUIRE(game.within_frames(2, [] { return !UiModalActive(); }));
    game.click("start-game", kPartyConfirmButton);
    game.wait_screen("main-game", W8_SCREEN_MAIN_GAME, 60);
    REQUIRE(g_runtime_test_hooks.movie_frames_presented == 0);
    World* world = GetWorld();
    REQUIRE((world && world->compiledIndex() && world->camera && world->scene));
    for (int i = 0; i < 30; ++i) game.frame();
    Snapshot("main-game", game.read_frame());
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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include "harness.h"
#include "wiz8/game.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/world/level_session.h"

namespace {

/* The UI load test consumes the save written by the quick-save regression.
   Reusing that real saved game avoids another full runtime startup. */
void WriteSaveFixture(const fs::path& output, const fs::path& save, const glm::vec3& anchor)
{
    fs::create_directories(output);
    fs::copy_file(save, output / "fixture.SAV", fs::copy_options::overwrite_existing);
    std::ofstream metadata(output / "fixture.txt");
    metadata << std::setprecision(std::numeric_limits<float>::max_digits10) << anchor.x << ' '
             << anchor.y << ' ' << anchor.z << ' ' << GetLoadedLevelID() << ' '
             << CurrentGame().party.characters[PremadeSlot()].experience << ' '
             << CurrentGame().party.characters[PremadeSlot()].hp_current << '\n';
    metadata.close();
    REQUIRE(metadata);
    std::printf("save-fixture: wrote %s (%ju bytes)\n", (output / "fixture.SAV").string().c_str(),
                std::uintmax_t(fs::file_size(output / "fixture.SAV")));
}

void QuickSaveScenario()
{
    GameHarness game(false);
    StartPremadeGame(game);
    const glm::vec3 anchor = Walk(game, "walk", 2.0f);
    const int level = GetLoadedLevelID();
    const unsigned experience = CurrentGame().party.characters[PremadeSlot()].experience;
    const auto health = CurrentGame().party.characters[PremadeSlot()].hp_current;
    REQUIRE(health > 1);
    const fs::path save = QuickSave(game);
    WriteSaveFixture(RuntimeOutput("WIZ8_RUNTIME_SAVE_FIXTURE"), save, anchor);
    Walk(game, "walk-away", 4.0f);
    REQUIRE(HorizontalDistance(PartyPosition(), anchor) > 2.0f);
    // Change a persistent field as well as the camera: loading must replace
    // the character record, not just reposition the party.
    CurrentGame().party.characters[PremadeSlot()].experience = experience + 123;
    CurrentGame().party.characters[PremadeSlot()].hp_current = health - 1;
    // Loading ends the fight while the main game is suspended.
    EnterCombat(game);
    /* With the pointer over the world view, re-entering the main game hit-tests
       the world before its selection state is reset; a fresh level block must
       not name item 0 as the selected world item. */
    game.move_pointer(320, 180);

    QuickLoad(game);
    ExpectAt("quick-load", anchor);
    REQUIRE(CountActiveCharacters() == 1);
    REQUIRE(GetLoadedLevelID() == level);
    REQUIRE(CurrentGame().party.characters[PremadeSlot()].experience == experience);
    REQUIRE(CurrentGame().party.characters[PremadeSlot()].hp_current == health);
    REQUIRE(!CurrentGame().combat.has_value());
    std::printf("quick-save: %s round-tripped after %lu frames\n", save.filename().string().c_str(),
                game.frames());
}

/* Main menu -> Load GameHarness -> the fixture save -> its party and position. */
void LoadGameScenario(const fs::path& fixture)
{
    /* Construct GameHarness first so a missing WIZ8_ASSET_ROOT skips rather than
       failing on the absent fixture when save-fixture itself skipped. */
    GameHarness game(false);
    glm::vec3 anchor{0.0f};
    int level = -1;
    unsigned experience = 0;
    unsigned health = 0;
    std::ifstream metadata(fixture / "fixture.txt");
    REQUIRE(metadata >> anchor.x >> anchor.y >> anchor.z >> level >> experience >> health);
    REQUIRE((std::isfinite(anchor.x) && std::isfinite(anchor.y) && std::isfinite(anchor.z)));
    REQUIRE(fs::is_regular_file(fixture / "fixture.SAV"));
    LoadFromMenu(game, fixture / "fixture.SAV");
    ExpectAt("loaded", anchor);
    REQUIRE(CountActiveCharacters() == 1);
    REQUIRE(GetLoadedLevelID() == level);
    REQUIRE(CurrentGame().party.characters[PremadeSlot()].experience == experience);
    REQUIRE(CurrentGame().party.characters[PremadeSlot()].hp_current == health);
    std::printf("load-game: loaded the fixture after %lu frames\n", game.frames());
}

/* The party a save holds, in slot order. */
struct SavedMember {
    std::string name;
    unsigned level, experience;
};
std::vector<SavedMember> PartyMembers()
{
    std::vector<SavedMember> members;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (CurrentGame().party.members[slot].has_value()) {
            const W8Character& character = CurrentGame().party.characters[slot];
            members.push_back({character.name, character.uiExpLevel, character.experience});
        }
    return members;
}
bool operator==(const SavedMember& a, const SavedMember& b)
{
    return a.name == b.name && a.level == b.level && a.experience == b.experience;
}

/* A save written by the retail game (fetch_downloads.cmake) loads through the
   menu with its party, level and position, and the party can walk. Our quick
   save of that world then loads back unchanged. */
void RetailSaveScenario()
{
    GameHarness game(false);
    LoadFromMenu(game, RuntimeInput("WIZ8_RUNTIME_DOWNLOADS") / "p2-RapaxAC.sav");
    REQUIRE(GetLoadedLevelID() == 16);
    ExpectAt("loaded", {29237.4f, 404.094f, -3452.46f});
    const std::vector<SavedMember> party{{"TBell", 23, 21210115}, {"Peter", 23, 21134738}};
    REQUIRE(PartyMembers() == party);
    ExpectScreen(game, "retail-save");

    const glm::vec3 anchor = Walk(game, "walk", 2.0f);
    QuickSave(game);
    Walk(game, "walk-away", 4.0f);
    REQUIRE(HorizontalDistance(PartyPosition(), anchor) > 2.0f);
    QuickLoad(game);
    ExpectAt("quick-load", anchor);
    REQUIRE(GetLoadedLevelID() == 16);
    REQUIRE(PartyMembers() == party);
    std::printf("retail-save: loaded and round-tripped after %lu frames\n", game.frames());
}

/* Menu saving exercises name edits and confirmation callbacks, which quick-save
   bypasses. Cancelled overwrites/deletes must leave the saved bytes intact. */
void MenuSaveScenario()
{
    GameHarness game(false);
    StartPremadeGame(game);
    const auto anchor = Settle(game, "menu-save-settle");
    const int slot = PremadeSlot();
    CurrentGame().party.characters[slot].experience = 123;
    const auto click = [&](const W8ButtonWidget& button) {
        const auto bounds = button.Bounds();
        game.click_at(bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    };
    const auto open = [&](bool save) -> W8OptionsSaveLoadPanel& {
        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
            game.command(W8_MGS_COMMAND_OPTIONS);
            game.wait_screen("save-options", W8_SCREEN_OPTIONS);
        }
        REQUIRE(FindScreen<OptionsScreen>());
        click(*FindScreen<OptionsScreen>()->m_menu_buttons[save ? 5 : 4]);
        REQUIRE(FindScreen<OptionsScreen>()->m_selected_panel == (save ? 5 : 4));
        return static_cast<W8OptionsSaveLoadPanel&>(
            *FindScreen<OptionsScreen>()->m_panel[save ? 5 : 4]->m_panels.front());
    };

    auto& create = open(true);
    REQUIRE(FindScreen<OptionsScreen>()->m_text_editor);
    game.enter_text("MenuOne");
    click(*create.m_action_button);
    REQUIRE((!FindScreen<OptionsScreen>() || !FindScreen<OptionsScreen>()->m_active_modal));
    WaitMainGame(game, "menu-save-created");
    auto saves = SaveFiles(game.user_root());
    REQUIRE(saves.size() == 1);
    auto save = saves.front();
    REQUIRE(save.stem() == "MenuOne");
    REQUIRE_FALSE(fs::exists(wiz8::asset_path("saves/MenuOne.SAV")));
    const auto original_bytes = wiz8::open_file(save)->read_text();
    REQUIRE_FALSE(original_bytes.empty());

    CurrentGame().party.characters[slot].experience = 456;
    auto& overwrite = open(true);
    click(overwrite.m_rows[1]->button);
    REQUIRE(overwrite.m_selected == 1);
    click(*overwrite.m_action_button);
    REQUIRE(FindScreen<OptionsScreen>()->m_active_modal);
    game.key(SDLK_ESCAPE);
    REQUIRE_FALSE(FindScreen<OptionsScreen>()->m_active_modal);
    REQUIRE(wiz8::open_file(save)->read_text() == original_bytes);
    click(*overwrite.m_action_button);
    REQUIRE(FindScreen<OptionsScreen>()->m_active_modal);
    game.key(SDLK_RETURN);
    WaitMainGame(game, "menu-save-overwritten");
    REQUIRE(SaveFiles(game.user_root()).size() == 1);
    REQUIRE(wiz8::open_file(save)->read_text() != original_bytes);

    // Renaming uses the same Save button while a name editor is active.
    // Cancelling its confirmation must restore both the row and the old file.
    auto& rename = open(true);
    click(rename.m_rows[1]->button);
    const auto edit_name = [&](std::string_view name) {
        const auto bounds = rename.m_rows[1]->button.Bounds();
        game.click_at(bounds.x + 120, bounds.y + 55);
        REQUIRE(FindScreen<OptionsScreen>()->m_text_editor);
        const auto length = FindScreen<OptionsScreen>()->m_text_editor->m_fields.Text(0).size();
        game.key(SDLK_END);
        for (std::size_t i = 0; i < length; ++i) game.key(SDLK_BACKSPACE);
        game.enter_text(name);
        click(*rename.m_action_button);
        REQUIRE(FindScreen<OptionsScreen>()->m_active_modal);
    };
    edit_name("MenuRenamed");
    game.key(SDLK_ESCAPE);
    REQUIRE(rename.m_rows[1]->m_save->name == "MenuOne");
    REQUIRE(SaveFiles(game.user_root()).size() == 1);
    REQUIRE(fs::exists(save));
    edit_name("MenuRenamed");
    game.key(SDLK_RETURN);
    WaitMainGame(game, "menu-save-renamed");
    REQUIRE_FALSE(fs::exists(save));
    saves = SaveFiles(game.user_root());
    REQUIRE(saves.size() == 1);
    save = saves.front();
    REQUIRE(save.stem() == "MenuRenamed");

    CurrentGame().party.characters[slot].experience = 999;
    auto& load = open(false);
    click(load.m_rows[0]->button);
    click(*load.m_action_button);
    WaitMainGame(game, "menu-save-loaded");
    Settle(game, "menu-save-loaded");
    REQUIRE(CurrentGame().party.characters[slot].experience == 456);
    ExpectAt("menu-save-loaded", anchor);

    auto& remove = open(false);
    click(remove.m_rows[0]->button);
    const auto overwritten_bytes = wiz8::open_file(save)->read_text();
    click(*remove.m_delete_button);
    REQUIRE(FindScreen<OptionsScreen>()->m_active_modal);
    game.key(SDLK_ESCAPE);
    REQUIRE_FALSE(FindScreen<OptionsScreen>()->m_active_modal);
    REQUIRE(wiz8::open_file(save)->read_text() == overwritten_bytes);
    // A save can change after enumeration, for example during cloud sync.
    // A failed delete must report an error and retain the displayed row.
    const auto backup = fs::path(save.string() + ".backup");
    fs::rename(save, backup);
    fs::create_directory(save);
    click(*remove.m_delete_button);
    game.key(SDLK_RETURN);
    REQUIRE(FindScreen<OptionsScreen>()->m_active_modal);
    REQUIRE(remove.m_rows[0]->button.IsEnabled());
    fs::remove(save);
    fs::rename(backup, save);
    game.key(SDLK_RETURN); // Dismiss the delete error, then retry.
    REQUIRE_FALSE(FindScreen<OptionsScreen>()->m_active_modal);
    click(*remove.m_delete_button);
    game.key(SDLK_RETURN);
    REQUIRE_FALSE(FindScreen<OptionsScreen>()->m_active_modal);
    REQUIRE(SaveFiles(game.user_root()).empty());
    REQUIRE_FALSE(remove.m_delete_button->IsEnabled());
    REQUIRE_FALSE(remove.m_action_button->IsEnabled());
    game.key(SDLK_ESCAPE);
    game.wait_screen("menu-save-return", W8_SCREEN_MAIN_GAME);
}

} // namespace

TEST_CASE("quick-save", "[runtime]")
{
    RunScenario([&] { QuickSaveScenario(); });
}

TEST_CASE("load-game", "[runtime]")
{
    RunScenario([&] { LoadGameScenario(RuntimeInput("WIZ8_RUNTIME_SAVE_FIXTURE")); });
}

TEST_CASE("menu-save", "[runtime]")
{
    RunScenario(MenuSaveScenario);
}

TEST_CASE("retail-save", "[runtime]")
{
    RunScenario(RetailSaveScenario);
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include "harness.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/pointer.h"

namespace {

/* --- Scripted exploration -------------------------------------------------
   Not a test: `native_runtime_test script` (hidden from CTest) replays the
   commands in WIZ8_RUNTIME_SCRIPT through the same driver, so a developer can
   play the real game loop headlessly. One command per line:
     newgame | premade | load SAVE | settle | frames N | click X Y | rclick X Y | move X Y
     drag X1 Y1 X2 Y2 | items | spells (reviewed character's equipment or spells)
     strings HEX_FIRST COUNT (message table) | log LINES (message box)
     monkey STEPS SEED (random clicks, keys and game commands) | journal
     key NAME | hold NAME | release NAME | text WORDS | command ID
     combat | approach MONSTER DISTANCE | trace MONSTER FRAMES | snap NAME
     state
   Snapshots go to WIZ8_RUNTIME_ARTIFACT_DIR.                                */
void PrintState(GameHarness& game)
{
    std::printf("[%lu] screen=%s pending=%s modal=%d combat=%d ui=%d", game.frames(),
                ScreenName(CurrentScreenState().id), ScreenName(PendingScreen()),
                g_modal_owner != nullptr, int(CurrentGame().combat.has_value()),
                int(g_settings.main_ui_mode));
    if (CurrentGame().combat)
        std::printf(" round=%u executing=%d", CurrentGame().combat->round_count,
                    int(CurrentGame().combat->execution_active));
    if (const auto* camp = FindScreen<CampScreen>())
        std::printf(" camp_pc=%d page=%d item_action=%d camp_dialog=%d targeting=%d",
                    camp->ReviewedPartySlot(), int(camp->page), int(camp->items.action()),
                    camp->dialogs.open() != nullptr, int(ScreenPointer().targeting()));
    std::printf("\n");
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (CurrentGame().party.members[slot].has_value()) {
            const auto& character = CurrentGame().party.characters[slot];
            std::printf("  pc %d %s hp=%u xp=%u kills=%d\n", slot, character.name.c_str(),
                        character.hp_current, character.experience, character.kill_count);
        }
    if (GetWorld()) {
        const CameraPose camera = partyPose(*GetWorld());
        const glm::vec3 at = camera.position;
        std::printf("  party at (%g %g %g) yaw %g\n", at.x, at.y, at.z, camera.orientation.yaw);
        std::vector<std::pair<float, W8MonsterInfo*>> nearby;
        for (const auto& owner : CurrentLevelPopulation().monster_roster.live()) {
            auto* monster = owner.get();
            if (monster->fActive && monster->entity.find() && monster->hp_current)
                nearby.emplace_back(HorizontalDistance(monster->entity->nav.GetPosition(), at),
                                    monster);
        }
        std::ranges::sort(nearby, {}, &std::pair<float, W8MonsterInfo*>::first);
        if (nearby.size() > 6) nearby.resize(6);
        for (const auto& [distance, monster] : nearby)
            std::printf("  monster %d %s hp=%u disposition=%d combat=%d at %.0f (%g %g)\n",
                        monster->location_id, GetMonsterName(monster, nullptr, false).c_str(),
                        monster->hp_current, int(monster->ubDisposition), bool(monster->pCombat),
                        distance, monster->entity->nav.GetPosition().x,
                        monster->entity->nav.GetPosition().z);
    }
    std::fflush(stdout);
}

/* Per-frame movement state of one monster, for diagnosing combat movement. */
void Trace(GameHarness& game, int location_id, int frames)
{
    W8MonsterInfo* monster = FindMonster(location_id, false);
    if (!monster || !monster->entity)
        FailStep("trace", "no monster " + std::to_string(location_id));
    for (int frame = 0; frame < frames; ++frame) {
        game.frame();
        const Monster& body = *monster->entity;
        const glm::vec3 at = monster->entity->nav.GetPosition();
        std::printf("  t%lu %d (%.0f %.0f) action=%d active=%d complete=%d stopped=%d halted=%d "
                    "flags=%x progress=%g/%g speed=%g status=%d actor=%d cycle=%d frame=%d/%d "
                    "end=%d\n",
                    game.frames(), location_id, at.x, at.z, int(monster->action_kind),
                    monster->pCombat ? int(monster->pCombat->active) : -1,
                    int(body.nav.movement_complete), int(body.nav.movement_stopped),
                    int(body.nav.halted), unsigned(body.nav.flags),
                    body.nav.movement.callback_progress, body.nav.movement.callback_threshold,
                    body.nav.movement.movement_speed,
                    CurrentGame().combat ? int(CurrentGame().combat->turn().index()) : -1,
                    CurrentGame().combat ? CurrentGame().combat->actingMonster().value_or(-1) : -1,
                    monster->entity->Query(W8_MONSTER_QUERY_CYCLE),
                    monster->entity->Query(W8_MONSTER_QUERY_FRAME),
                    monster->entity->Query(W8_MONSTER_QUERY_FRAME_COUNT),
                    monster->entity->Query(W8_MONSTER_QUERY_AT_PLAYBACK_END));
    }
}

void PrintItems(int slot)
{
    const W8Character& character = CurrentGame().party.characters[slot];
    const auto describe = [](const W8ItemInstance& item) {
        return item.iItemNo > 0 ? Data().items[item.iItemNo].display_name + "(" +
                                      std::to_string(item.iItemNo) + ")"
                                : std::string("-");
    };
    std::printf("  items of %d %s:", slot, character.name.c_str());
    for (const auto& item : character.EquippedItem) std::printf(" %s", describe(item).c_str());
    std::printf(" | backpack:");
    for (const auto& item : character.backpack) std::printf(" %s", describe(item).c_str());
    std::printf("\n");
}

/* Random but reproducible play: each step is a click, right click, key or bound
   main-game command, followed by a few frames. Steps are printed so a failure
   can be replayed as a script. Random input may quit the game, which ends it. */
int Monkey(GameHarness& game, int steps, unsigned seed)
{
    std::mt19937 random(seed);
    const auto pick = [&](int count) { return int(random() % unsigned(count)); };
    static constexpr SDL_Keycode keys[] = {SDLK_ESCAPE, SDLK_RETURN, SDLK_SPACE, SDLK_TAB,
                                           SDLK_UP,     SDLK_DOWN,   SDLK_LEFT,  SDLK_RIGHT,
                                           SDLK_1,      SDLK_2,      SDLK_A,     SDLK_I};
    static constexpr W8MGSCommand commands[] = {
        W8_MGS_COMMAND_INVENTORY,       W8_MGS_COMMAND_USE_ITEM,
        W8_MGS_COMMAND_CAST_SPELL,      W8_MGS_COMMAND_AUTOMAP,
        W8_MGS_COMMAND_JOURNAL,         W8_MGS_COMMAND_CAMP,
        W8_MGS_COMMAND_TOGGLE_SEARCH,   W8_MGS_COMMAND_TOGGLE_COMBAT,
        W8_MGS_COMMAND_SWAP_WEAPONS,    W8_MGS_COMMAND_SWAP_ALL_WEAPONS,
        W8_MGS_COMMAND_SELECT_PC_1,     W8_MGS_COMMAND_SELECT_PC_3,
        W8_MGS_COMMAND_SELECT_PC_6,     W8_MGS_COMMAND_START_COMBAT_ROUND,
        W8_MGS_COMMAND_ATTACK,          W8_MGS_COMMAND_DEFEND,
        W8_MGS_COMMAND_NEXT_LAYOUT,     W8_MGS_COMMAND_RADAR_ZOOM,
        W8_MGS_COMMAND_QUICK_SAVE,      W8_MGS_COMMAND_QUICK_LOAD,
        W8_MGS_COMMAND_CAST_LAST_SPELL, W8_MGS_COMMAND_USE_LAST_ITEM};
    for (int step = 0; step < steps; ++step) try {
            const int x = pick(640), y = pick(480);
            switch (pick(8)) {
            case 0:
            case 1:
            case 2:
                std::printf("  monkey %d: click %d %d\n", step, x, y);
                game.click_at(x, y);
                break;
            case 3:
                std::printf("  monkey %d: rclick %d %d\n", step, x, y);
                game.right_click_at(x, y);
                break;
            case 4:
            case 5: {
                const SDL_Keycode key = keys[pick(int(std::size(keys)))];
                std::printf("  monkey %d: key %s\n", step, SDL_GetKeyName(key));
                game.key(key);
                break;
            }
            default:
                // Bound commands only reach the game on the main-game screen.
                const W8MGSCommand command = commands[pick(int(std::size(commands)))];
                std::printf("  monkey %d: command %d\n", step, int(command));
                const int binding = g_mgs_keyboard.FindBinding(command);
                if (binding >= 0 && g_mgs_keyboard.GetBinding(binding)->key) game.command(command);
                break;
            }
            std::fflush(stdout);
            for (int frame = pick(20); frame > 0; --frame) game.frame();
        } catch (const GameExited&) {
            std::printf("  monkey %d: the game quit\n", step);
            return step;
        }
    return steps;
}

/* Runs one script line; false when the command or its arguments are unknown. */
bool RunScriptCommand(GameHarness& game, const std::string& line)
{
    std::istringstream in(line);
    std::string op, name;
    int x = 0, y = 0;
    in >> op;
    if (op == "newgame") {
        StartGame(game);
    } else if (op == "premade") {
        StartPremadeGame(game);
    } else if (op == "load" && std::getline(in >> std::ws, name)) {
        LoadFromMenu(game, name);
    } else if (op == "settle") {
        Settle(game, "settle");
    } else if (op == "frames" && in >> x) {
        for (int i = 0; i < x; ++i) game.frame();
    } else if (op == "click" && in >> x >> y) {
        game.click_at(x, y);
    } else if (op == "rclick" && in >> x >> y) {
        game.right_click_at(x, y);
    } else if (int to_x = 0, to_y = 0; op == "drag" && in >> x >> y >> to_x >> to_y) {
        game.drag({x, y, 1, 1}, {to_x, to_y, 1, 1});
    } else if (op == "strings" && in >> std::hex >> x >> std::dec >> y) {
        for (int index = x; index < x + y; ++index)
            std::printf("  string 0x%x: \"%s\"\n", index, Data().strings[index].c_str());
    } else if (op == "log" && in >> x) {
        // The newest lines of the main message box.
        const unsigned used = CurrentGame().status.text_box_lines_used[0];
        for (unsigned line = used > unsigned(x) ? used - x : 0; line < used; ++line)
            std::printf("  log: \"%s\"\n", CurrentGame().message_log[0][line].text.c_str());
    } else if (unsigned seed = 0; op == "monkey" && in >> x >> seed) {
        Monkey(game, x, seed);
    } else if (op == "journal") {
        std::printf("  journal: %zu entries, %zu facts\n", CurrentGame().story.journal.size(),
                    Data().facts.size());
        for (const W8JournalEntry& entry : CurrentGame().story.journal)
            if (unsigned(entry.fact) >= Data().facts.size())
                std::printf("  journal entry level %d fact %d alternate %d out of range\n",
                            entry.level, int(entry.fact), entry.alternate_text);
    } else if (op == "items" || op == "spells") {
        const int slot = CurrentScreenState().id == W8_SCREEN_CAMP
                             ? FindScreen<CampScreen>()->ReviewedPartySlot()
                             : CurrentGame().status.selected_character;
        if (op == "items") {
            PrintItems(slot);
        } else {
            const auto& character = CurrentGame().party.characters[slot];
            std::printf("  spells of %d %s:\n", slot, character.name.c_str());
            for (std::size_t id = 0; id < std::size(character.spell_learned); ++id) {
                if (!character.spell_learned[id]) continue;
                const auto& spell = Data().spells[id];
                std::printf("  spell %zu %s realm=%d mana=%d/%d cost=%u\n", id,
                            spell.display_name.c_str(), int(spell.realm),
                            character.iSPLeft[spell.realm], character.sp_max[spell.realm],
                            unsigned(spell.spell_point_cost));
            }
        }
    } else if (op == "move" && in >> x >> y) {
        game.move_pointer(x, y);
    } else if ((op == "key" || op == "hold" || op == "release") && in >> name) {
        const SDL_Keycode code = SDL_GetKeyFromName(name.c_str());
        if (code == SDLK_UNKNOWN) return false;
        if (op == "key") {
            game.key(code);
        } else {
            game.hold_key(code, op == "hold");
            game.frame();
        }
    } else if (op == "text" && std::getline(in >> std::ws, name)) {
        game.enter_text(name);
    } else if (op == "command" && in >> x) {
        game.command(W8MGSCommand(x));
    } else if (op == "combat") {
        EnterCombat(game);
    } else if (op == "approach" && in >> x >> y) {
        Approach(game, x, float(y));
    } else if (op == "trace" && in >> x >> y) {
        Trace(game, x, y);
    } else if (op == "snap" && in >> name) {
        Snapshot(name.c_str(), game.read_frame());
    } else if (op == "state") {
        PrintState(game);
    } else {
        return false;
    }
    return true;
}

void ScriptScenario()
{
    const char* path = SDL_getenv("WIZ8_RUNTIME_SCRIPT");
    if (!path) SKIP("WIZ8_RUNTIME_SCRIPT is not set");
    std::ifstream script(path);
    REQUIRE(script);
    GameHarness game(false);
    for (std::string line; std::getline(script, line);) {
        if (line.find_first_not_of(" \t") == std::string::npos || line[0] == '#') continue;
        std::printf("> %s\n", line.c_str());
        if (!RunScriptCommand(game, line)) FailStep(line, "unknown script command");
    }
    PrintState(game);
}

} // namespace

// A bounded input sequence catches stale screen/modal owners across transitions.
TEST_CASE("random-play", "[runtime]")
{
    RunScenario([] {
        GameHarness game(false);
        StartPremadeGame(game);
        Settle(game, "settle");
        REQUIRE(Monkey(game, 300, 42) == 300);
    });
}

TEST_CASE("script", "[.][runtime]")
{
    RunScenario(ScriptScenario);
}

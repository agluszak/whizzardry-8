#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <catch2/catch_test_macros.hpp>
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>

#include "surface_pixels.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/world/camera_pose.h"

/* Installed character and level assets through the recovered game loop. */
#include <SDL3/SDL_surface.h>

#include "temporary_directory.h"
#include "wiz8/application.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

static std::vector<unsigned int> ReadFrame()
{
    auto surface = g_renderer->readback();
    REQUIRE(surface);
    std::vector<unsigned int> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] = ReadARGB(*surface, x, y);
    return pixels;
}

static void SaveFrame(const char* path, const std::vector<unsigned int>& pixels)
{
    auto output = wiz8::open_file(wiz8::path_from_utf8(path), wiz8::OpenMode::replace);
    REQUIRE(output);
    char header[80];
    const int count =
        snprintf(header, sizeof(header), "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    output->write(header, count);
    for (auto pixel : pixels) {
        unsigned char rgb[] = {static_cast<unsigned char>(pixel >> 16),
                               static_cast<unsigned char>(pixel >> 8),
                               static_cast<unsigned char>(pixel)};
        output->write(rgb, sizeof(rgb));
    }
    output->close();
}

TEST_CASE("world graphics")
{
    const char* character_name = SDL_getenv("WIZ8_WORLD_CHARACTER");
    const char* mode = SDL_getenv("WIZ8_WORLD_MODE");
    std::unique_ptr<wiz8::Application> application;
    std::string temporary;
    bool have_user_directory = false;

    REQUIRE(
        character_name); // Host path to a character fixture to install in the temporary user root.
    temporary = make_temporary_directory("wiz8-world");
    have_user_directory = true;
    auto roots = w8_native::path_roots();
    roots.user = temporary;
    w8_native::configure_paths(roots);
    const auto character_file = std::filesystem::path(character_name);
    wiz8::create_directory(wiz8::user_path("saves/characters"));
    std::filesystem::copy_file(character_file,
                               wiz8::user_path("saves/characters") / character_file.filename());
    wiz8::create_directory(wiz8::user_path("config"));
    auto config = wiz8::open_file(wiz8::user_path("config/3dvideo.cfg"), wiz8::OpenMode::replace);
    const std::string settings = "SDLGPU\n640\n480\n16\nSDL mixer spatial\n";
    config->write(settings.data(), settings.size());
    config->close();
    const std::array<std::string_view, 1> arguments{"/WINDOW"};
    fprintf(stderr, "world: initialize native application/game\n");
    application = std::make_unique<wiz8::Application>(arguments);
    g_renderer->setCaptureEnabled(true);
    BeginNewGame(application->games());
    W8Character character{};
    REQUIRE(LoadCharacter(character_file.filename().string(), &character, -1));
    REQUIRE(AddCharacterToParty(character, -1) == 2);
    RunNewGameOpeningSequence(false, {});
    // Bypass only the opening movie; use the normal new-game loading screen.
    RequestScreen({.id = W8_SCREEN_PLEASE_WAIT, .mode = W8_PLEASE_WAIT_NEW_GAME});
    fprintf(stderr, "world: enter real new-game loader\n");
    for (int frame = 0; frame < 5; ++frame) {
        GameLoop();
        fprintf(stderr, "world: frame %d screen %d pending %d\n", frame,
                int(CurrentScreenState().id), int(PendingScreen()));
    }
    REQUIRE(CurrentScreenState().id == W8_SCREEN_MAIN_GAME);
    auto world = GetWorld();
    REQUIRE((world && world->compiledIndex() && world->camera && world->scene &&
             !world->psrMeshes.empty()));
    if (mode && strcmp(mode, "ui") == 0) {
        SetMainUiPreference(W8_MAIN_UI_MODE_PORTRAITS);
        for (int frame = 0; frame < 100; ++frame) {
            SDL_Delay(16);
            GameLoop();
        }
        int slot = 0;
        while (!CurrentGame().party.members[slot].has_value()) ++slot;
        auto& portrait = CurrentGame().monster_manager_entries[slot];
        portrait.portrait_event_active = false;
        portrait.damage_splat_active = portrait.effect_icon_active = false;
        portrait.keyboard_menu_open = false;
        portrait.portrait.pose = portrait.portrait.rendered_pose = 2;
        portrait.portrait.target_pose = 1;
        portrait.portrait.blink_closing = false;
        portrait.portrait.pose_dirty = portrait.portrait.frame_dirty = false;
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 1; ++frame) {
            portrait.portrait.pose_clock = 0;
            AnimatePartyPortraits();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.pose == 1);
        REQUIRE(portrait.portrait.rendered_pose == 1);
        // A complete blink must paint both the closed and reopened eyes.
        portrait.portrait.blink_closing = true;
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 2; ++frame) {
            portrait.portrait.pose_clock = 0;
            AnimatePartyPortraits();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.rendered_pose == 2);
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 1; ++frame) {
            portrait.portrait.pose_clock = 0;
            AnimatePartyPortraits();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.rendered_pose == 1);
        if (!CurrentGame().combat.has_value()) CurrentGame().beginCombat(0);
        GameLoop();
        REQUIRE(CurrentGame().combat.has_value());
        DrawSubMenuCharacterAction();
        const auto& framebuffer = GetFrameBufferSurface();
        const auto caption = [&] {
            std::vector<unsigned char> result;
            auto* pixels = static_cast<const unsigned char*>(framebuffer.pixels);
            for (int y = 450; y < 480; ++y)
                result.insert(result.end(), pixels + y * framebuffer.pitch + 343 * 2,
                              pixels + y * framebuffer.pitch + 528 * 2);
            return result;
        };
        const auto first = caption();
        // Deliberately stain the panel to model obsolete text from another action.
        for (int y = 450; y < 480; ++y)
            memset(static_cast<unsigned char*>(framebuffer.pixels) + y * framebuffer.pitch +
                       343 * 2,
                   0xff, 185 * 2);
        DrawSubMenuCharacterAction();
        REQUIRE(caption() == first);
        VideoRemoveToolTip();
        RenderFrame();
        const auto without_tooltip = ReadFrame();
        VideoToolTip("Swap Weapons");
        VideoPositionToolTip(300, 200);
        RenderFrame();
        const auto tooltip = ReadFrame();
        // Inspect the composed tooltip, including its opaque black background
        // and transparent area outside the panel, rather than a copied widget.
        REQUIRE_FALSE(g_compositor.layerGroup(OverlayLayer::Cursor).children.empty());
        REQUIRE((tooltip[200 * SCREEN_WIDTH + 300] & 0xffffff) != 0);
        REQUIRE((tooltip[201 * SCREEN_WIDTH + 301] & 0xffffff) == 0);
        const auto outside = 202 * SCREEN_WIDTH + 300 + g_help_box_width + 2;
        REQUIRE(tooltip[outside] == without_tooltip[outside]);
        const char* output = SDL_getenv("WIZ8_WORLD_OUTPUT");
        const std::string ui_output =
            std::string(output ? output : "/tmp/wiz8-native-world.ppm") + ".ui.ppm";
        SaveFrame(ui_output.c_str(), tooltip);
        VideoRemoveToolTip();
    }

    CameraPose state{};
    state = cameraState(*world);
    fprintf(stderr, "world: camera (%g %g %g) yaw %g pitch %g\n", state.position.x,
            state.position.y, state.position.z, state.orientation.yaw, state.orientation.pitch);
    unsigned enabled = 0;
    for (unsigned i = 0; i <= world->compiledIndex()->GetMeshCount(); ++i)
        if (world->psrMeshes[i] && world->psrMeshes[i]->visible) ++enabled;
    REQUIRE(enabled > 0);

    // Same scene and UI, with only the actual world pass switched off.
    REQUIRE((g_world_render_enabled && !g_world_blacked_out));
    RenderFrame();
    unsigned polygons = world->scene->submitted_polygons;
    auto visible = ReadFrame();
    g_world_render_enabled = 0;
    RenderFrame();
    auto control = ReadFrame();
    g_world_render_enabled = 1;
    RenderFrame();
    auto restored = ReadFrame();
    unsigned changed = 0, recovered = 0;
    for (int y = g_viewport.top; y < g_viewport.bottom; ++y)
        for (int x = g_viewport.left; x < g_viewport.right; ++x) {
            int index = y * SCREEN_WIDTH + x;
            if (visible[index] != control[index]) ++changed;
            if (restored[index] != control[index]) ++recovered;
        }
    fprintf(stderr,
            "world: %u enabled meshes, %u submitted polygons; viewport pixels %u changed, %u "
            "restored\n",
            enabled, polygons, changed, recovered);
    REQUIRE(polygons > 0);
    REQUIRE((changed > 1000 && recovered > 1000));
    const char* output = SDL_getenv("WIZ8_WORLD_OUTPUT");
    if (!output) output = "/tmp/wiz8-native-world.ppm";
    SaveFrame(output, restored);
    std::string control_path = std::string(output) + ".control.ppm";
    SaveFrame(control_path.c_str(), control);
    if (mode && strcmp(mode, "kill") == 0) {
        W8MonsterInfo* monster = nullptr;
        for (const auto& candidate : CurrentLevelPopulation().monster_roster.live()) {
            if (candidate->fActive && candidate->p3D && candidate->hp_current) {
                monster = candidate.get();
                break;
            }
        }
        REQUIRE(monster);
        fprintf(stderr, "regression: kill monster %d species %u HP %u\n", monster->location_id,
                monster->monster_species, monster->hp_current);
        if (!CurrentGame().combat.has_value()) {
            for (int frame = 0; frame < 90; ++frame) {
                SDL_Delay(16);
                GameLoop();
            }
        }
        if (!CurrentGame().combat.has_value()) REQUIRE(CurrentGame().beginCombat(0));
        if (!monster->pCombat) MonsterInfoEnterCombat(monster);
        W8TargetSource attacker{};
        attacker.iType = W8_TARGET_SOURCE_CHARACTER;
        attacker.iChar = 0;
        while (attacker.iChar < W8_PARTY_SLOT_COUNT &&
               !CurrentGame().party.members[attacker.iChar].has_value())
            ++attacker.iChar;
        REQUIRE(attacker.iChar < W8_PARTY_SLOT_COUNT);
        attacker.iMonsterID = -1;
        ApplyDamageToMonster(monster, monster->hp_current, &attacker, false, 1, 0, nullptr, false);
        fprintf(stderr, "regression: lethal damage returned\n");
        for (int frame = 0; frame < 100; ++frame) GameLoop();
    }
    if (mode && strcmp(mode, "save") == 0) {
        REQUIRE(SaveGame("native-test", nullptr));
        fprintf(stderr, "regression: save succeeded\n");
        // Load as the game does, so the main game of the saved game is discarded first.
        RequestScreen(
            {.id = W8_SCREEN_PLEASE_WAIT, .mode = W8_PLEASE_WAIT_LOAD_GAME, .name = "native-test"});
        for (int frame = 0; frame < 5; ++frame) GameLoop();
        REQUIRE(CurrentScreenState().id == W8_SCREEN_MAIN_GAME);
        fprintf(stderr, "regression: reload succeeded\n");
    }
    if (mode && strcmp(mode, "angles") == 0) {
        auto previous = restored;
        for (int angle = 0; angle < 4; ++angle) {
            // This diagnostic controls the actual render cameras. Scripted
            // tracking can reject SetOrientationImmediate, yielding identical
            // captures that never exercise the requested angles.
            partyCamera().SetOrientation(state.orientation.yaw + angle * 1.5707963f,
                                         angle < 2 ? -0.7f : 0.5f);
            const auto rotation = partyCamera().rotationMatrix();
            world->camera->transform.setRotation(rotation);
            if (GetSecondaryWorld()) GetSecondaryWorld()->camera->transform.setRotation(rotation);
            RenderFrame();
            auto pixels = ReadFrame();
            REQUIRE(pixels != previous);
            SaveFrame((std::string(output) + "." + std::to_string(angle) + ".ppm").c_str(), pixels);
            previous = std::move(pixels);
        }
    }

    application.reset();
    if (have_user_directory) std::filesystem::remove_all(temporary);
}

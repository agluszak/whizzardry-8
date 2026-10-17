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

/* Installed character and level assets through the recovered game loop. */
#include <SDL3/SDL_surface.h>

#include "image.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "surrender/srCamera.h"
#include "surrender/srGERD.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/srTexture.h"
#include "temporary_directory.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/overlay_scenes.h"
#include "wiz8/xstatus.h"

static std::vector<unsigned int> ReadFrame()
{
    auto surface = g_gerd->lockBuffer();
    REQUIRE(surface);
    std::vector<unsigned int> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] = ReadARGB(*surface, x, y);
    g_gerd->unlockBuffer();
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
    ResetForNewGame();
    W8Character character{};
    REQUIRE(LoadCharacter(character_file.filename().string(), &character, -1));
    REQUIRE(AddCharacterToParty(character, -1) == 2);
    RunNewGameOpeningSequence(false, {});
    // Bypass only the opening movie; use the normal new-game loading screen.
    g_pending_screen_state.mode = 0;
    g_pending_screen_state.id = W8_SCREEN_PLEASE_WAIT;
    fprintf(stderr, "world: enter real new-game loader\n");
    for (int frame = 0; frame < 5; ++frame) {
        GameLoop();
        fprintf(stderr, "world: frame %d screen %d pending %d\n", frame,
                int(g_current_screen_state.id), int(g_pending_screen_state.id));
    }
    REQUIRE(g_current_screen_state.id == W8_SCREEN_MAIN_GAME);
    auto world = GetWorld();
    REQUIRE((world && world->octree && world->camera && world->level && !world->psrMeshes.empty()));
    if (mode && strcmp(mode, "ui") == 0) {
        ApplyMainGameModeFlag(W8_MAIN_UI_MODE_PORTRAITS, true);
        for (int frame = 0; frame < 100; ++frame) {
            SDL_Delay(16);
            GameLoop();
        }
        int slot = 0;
        while (!g_status.buffers.XChar[slot].fOccupied) ++slot;
        auto& portrait = gXStatus.monster_manager_entries[slot];
        portrait.portrait_event_active = false;
        portrait.damage_splat_active = portrait.effect_icon_active = false;
        portrait.keyboard_menu_open = false;
        portrait.portrait.pose = portrait.portrait.rendered_pose = 2;
        portrait.portrait.target_pose = 1;
        portrait.portrait.blink_closing = false;
        portrait.portrait.pose_dirty = portrait.portrait.frame_dirty = false;
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 1; ++frame) {
            portrait.portrait.pose_clock = 0;
            UpdateCharacterEventState();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.pose == 1);
        REQUIRE(portrait.portrait.rendered_pose == 1);
        // A complete blink must paint both the closed and reopened eyes.
        portrait.portrait.blink_closing = true;
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 2; ++frame) {
            portrait.portrait.pose_clock = 0;
            UpdateCharacterEventState();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.rendered_pose == 2);
        for (int frame = 0; frame < 10 && portrait.portrait.pose != 1; ++frame) {
            portrait.portrait.pose_clock = 0;
            UpdateCharacterEventState();
            REQUIRE(portrait.portrait.rendered_pose == portrait.portrait.pose);
        }
        REQUIRE(portrait.portrait.rendered_pose == 1);
        if (!gXStatus.fCombatMode) StartCombat(0);
        GameLoop();
        REQUIRE(gXStatus.fCombatMode);
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
        VideoToolTip("Swap Weapons");
        VideoPositionToolTip(300, 200);
        RenderFrame();
        const auto tooltip = ReadFrame();
        auto* node = g_overlay_scenes.cursor->findChild("Video2DRectToPolygon");
        auto* tooltip_node = static_cast<srModelInstance*>(node);
        REQUIRE(tooltip_node);
        auto* mesh = static_cast<srMeshModel*>(tooltip_node->getModel());
        auto* texture = static_cast<srTexture*>(mesh->getTexture(0, 0));
        auto* surface = texture->getSurfacePtr();
        REQUIRE(surface);
        for (int y = 0; y < g_help_box_height; ++y)
            for (int x = 0; x < g_help_box_width; ++x)
                REQUIRE((ReadARGB(*surface, x + 1, y + 1) & 0xff000000) != 0);
        REQUIRE((ReadARGB(*surface, g_help_box_width + 2, 2) & 0xff000000) == 0);
        const char* output = SDL_getenv("WIZ8_WORLD_OUTPUT");
        const std::string ui_output =
            std::string(output ? output : "/tmp/wiz8-native-world.ppm") + ".ui.ppm";
        SaveFrame(ui_output.c_str(), tooltip);
        VideoRemoveToolTip();
    }

    W8WorldCameraState state{};
    state = cameraState(*world);
    fprintf(stderr, "world: camera (%g %g %g) yaw %g pitch %g\n", state.position.x,
            state.position.y, state.position.z, state.yaw[0], state.pitch[0]);
    unsigned enabled = 0;
    for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i)
        if (world->psrMeshes[i] && !world->psrMeshes[i]->testFlag(srNode::FLAG_DISABLE)) ++enabled;
    REQUIRE(enabled > 0);

    // Same scene and UI, with only the actual world pass switched off.
    REQUIRE((g_world_render_enabled && !g_world_blacked_out));
    RenderFrame();
    const auto with_world = g_gerd->getStatistics();
    unsigned polygons = world->level->m_submitted_polygons;
    auto visible = ReadFrame();
    g_world_render_enabled = 0;
    RenderFrame();
    const auto without_world = g_gerd->getStatistics();
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
            "world: %u enabled meshes, %u submitted polygons; draws %u/%u, "
            "triangles %u/%u; viewport pixels %u changed, %u restored\n",
            enabled, polygons, with_world.draw_calls, without_world.draw_calls,
            with_world.input_triangles, without_world.input_triangles, changed, recovered);
    REQUIRE((polygons > 0 && with_world.draw_calls > without_world.draw_calls));
    REQUIRE(with_world.input_triangles > without_world.input_triangles);
    REQUIRE((changed > 1000 && recovered > 1000));
    const char* output = SDL_getenv("WIZ8_WORLD_OUTPUT");
    if (!output) output = "/tmp/wiz8-native-world.ppm";
    SaveFrame(output, restored);
    std::string control_path = std::string(output) + ".control.ppm";
    SaveFrame(control_path.c_str(), control);
    if (mode && strcmp(mode, "kill") == 0) {
        W8MonsterInfo* monster = nullptr;
        for (const auto& candidate : gXStatus.monster_roster.live()) {
            if (candidate->fActive && candidate->p3D && candidate->hp_current) {
                monster = candidate.get();
                break;
            }
        }
        REQUIRE(monster);
        fprintf(stderr, "regression: kill monster %d species %u HP %u\n", monster->location_id,
                monster->monster_species, monster->hp_current);
        if (!gXStatus.fCombatMode) {
            for (int frame = 0; frame < 90; ++frame) {
                SDL_Delay(16);
                GameLoop();
            }
        }
        if (!gXStatus.fCombatMode) REQUIRE(StartCombat(0));
        if (!monster->fInCombat) MonsterInfoEnterCombat(monster);
        W8TargetSource attacker{};
        attacker.iType = W8_TARGET_SOURCE_CHARACTER;
        attacker.iChar = 0;
        while (attacker.iChar < W8_PARTY_SLOT_COUNT &&
               !g_status.buffers.XChar[attacker.iChar].fOccupied)
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
        REQUIRE(LoadGame("native-test"));
        fprintf(stderr, "regression: reload succeeded\n");
    }
    if (mode && strcmp(mode, "angles") == 0) {
        auto previous = restored;
        for (int angle = 0; angle < 4; ++angle) {
            // This diagnostic controls the actual render cameras. Scripted
            // tracking can reject SetOrientationImmediate, yielding identical
            // captures that never exercise the requested angles.
            PartyCamera().SetOrientation(state.yaw[0] + angle * 1.5707963f,
                                        angle < 2 ? -0.7f : 0.5f);
            const auto rotation = PartyCamera().rotationMatrix();
            world->camera->setRotation(rotation);
            if (GetSecondaryWorld()) GetSecondaryWorld()->camera->setRotation(rotation);
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

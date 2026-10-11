#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>
#include <wiz8/asset_paths.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

/* Installed character and level assets through the recovered game loop. */
#include "wiz8/application.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/xstatus.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/layouts/character.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Level.h"
#include "surrender/srGERD.h"
#include "surrender/srModelInstance.h"
#include "temporary_directory.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"

static void Check(bool condition, const char* expression)
{
    if (!condition)
        throw std::runtime_error(expression);
}

static std::vector<unsigned int> ReadFrame()
{
    auto surface = g_gerd->lockBuffer();
    REQUIRE(surface);
    std::vector<unsigned int> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] = surface->getPixel(x, y);
    g_gerd->unlockBuffer();
    return pixels;
}

static void SaveFrame(const char* path, const std::vector<unsigned int>& pixels)
{
    auto output = wiz8::open_host_file(wiz8::path_from_utf8(path), wiz8::OpenMode::replace);
    REQUIRE(output);
    char header[80];
    const int count = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    output->write(header, count);
    for (auto pixel : pixels)
    {
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
    bool have_overlay = false;

        REQUIRE(character_name); // Character basename under installed Saves/Characters.
        temporary = make_temporary_directory("wiz8-world");
        have_overlay = true;
        auto roots = w8_native::path_roots();
        roots.user = temporary;
        w8_native::configure_paths(roots);
        auto config = wiz8::open_file("C:\\3DVideo.CFG", wiz8::OpenMode::replace);
        const std::string settings = "SDLGPU\n640\n480\n16\nSDL mixer spatial\n";
        config->write(settings.data(), settings.size());
        config->close();
        const std::array<std::string_view, 1> arguments{"/WINDOW"};
        fprintf(stderr, "world: initialize native application/game\n");
        application = std::make_unique<wiz8::Application>(arguments);
        ResetForNewGame();
        W8Character character{};
        REQUIRE(LoadCharacter(character_name, &character, -1, false));
        REQUIRE(AddCharacterToParty(&character, -1) == 2);
        RunNewGameOpeningSequence(false, {});
        // Bypass only the opening movie; use the normal new-game loading screen.
        g_pending_screen_state.mode = 0;
        SetPendingScreenState(W8_SCREEN_PLEASE_WAIT);
        fprintf(stderr, "world: enter real new-game loader\n");
        for (int frame = 0; frame < 5; ++frame)
        {
            GameLoop();
            fprintf(stderr, "world: frame %d screen %d pending %d\n", frame,
                    int(g_current_screen_state.id), int(g_pending_screen_state.id));
        }
        REQUIRE(g_current_screen_state.id == W8_SCREEN_MAIN_GAME);
        auto world = GetWorld();
        REQUIRE((world && world->octree && world->camera && world->level && !world->psrMeshes.empty()));
        W8WorldCameraState state{};
        GetWorldCameraState(world, &state);
        fprintf(stderr, "world: camera (%g %g %g) yaw %g pitch %g\n", state.position.x,
                state.position.y, state.position.z, state.yaw[0], state.pitch[0]);
        unsigned enabled = 0;
        for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i)
            if (world->psrMeshes[i] && !world->psrMeshes[i]->testFlag(srNode::FLAG_DISABLE))
                ++enabled;
        REQUIRE(enabled > 0);

        // Same scene and UI, with only the actual world pass switched off.
        REQUIRE((g_world_render_enabled && !g_world_blacked_out));
        RenderFrame();
        srGERD::Statistics with_world{};
        g_gerd->getStatistics(with_world);
        unsigned polygons = world->level->m_submitted_polygons;
        auto visible = ReadFrame();
        g_world_render_enabled = 0;
        RenderFrame();
        srGERD::Statistics without_world{};
        g_gerd->getStatistics(without_world);
        auto control = ReadFrame();
        g_world_render_enabled = 1;
        RenderFrame();
        auto restored = ReadFrame();
        unsigned changed = 0, recovered = 0;
        for (int y = g_viewport.top; y < g_viewport.bottom; ++y)
            for (int x = g_viewport.left; x < g_viewport.right; ++x)
            {
                int index = y * SCREEN_WIDTH + x;
                if (visible[index] != control[index])
                    ++changed;
                if (restored[index] != control[index])
                    ++recovered;
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
        if (mode && strcmp(mode, "kill") == 0)
        {
            W8MonsterInfo* monster = GetNextMonsterInfo(true);
            while (monster && (!monster->fActive || !monster->p3D || !monster->hp_current))
                monster = GetNextMonsterInfo(false);
            REQUIRE(monster);
            fprintf(stderr, "regression: kill monster %d species %u HP %u\n",
                    monster->location_id, monster->monster_species, monster->hp_current);
            if (!gXStatus.fCombatMode)
            {
                for (int frame = 0; frame < 90; ++frame)
                {
                    SDL_Delay(16);
                    GameLoop();
                }
            }
            if (!gXStatus.fCombatMode)
                REQUIRE(StartCombat(0));
            if (!monster->fInCombat)
                MonsterInfoEnterCombat(monster);
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
        if (mode && strcmp(mode, "save") == 0)
        {
            REQUIRE(SaveGame("native-test", nullptr));
            fprintf(stderr, "regression: save succeeded\n");
            REQUIRE(LoadGame("native-test"));
            fprintf(stderr, "regression: reload succeeded\n");
        }
        if (mode && strcmp(mode, "angles") == 0)
        {
            for (int angle = 0; angle < 4; ++angle)
            {
                g_gd_camera->SetOrientationImmediate(angle < 2 ? -0.7f : 0.5f,
                                                      state.yaw[0] + angle * 1.5707963f);
                GameLoop();
                RenderFrame();
                auto pixels = ReadFrame();
                SaveFrame((std::string(output) + "." + std::to_string(angle) + ".ppm").c_str(), pixels);
            }
        }

    application.reset();
    if (have_overlay)
        std::filesystem::remove_all(temporary);
}

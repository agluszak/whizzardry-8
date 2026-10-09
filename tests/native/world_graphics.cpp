/* Installed character and level assets through the recovered game loop. */
#include "sgp.h"
#include "platform_paths.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/layouts/character.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Level.h"
#include "surrender/srCamera.h"
#include "surrender/srGERD.h"
#include "surrender/srModelInstance.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <unistd.h>

static void Check(bool condition, const char* expression)
{
    if (!condition)
        throw std::runtime_error(expression);
}
#define CHECK(expression) Check(bool(expression), #expression)

static std::vector<unsigned int> ReadFrame()
{
    auto surface = g_gerd->lockBuffer();
    CHECK(surface);
    std::vector<unsigned int> pixels(SCREEN_WIDTH * SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y * SCREEN_WIDTH + x] = surface->getPixel(x, y);
    g_gerd->unlockBuffer();
    return pixels;
}

static void SaveFrame(const char* path, const std::vector<unsigned int>& pixels)
{
    FILE* output = fopen(path, "wb");
    CHECK(output);
    fprintf(output, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    bool written = true;
    for (auto pixel : pixels)
    {
        unsigned char rgb[] = {static_cast<unsigned char>(pixel >> 16),
                               static_cast<unsigned char>(pixel >> 8),
                               static_cast<unsigned char>(pixel)};
        if (fwrite(rgb, 1, sizeof(rgb), output) != sizeof(rgb))
            written = false;
    }
    CHECK(fclose(output) == 0 && written);
}

int main(int argc, char** argv)
{
    char temporary[] = "/tmp/wiz8-world-XXXXXX";
    bool have_overlay = false;
    int result = 1;
    try
    {
        CHECK(argc >= 2); // Character basename under installed Saves/Characters.
        CHECK(mkdtemp(temporary));
        have_overlay = true;
        auto roots = w8_native::path_roots();
        roots.user = temporary;
        w8_native::configure_paths(roots);
        FILE* config = fopen("C:\\3DVideo.CFG", "w");
        CHECK(config);
        fputs("SDLGPU\n640\n480\n16\nminiaudio spatial\n", config);
        CHECK(fclose(config) == 0);
        CHECK(SDL_Init(SDL_INIT_VIDEO));
        char command[] = "/WINDOW";
        ProcessCommandLine(command);
        fprintf(stderr, "world: initialize real SGP/game\n");
        CHECK(InitializeStandardGamingPlatform(nullptr, 9));
        gfApplicationActive = TRUE;
        gfProgramIsRunning = TRUE;
        ResetForNewGame();
        W8Character character{};
        CHECK(LoadCharacter(argv[1], &character, -1, false));
        CHECK(AddCharacterToParty(&character, -1) == 2);
        RunNewGameOpeningSequence(false, nullptr);
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
        CHECK(g_current_screen_state.id == W8_SCREEN_MAIN_GAME);
        auto world = GetWorld();
        CHECK(world && world->octree && world->camera && world->level && world->psrMeshes);
        W8WorldCameraState state{};
        GetWorldCameraState(world, &state);
        fprintf(stderr, "world: camera (%g %g %g) yaw %g pitch %g\n", state.position.x,
                state.position.y, state.position.z, state.yaw[0], state.pitch[0]);
        unsigned enabled = 0;
        for (unsigned i = 0; i <= world->octree->GetMeshCount(); ++i)
            if (world->psrMeshes[i] && !world->psrMeshes[i]->testFlag(srNode::FLAG_DISABLE))
                ++enabled;
        CHECK(enabled > 0);

        // Same scene and UI, with only the actual world pass switched off.
        CHECK(g_world_render_enabled && !g_world_blacked_out);
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
        CHECK(polygons > 0 && with_world.draw_calls > without_world.draw_calls);
        CHECK(with_world.input_triangles > without_world.input_triangles);
        CHECK(changed > 1000 && recovered > 1000);
        const char* output = argc > 2 ? argv[2] : "/tmp/wiz8-native-world.ppm";
        SaveFrame(output, restored);
        std::string control_path = std::string(output) + ".control.ppm";
        SaveFrame(control_path.c_str(), control);
        result = 0;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "world: %s\n", failure.what());
    }
    SGPExit();
    SDL_Quit();
    if (have_overlay)
        std::filesystem::remove_all(temporary);
    return result;
}

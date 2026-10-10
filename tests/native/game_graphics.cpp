#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>
/* Real SLF/STI -> recovered SGP surfaces -> recovered stSurface2D -> SDL GPU.
   Also checks a movie-owned surface and restoration of the game primary. */

#include "compat/video.h"
#include "native/input_events.h"
#include <wiz8/asset_paths.h>
#include "wiz8/application.h"
#include "input.h"
#include "surrender/srGERD.h"
#include "surrender/srImageIO.h"
#include "surrender/srTriMeshPipeline.h"
#include "wiz8/bink_video.h"
#include "../../src/native/movie.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/sr_api.h"
#include "wiz8/surface2d.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include "temporary_directory.h"
#include <vector>
void PresentMenuOverlayFrame();
void SaveJpegScreenshot();
extern unsigned char g_fullscreen;
extern srScene* g_cursor_scene;
unsigned char InitializeMouseCursorScene();
void PositionMouseCursor(int, int, bool);
BOOLEAN SetMouseCursorFromVideoObject(UINT32, UINT16, INT16, INT16);
struct FixtureRoot
{
    std::filesystem::path path;
    ~FixtureRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
TEST_CASE("game graphics")
{
        const auto temporary = make_temporary_directory("wiz8-graphics");
        FixtureRoot fixture{temporary};
        auto roots = w8_native::path_roots();
        roots.user = temporary;
        w8_native::configure_paths(roots);
        std::filesystem::copy_file(WIZ8_MOVIE_FIXTURE, fixture.path / "movie-fixture.mkv");
        const char* movie_path = "C:\\movie-fixture.mkv";
        auto config = wiz8::open_file("C:\\3DVideo.CFG", wiz8::OpenMode::replace);
        const std::string settings = "SDLGPU\n640\n480\n16\nnone\n";
        config->write(settings.data(), settings.size());
        config->close();
        /* This standalone test owns SDL and DBus until process exit. */
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));

        wiz8::mount_slf("Data\\Data.slf");
        REQUIRE(InitializeInputManager());
        g_fullscreen = 0;
        Initialize16BitPixelFormatMasks();
        REQUIRE(CreateWizardryWindow());
        REQUIRE(InitializePrimaryCpuSurface());
        REQUIRE(InitializeVideoDevice());
        REQUIRE(InitializeRendererSceneObjects());
        auto targa = srImage::load("Data\\AUTOMAP\\MAP_MONSTERFRIENDLY_A.TGA");
        REQUIRE((targa && targa->getWidth() > 0 && targa->getHeight() > 0));
        REQUIRE(!srTriMeshPipeline::Get(nullptr));
        auto pipeline = srTriMeshPipeline::Get(g_gerd);
        REQUIRE(pipeline);
        pipeline->Flush();
        REQUIRE(InitializeVideoSurfaceManager());
        REQUIRE(InitializeVideoObjectManager());
        VOBJECT_DESC image{};
        image.fCreateFlags = VOBJECT_CREATE_FROMFILE;
        const char* image_path = SDL_getenv("WIZ8_GRAPHICS_IMAGE");
        image.ImageFile = image_path ? image_path : "Data\\MAIN INTERFACE\\BOTTOM.STI";
        UINT32 image_id;
        REQUIRE(AddVideoObject(&image, &image_id));
        REQUIRE(BltVideoObjectFromIndex(FRAME_BUFFER, image_id, 0, 0, 0, VO_BLT_SRCTRANSPARENCY,
                                      nullptr));
        UINT32 pitch;
        auto pixels = static_cast<UINT16*>(LockPrimarySurface(&pitch));
        std::vector<UINT16> expected(640 * 480);
        for (int y = 0; y < 480; ++y)
            memcpy(expected.data() + y * 640, reinterpret_cast<BYTE*>(pixels) + y * pitch, 1280);
        UnlockPrimarySurface();
        REQUIRE(InitializeMouseCursorScene());
        image.ImageFile = "Data\\CURSORS\\2D-CURSORS.STI";
        UINT32 cursor_id;
        REQUIRE(AddVideoObject(&image, &cursor_id));
        REQUIRE(SetMouseCursorFromVideoObject(cursor_id, 0, 0, 0));
        PositionMouseCursor(480, 240, true);
        unsigned colored = 0, matched = 0, cursor_pixels = 0;
        for (int frame = 0; frame < 3; ++frame)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                HandleInputEvent(event);
            auto error = g_gerd->beginFrame();
            if (error != srGERD::ERROR_NONE)
                fprintf(stderr, "begin frame %d: %d %s window %llu open %d\n", frame, int(error),
                        g_gerd->getErrorString(error),
                        (unsigned long long)g_gerd->getWindowHandle(), g_gerd->isWindowOpen());
            REQUIRE(error == srGERD::ERROR_NONE);
            g_gerd->setClearColor(0, 0, 0, 1);
            g_gerd->clear(srFlags<srGERD::e_buffer>(srGERD::BUFFER_COLOR | srGERD::BUFFER_DEPTH));
            g_gerd->matrixMode(srGERD::MATRIX_MODELVIEW);
            g_gerd->loadIdentity();
            g_surface_node->DrawTiles(g_gerd);
            RenderScene(g_cursor_scene, g_overlay_camera, nullptr, false);
            g_gerd->endFrame();
        }
        auto buffer = g_gerd->lockBuffer();
        REQUIRE(buffer);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
            {
                unsigned value = buffer->getPixel(x, y), original = expected[y * 640 + x];
                if (x >= 470 && x < 540 && y >= 230 && y < 300 && (value & 0xffffff))
                    ++cursor_pixels;
                if (original & 0x7fff)
                {
                    ++colored;
                    int r = ((original >> 10) & 31) * 255 / 31,
                        g = ((original >> 5) & 31) * 255 / 31, b = (original & 31) * 255 / 31;
                    if (abs(int((value >> 16) & 255) - r) <= 8 &&
                        abs(int((value >> 8) & 255) - g) <= 8 && abs(int(value & 255) - b) <= 8)
                        ++matched;
                }
            }
        if (const char* output_path = SDL_getenv("WIZ8_GRAPHICS_OUTPUT"))
        {
            auto output = wiz8::open_host_file(wiz8::path_from_utf8(output_path),
                                              wiz8::OpenMode::replace);
            REQUIRE(output);
            output->write("P6\n640 480\n255\n", 15);
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x)
                {
                    unsigned v = buffer->getPixel(x, y);
                    unsigned char rgb[] = {BYTE(v >> 16), BYTE(v >> 8), BYTE(v)};
                    output->write(rgb, 3);
                }
            output->close();
        }
        g_gerd->unlockBuffer();
        printf("retail STI: %u colored pixels, %u match CPU RGB555 within 8 levels\n", colored,
               matched);
        REQUIRE((colored > 100 && matched == colored));
        printf("retail cursor: %u GPU pixels\n", cursor_pixels);
        REQUIRE(cursor_pixels > 5);

        SDL_Window* persistent_window = ghWindow;
        {
            std::vector<uint16_t> movie_pixels;
            {
                W8NativeVideo decoded;
                decoded.open(movie_path);
                REQUIRE(decoded.update(0) == W8NativeVideo::FrameReady);
                REQUIRE((decoded.frame().width == 32 && decoded.frame().height == 24));
                movie_pixels = decoded.frame().pixels;
            }
            W8BinkVideo movie;
            movie.SetTarget(BeginVideoPresentation());
            REQUIRE(movie.Open(movie_path, 0));
            REQUIRE(!movie.UpdateFrame());
            // The actual presentation surface reaches GPU output; the game primary stays intact.
            RenderFrame();
            buffer = g_gerd->lockBuffer();
            REQUIRE(buffer);
            unsigned movie_matches = 0;
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x)
                {
                    UINT16 original = x < 32 && y < 24 ? movie_pixels[y * 32 + x] : 0;
                    unsigned actual = buffer->getPixel(x, y);
                    int r = ((original >> 10) & 31) * 255 / 31,
                        g = ((original >> 5) & 31) * 255 / 31, b = (original & 31) * 255 / 31;
                    if (abs(int((actual >> 16) & 255) - r) > 8 ||
                        abs(int((actual >> 8) & 255) - g) > 8 || abs(int(actual & 255) - b) > 8)
                        fprintf(stderr, "movie mismatch %d,%d: GPU %08x CPU %04x\n", x, y, actual,
                                original);
                    REQUIRE((abs(int((actual >> 16) & 255) - r) <= 8 &&
                          abs(int((actual >> 8) & 255) - g) <= 8 &&
                          abs(int(actual & 255) - b) <= 8));
                    ++movie_matches;
                }
            g_gerd->unlockBuffer();
            printf("movie GPU: %u RGB555 pixels and black background match\n", movie_matches);
            REQUIRE((FinishVideoPresentation() && ghWindow == persistent_window));
        }
        pixels = static_cast<UINT16*>(LockPrimarySurface(&pitch));
        REQUIRE(pixels);
        for (int y = 0; y < 480; ++y)
            REQUIRE(memcmp(reinterpret_cast<BYTE*>(pixels) + y * pitch, expected.data() + y * 640,
                         1280) == 0);
        UnlockPrimarySurface();
        PresentMenuOverlayFrame();

        g_screenshot_index = 0;
        SaveJpegScreenshot();
        auto screenshot = srImage::load("Wiz800000.JPG");
        REQUIRE((screenshot && screenshot->getWidth() == 640 && screenshot->getHeight() == 480));
        puts("retail Targa import and game JPEG screenshot round-trip");

        ShutdownVideoObjectManager();
        ShutdownVideoSurfaceManager();
        ShutdownVideoScenes();
        SDL_Window* window = ghWindow;
        srExit();
        ReleasePrimaryCpuSurface();
        W8DestroyGameWindow(window);

        wiz8::clear_asset_archives();

        SDL_Quit();


}

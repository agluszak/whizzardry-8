#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>

#include "surface_pixels.h"
/* Real SLF/STI -> immutable sprites and SDL surfaces -> UiCanvasTexture -> VSG.
   Also checks a movie-owned surface and restoration of the game primary. */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <wiz8/asset_paths.h>

#include "../../src/native/movie.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/bink_video.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/cursor.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/image_io.h"
#include "wiz8/render/renderer.h"
#include "wiz8/surface_storage.h"

class WorldScene;

void PresentMenuOverlayFrame();
void SaveJpegScreenshot();
extern unsigned char g_fullscreen;
bool InitializeMouseCursorScene();
void PositionMouseCursor(int, int, bool);
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
struct FixtureRoot {
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
    const char* movie_path = "movie-fixture.mkv";
    wiz8::create_directory(wiz8::user_path("config"));
    auto config = wiz8::open_file(wiz8::user_path("config/3dvideo.cfg"), wiz8::OpenMode::replace);
    const std::string settings = "SDLGPU\n640\n480\n16\nnone\n";
    config->write(settings.data(), settings.size());
    config->close();
    /* This standalone test owns SDL and DBus until process exit. */
    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));

    GetInputState() = {};
    g_fullscreen = 0;
    REQUIRE(CreateWizardryWindow());
    REQUIRE(InitializePrimarySurface());
    REQUIRE(InitializeVideoDevice());
    g_renderer->setCaptureEnabled(true);
    REQUIRE(g_renderer->isOpen());
    REQUIRE(InitializeRendererSceneObjects());
    auto targa_file = wiz8::open_file(wiz8::asset_path("data\\automap\\map_monsterfriendly_a.tga"));
    auto targa = imageIO::loadTga(*targa_file);
    REQUIRE((targa && targa->w > 0 && targa->h > 0));
    const char* image_path = SDL_getenv("WIZ8_GRAPHICS_IMAGE");
    auto image_object =
        g_image_assets.sprites(image_path ? image_path : "data\\main interface\\bottom.sti");
    REQUIRE(image_object);
    DrawSprite(GetFrameBufferSurface(), *image_object, 0, 0, 0);
    const auto& framebuffer = GetFrameBufferSurface();
    const auto pitch = framebuffer.pitch;
    const auto* pixels = static_cast<const std::uint8_t*>(framebuffer.pixels);
    std::vector<std::uint16_t> expected(640 * 480);
    for (int y = 0; y < 480; ++y) memcpy(expected.data() + y * 640, pixels + y * pitch, 1280);
    REQUIRE(InitializeMouseCursorScene());
    auto cursor_object = g_image_assets.sprites("data\\cursors\\2d-cursors.sti");
    REQUIRE(cursor_object);
    REQUIRE(SetMouseCursorFromVideoObject(*cursor_object, 0, 0, 0));
    PositionMouseCursor(480, 240, true);
    unsigned colored = 0, matched = 0, cursor_pixels = 0;
    for (int frame = 0; frame < 3; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) HandleInputEvent(event);
        g_renderer->beginFrame({0, 0, 0, 1});
        g_ui_canvas->draw(g_renderer.get());
        g_compositor.draw(*g_renderer, OverlayLayer::Cursor);
        g_renderer->endFrame();
    }
    auto buffer = g_renderer->readback();
    REQUIRE(buffer);
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x) {
            unsigned value = ReadARGB(*buffer, x, y), original = expected[y * 640 + x];
            if (x >= 470 && x < 540 && y >= 230 && y < 300 && (value & 0xffffff)) ++cursor_pixels;
            if (original & 0x7fff) {
                ++colored;
                int r = ((original >> 10) & 31) * 255 / 31, g = ((original >> 5) & 31) * 255 / 31,
                    b = (original & 31) * 255 / 31;
                if (abs(int((value >> 16) & 255) - r) <= 8 &&
                    abs(int((value >> 8) & 255) - g) <= 8 && abs(int(value & 255) - b) <= 8)
                    ++matched;
            }
        }
    if (const char* output_path = SDL_getenv("WIZ8_GRAPHICS_OUTPUT")) {
        auto output = wiz8::open_file(wiz8::path_from_utf8(output_path), wiz8::OpenMode::replace);
        REQUIRE(output);
        output->write("P6\n640 480\n255\n", 15);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                unsigned v = ReadARGB(*buffer, x, y);
                unsigned char rgb[] = {std::uint8_t(v >> 16), std::uint8_t(v >> 8),
                                       std::uint8_t(v)};
                output->write(rgb, 3);
            }
        output->close();
    }
    printf("retail STI: %u colored pixels, %u match CPU RGB555 within 8 levels\n", colored,
           matched);
    REQUIRE((colored > 100 && matched == colored));
    printf("retail cursor: %u GPU pixels\n", cursor_pixels);
    REQUIRE(cursor_pixels > 5);

    SDL_Window* persistent_window = ghWindow;
    {
        auto movie_roots = roots;
        movie_roots.assets = (fixture.path / "movie-assets").string();
        movie_roots.user = (fixture.path / "movie-user").string();
        std::filesystem::create_directories(movie_roots.assets);
        std::filesystem::copy_file(fixture.path / "movie-fixture.mkv",
                                   std::filesystem::path(movie_roots.assets) / "movie-fixture.mkv");
        ScopedAssetRoots movie_assets{movie_roots};
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
        buffer = g_renderer->readback();
        REQUIRE(buffer);
        unsigned movie_matches = 0;
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                std::uint16_t original = x < 32 && y < 24 ? movie_pixels[y * 32 + x] : 0;
                unsigned actual = ReadARGB(*buffer, x, y);
                int r = ((original >> 10) & 31) * 255 / 31, g = ((original >> 5) & 31) * 255 / 31,
                    b = (original & 31) * 255 / 31;
                if (abs(int((actual >> 16) & 255) - r) > 8 ||
                    abs(int((actual >> 8) & 255) - g) > 8 || abs(int(actual & 255) - b) > 8)
                    fprintf(stderr, "movie mismatch %d,%d: GPU %08x CPU %04x\n", x, y, actual,
                            original);
                REQUIRE((abs(int((actual >> 16) & 255) - r) <= 8 &&
                         abs(int((actual >> 8) & 255) - g) <= 8 &&
                         abs(int(actual & 255) - b) <= 8));
                ++movie_matches;
            }
        printf("movie GPU: %u RGB555 pixels and black background match\n", movie_matches);
        REQUIRE((FinishVideoPresentation() && ghWindow == persistent_window));
    }
    const auto& restored_framebuffer = GetFrameBufferSurface();
    const auto* restored_pixels = static_cast<const std::uint8_t*>(restored_framebuffer.pixels);
    REQUIRE(restored_pixels);
    for (int y = 0; y < 480; ++y)
        REQUIRE(memcmp(restored_pixels + y * restored_framebuffer.pitch, expected.data() + y * 640,
                       1280) == 0);
    PresentMenuOverlayFrame();

    // Reopening presentation retains the canvas and the SDL platform window.
    g_renderer->close();
    REQUIRE(OpenRendererWindow());
    REQUIRE(g_renderer->isOpen());
    PresentMenuOverlayFrame();

    g_screenshot_index = 0;
    SaveJpegScreenshot();
    auto screenshot_file = wiz8::open_file(wiz8::user_path("Wiz800000.JPG"));
    SDLSurfaceOwner screenshot(IMG_LoadJPG_IO(screenshot_file->io()), SDL_DestroySurface);
    REQUIRE((screenshot && screenshot->w == 640 && screenshot->h == 480));
    puts("retail Targa import and game JPEG screenshot round-trip");

    ShutdownVideoManager();
    REQUIRE(g_renderer == nullptr);

    SDL_Quit();
}

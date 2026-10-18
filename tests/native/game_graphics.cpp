#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>

#include "surface_pixels.h"
/* Real SLF/STI -> immutable sprites and SDL surfaces -> stSurface2D -> SDL GPU.
   Also checks a movie-owned surface and restoration of the game primary. */

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <wiz8/asset_paths.h>

#include "../../src/native/movie.h"
#include "scoped_asset_roots.h"
#include "surrender/SDLGPUDevice.h"
#include "surrender/srGERD.h"
#include "surrender/srImageIO.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srTriMeshPipeline.h"
#include "temporary_directory.h"
#include "wiz8/bink_video.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/overlay_scenes.h"
#include "wiz8/platform/input.h"
#include "wiz8/surface2d.h"
#include "wiz8/surface_storage.h"

class srScene;

void PresentMenuOverlayFrame();
void SaveJpegScreenshot();
extern unsigned char g_fullscreen;
unsigned char InitializeMouseCursorScene();
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
    REQUIRE(srGERD::getActive() == g_gerd.get());
    REQUIRE(InitializeRendererSceneObjects());
    auto targa = srImage::load("data\\automap\\map_monsterfriendly_a.tga");
    REQUIRE((targa && targa->w > 0 && targa->h > 0));
    auto pipeline = &g_gerd->meshPipeline();
    REQUIRE(pipeline);
    pipeline->Flush();
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
        auto error = g_gerd->beginFrame();
        if (error != srGERD::ERROR_NONE)
            fprintf(stderr, "begin frame %d: %d window %p open %d\n", frame, int(error),
                    static_cast<void*>(ghWindow), g_gerd->isWindowOpen());
        REQUIRE(error == srGERD::ERROR_NONE);
        g_gerd->setClearColor(0, 0, 0, 1);
        g_gerd->clear(SDLGPUDevice::BUFFER_COLOR | SDLGPUDevice::BUFFER_DEPTH);
        g_gerd->matrixMode(srGERD::MATRIX_MODELVIEW);
        g_gerd->loadIdentity();
        g_surface_node->DrawTiles(g_gerd.get());
        RenderScene(g_overlay_scenes.cursor.get(), g_overlay_scenes.camera.get(), nullptr, false);
        g_gerd->endFrame();
    }
    auto buffer = g_gerd->lockBuffer();
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
    g_gerd->unlockBuffer();
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
        buffer = g_gerd->lockBuffer();
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
        g_gerd->unlockBuffer();
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

    // Reopening must publish the renderer again and rebuild invalidated texture caches.
    g_gerd->closeWindow(static_cast<srGERD::e_closeHint>(0));
    REQUIRE(srGERD::getActive() == nullptr);
    REQUIRE(OpenRendererWindow());
    REQUIRE(srGERD::getActive() == g_gerd.get());
    PresentMenuOverlayFrame();

    g_screenshot_index = 0;
    SaveJpegScreenshot();
    auto screenshot_file = wiz8::open_file(wiz8::user_path("Wiz800000.JPG"));
    auto screenshot = srImage::load("screenshot.jpg", *screenshot_file);
    REQUIRE((screenshot && screenshot->w == 640 && screenshot->h == 480));
    puts("retail Targa import and game JPEG screenshot round-trip");

    // Reusing a resource's address must not find its retired cache entry. Deferred
    // GPU destruction must also leave the replacement alive when the old entry is drained.
    {
        auto red = CreateCpuStorage(1, 1, SDL_PIXELFORMAT_BGRA32);
        auto green = CreateCpuStorage(1, 1, SDL_PIXELFORMAT_BGRA32);
        REQUIRE(SDL_FillSurfaceRect(red.get(), nullptr, 0xffff0000));
        REQUIRE(SDL_FillSurfaceRect(green.get(), nullptr, 0xff00ff00));
        std::optional<srTexture> texture{std::shared_ptr<SDL_Surface>(std::move(red))};
        const std::array<float, 12> triangle{-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1};
        g_gerd->matrixMode(srGERD::MATRIX_PROJECTION);
        g_gerd->loadIdentity();
        g_gerd->matrixMode(srGERD::MATRIX_MODELVIEW);
        g_gerd->loadIdentity();
        g_gerd->setVertexPointer(4, srRendererDefs::TYPE_FLOAT, 16, triangle.data(), 3);
        g_gerd->setVertexArrayMask(1 << srRendererDefs::VERTEX_ARRAY_POSITIONS);
        g_gerd->setCullMode(srGERD::CULL_NONE);
        srShader shader;
        shader.value = srShader::PASS_ALWAYS | srShader::MASK_COLOR_WRITE |
                       srShader::MASK_TEXTURING |
                       (srShader::SRCBLEND_ONE << srShader::SRCBLEND_SHIFT);
        g_gerd->setShader(shader);
        const auto draw = [&] {
            g_gerd->setTexture(&*texture, 0);
            g_gerd->drawArrays(srRendererDefs::PRIMITIVE_TRIANGLES, 0, 3);
        };
        draw();
        texture.reset();
        texture.emplace(std::shared_ptr<SDL_Surface>(std::move(green)));
        draw();
        g_gerd->invalidateTextureCache();
        draw();
        buffer = g_gerd->lockBuffer();
        REQUIRE(buffer);
        REQUIRE((ReadARGB(*buffer, 320, 240) & 0xffffff) == 0x00ff00);
        g_gerd->unlockBuffer();
    }

    ShutdownVideoManager();
    REQUIRE(g_gerd == nullptr);
    REQUIRE(srGERD::getActive() == nullptr);

    SDL_Quit();
}

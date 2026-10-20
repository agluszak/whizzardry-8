// SDL 3.4 GPU 2D renderer prototype for the UI canvas.
//
// Draws the same canvas operations through the game's CPU canvas (DrawSprite,
// ShadowRect and fills on an ARGB1555 surface) and through GpuCanvas, then
// compares every pixel. The window presents the GPU canvas by blitting its
// SDL_GPUTexture on the shared device, as the compositor would sample it.

#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>

#include "gpu_canvas.h"
#include "poc_app.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/shading.h"
#include "wiz8/canvas/sprite.h"

namespace {
using namespace render_poc;

// Artwork frames: palette indices including opaque index 0, with transparent
// runs outside an ellipse.
SpriteSheet artSheet()
{
    SpriteSheet sheet;
    for (int index = 0; index < 256; ++index)
        sheet.palette[index] =
            SDL_Color{static_cast<Uint8>(index * 7), static_cast<Uint8>(index * 3),
                      static_cast<Uint8>(255 - index), 255};
    sheet.colors = Create16BPPPalette(sheet.palette);
    for (const auto [width, height, offset] :
         {std::tuple{48, 40, SDL_Point{2, 3}}, std::tuple{31, 57, SDL_Point{-4, 1}},
          std::tuple{90, 22, SDL_Point{0, 0}}}) {
        SpriteFrame frame;
        frame.width = width;
        frame.height = height;
        frame.offset = offset;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float u = (x + .5f) / width * 2 - 1, v = (y + .5f) / height * 2 - 1;
                frame.indices.push_back(u * u + v * v > 1 ? SpriteFrame::kTransparent
                                        : (x + y) % 9 == 0
                                            ? 0
                                            : static_cast<std::uint16_t>((x * 7 + y * 3) % 256));
            }
        }
        sheet.frames.push_back(std::move(frame));
    }
    return sheet;
}

// Mono glyphs: 0 background, 1 shadow, others foreground, plus transparent runs.
SpriteSheet glyphSheet()
{
    SpriteSheet sheet;
    for (int glyph = 0; glyph < 8; ++glyph) {
        SpriteFrame frame;
        frame.width = 8;
        frame.height = 11;
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const int value = (x * 3 + y * 5 + glyph * 7) % 11;
                frame.indices.push_back(value == 0   ? SpriteFrame::kTransparent
                                        : value < 4  ? 0
                                        : value == 4 ? 1
                                                     : static_cast<std::uint16_t>(value));
            }
        sheet.frames.push_back(std::move(frame));
    }
    return sheet;
}

// The same operations on any canvas: a CPU surface or the GPU canvas.
template<class Canvas> void drawScene(Canvas& canvas, double time)
{
    static const SpriteSheet art = artSheet();
    static const SpriteSheet glyphs = glyphSheet();
    static const Palette16 shaded = Create16BPPPaletteShaded(art.palette, 200, 120, 80, false);
    // A palette whose entry 0 writes transparent black.
    static const Palette16 clearing = [] {
        auto palette = art.colors;
        palette[0] = 0;
        return palette;
    }();
    const int shift = static_cast<int>(std::lround(std::sin(time) * 40));

    canvas.fill({0, 0, 640, 480}, Get16BPPColor({40, 60, 90, 255}));
    canvas.fill({100, 280, 260, 150}, 0); // cleared overlay region, as EraseRegion leaves it
    for (int index = 0; index < 12; ++index)
        canvas.drawSprite(art, index % 3, 20 + index * 52 + shift, 30 + (index % 4) * 45,
                          {.mirror = index % 2 == 1});
    canvas.drawSprite(art, 0, -20, 200);                 // clipped by the canvas edge
    canvas.drawSprite(art, 1, 620, 460);
    canvas.drawSprite(art, 2, 130 + shift, 300, {.palette = &shaded});
    canvas.drawSprite(art, 0, 200, 330, {.palette = &clearing});
    const SDL_Rect clip{60, 220, 420, 160};
    canvas.setClip(&clip);
    for (int index = 0; index < 6; ++index)
        canvas.drawSprite(art, index % 3, 40 + index * 80 - shift, 210, {.mirror = index == 2});
    canvas.setClip(nullptr);
    const std::array colours{
        std::tuple{Get16BPPColor({255, 230, 120, 255}), std::uint16_t{0}, std::uint16_t{0}},
        std::tuple{Get16BPPColor({255, 255, 255, 255}), Get16BPPColor({20, 20, 20, 255}),
                   std::uint16_t{0}},
        std::tuple{Get16BPPColor({120, 255, 120, 255}), std::uint16_t{0},
                   Get16BPPColor({0, 0, 0, 255})}};
    for (std::size_t line = 0; line < colours.size(); ++line) {
        const auto [foreground, background, shadow] = colours[line];
        for (int glyph = 0; glyph < 40; ++glyph)
            canvas.drawSprite(glyphs, glyph % 8, 380 + glyph * 6, 300 + int(line) * 14,
                              {.blend = SpriteBlend::Mono,
                               .foreground = foreground,
                               .background = background,
                               .shadow = shadow});
    }
    canvas.shadowRect({80, 250, 200, 120});  // over artwork and the cleared region
    canvas.shadowRect({150, 300, 220, 100}); // overlapping: shaded twice
}

// The game's CPU canvas.
class SurfaceCanvas {
public:
    SurfaceCanvas() : surface(CreateFrameSurface(GpuCanvas::kWidth, GpuCanvas::kHeight)) {}
    void setClip(const SDL_Rect* clip) { SDL_SetSurfaceClipRect(surface.get(), clip); }
    void fill(const SDL_Rect& area, std::uint16_t color)
    {
        SDL_FillSurfaceRect(surface.get(), &area, color);
    }
    void drawSprite(const SpriteSheet& sprites, std::size_t frame, int x, int y,
                    SpriteDraw options = {})
    {
        DrawSprite(*surface, sprites, frame, x, y, options);
    }
    void shadowRect(const SDL_Rect& area) { ShadowRect(*surface, area); }

    std::vector<std::uint16_t> pixels() const
    {
        std::vector<std::uint16_t> result(GpuCanvas::kWidth * GpuCanvas::kHeight);
        for (int y = 0; y < GpuCanvas::kHeight; ++y)
            std::copy_n(reinterpret_cast<const std::uint16_t*>(
                            static_cast<const std::uint8_t*>(surface->pixels) + y * surface->pitch),
                        GpuCanvas::kWidth, result.begin() + y * GpuCanvas::kWidth);
        return result;
    }

private:
    SDLSurfaceOwner surface;
};

std::vector<std::uint8_t> toRgba(const std::vector<std::uint16_t>& pixels)
{
    std::vector<std::uint8_t> rgba;
    const auto* format = SDL_GetPixelFormatDetails(kFramePixelFormat);
    for (const auto pixel : pixels) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(pixel, format, nullptr, &r, &g, &b, &a);
        rgba.insert(rgba.end(), {r, g, b, a});
    }
    return rgba;
}

int run(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SetShadeTablePercent(.6f);
    auto* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!device) throw std::runtime_error(SDL_GetError());
    const auto flags = options.hidden ? SDL_WINDOW_HIDDEN : SDL_WindowFlags{0};
    auto* window =
        SDL_CreateWindow("Whizzardry - SDL GPU canvas", options.width, options.height, flags);
    if (!window || !SDL_ClaimWindowForGPUDevice(device, window))
        throw std::runtime_error(SDL_GetError());

    int status = 0;
    {
        GpuCanvas canvas(device);
        if (options.verify) {
            const double time = options.time.value_or(1.3);
            SurfaceCanvas reference;
            drawScene(reference, time);
            drawScene(canvas, time);
            const auto expected = reference.pixels();
            const auto actual = canvas.readArgb1555();
            std::size_t mismatches = 0;
            for (std::size_t pixel = 0; pixel < expected.size(); ++pixel)
                mismatches += expected[pixel] != actual[pixel];
            std::printf("SDL GPU canvas vs CPU canvas: %zu of %zu ARGB1555 pixels differ\n",
                        mismatches, expected.size());
            if (mismatches != 0) {
                status = 1;
                savePng("cpu_canvas.png", toRgba(expected), GpuCanvas::kWidth, GpuCanvas::kHeight,
                        false);
            }
        }
        FrameClock clock;
        std::vector<std::uint8_t> last;
        for (int frame = 0; !options.frames || frame < *options.frames; ++frame) {
            bool quit = false;
            for (SDL_Event event; SDL_PollEvent(&event);)
                quit |= event.type == SDL_EVENT_QUIT ||
                        (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
            if (quit) break;
            drawScene(canvas, clock.sceneTime(options));
            if (options.frames && frame + 1 == *options.frames && !options.screenshot.empty())
                savePng(options.screenshot, toRgba(canvas.readArgb1555()), GpuCanvas::kWidth,
                        GpuCanvas::kHeight, false);
            canvas.flush();
            // The compositor's view: the canvas is an SDL_GPUTexture on the shared device.
            auto* commands = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUTexture* swapchain = nullptr;
            Uint32 width = 0, height = 0;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &width,
                                                       &height))
                throw std::runtime_error(SDL_GetError());
            if (swapchain) {
                SDL_GPUBlitInfo blit{};
                blit.source = {canvas.gpuTexture(), 0, 0, 0, 0, GpuCanvas::kWidth,
                               GpuCanvas::kHeight};
                blit.destination = {swapchain, 0, 0, 0, 0, width, height};
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(commands, &blit);
            }
            SDL_SubmitGPUCommandBuffer(commands);
            clock.frameDone();
        }
        std::printf("SDL GPU canvas: %s\n", clock.summary().c_str());
    }
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return status;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}

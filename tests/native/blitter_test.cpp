#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "himage.h"
#include "image.h"
#include "shading.h"
#include "sprite.h"
#include "surrender/srColorSurface.h"
#include "surrender/srPixelConvert.h"
#include "surrender_test_fixture.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/surface_storage.h"

// Recovered helper used by UI overlays; retain its destination-first copy contract.
unsigned char CopySurfaceWithBorder(srColorSurface*, const W8ControlsRect*, SDL_Surface&, float*,
                                    float*, float*, float*);

namespace {
using Surface = SDLSurfaceOwner;

Surface surface(int width, int height)
{
    return CreateCpuStorage(width, height, SDL_PIXELFORMAT_RGB565);
}

std::uint16_t& pixel(SDL_Surface& image, int x, int y)
{
    return static_cast<std::uint16_t*>(image.pixels)[y * image.pitch / 2 + x];
}

std::vector<std::uint16_t> pixels(SDL_Surface& image)
{
    std::vector<std::uint16_t> result;
    result.reserve(image.w * image.h);
    for (int y = 0; y < image.h; ++y)
        for (int x = 0; x < image.w; ++x) result.push_back(pixel(image, x, y));
    return result;
}

std::vector<std::uint16_t> golden(std::string_view name)
{
    std::ifstream input(WIZ8_BLITTER_EXPECTED);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream values(line);
        std::string entry;
        values >> entry;
        if (entry != name) continue;
        std::vector<std::uint16_t> result;
        std::string value;
        while (values >> value)
            result.push_back(static_cast<std::uint16_t>(std::stoul(value, nullptr, 0)));
        return result;
    }
    return {};
}

SpriteSheet sprite_sheet(unsigned width, unsigned height, std::vector<std::uint16_t> indices)
{
    SpriteSheet sprites;
    sprites.frames.resize(1);
    sprites.frames[0].width = static_cast<int>(width);
    sprites.frames[0].height = static_cast<int>(height);
    sprites.frames[0].indices = std::move(indices);
    for (std::size_t index = 0; index < sprites.colors.size(); ++index)
        sprites.colors[index] = static_cast<std::uint16_t>(0x1000 + index);
    return sprites;
}

constexpr int blitter_width = 648;
constexpr int blitter_height = 20;
constexpr int blitter_pitch = blitter_width * 2;

void reset_surface(SDL_Surface& image, std::uint16_t multiplier, std::uint16_t offset)
{
    auto* pixels = static_cast<std::uint16_t*>(image.pixels);
    for (int index = 0; index < image.pitch * image.h / 2; ++index)
        pixels[index] = static_cast<std::uint16_t>(index * multiplier + offset);
}

SpriteSheet legacy_sprite(int data, const std::array<std::uint16_t, 256>& palette)
{
    const unsigned width = data == 2 ? 140 : 15;
    constexpr unsigned height = 5;
    SpriteSheet sprites;
    sprites.frames.resize(1);
    SpriteFrame& frame = sprites.frames[0];
    frame.width = static_cast<int>(width);
    frame.height = static_cast<int>(height);
    frame.offset = {1, -1};
    frame.indices.assign(std::size_t(width) * height, 0);

    for (unsigned y = 0; y < height; ++y) {
        std::uint16_t* row = frame.indices.data() + std::size_t(y) * width;
        unsigned x = 0;
        unsigned run = 0;
        constexpr unsigned lengths[]{1, 2, 4, 7, 127};
        while (x < width) {
            const unsigned count = std::min(lengths[(run + y) % 5], width - x);
            const bool transparent = (run + y) % 3 == 1;
            for (unsigned index = 0; index < count; ++index) {
                row[x + index] = transparent ? SpriteFrame::kTransparent
                                 : data == 1
                                     ? static_cast<std::uint16_t>((x + index + y) % 4)
                                     : static_cast<std::uint16_t>((x + index + 11 * y) % 255 + 1);
            }
            x += count;
            ++run;
        }
    }
    sprites.colors = palette;
    return sprites;
}

std::vector<std::string> legacy_goldens()
{
    std::ifstream file(WIZ8_BLITTER_EXPECTED);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string name;
        fields >> name;
        if (name.starts_with("Blt") || name == "DrawSpriteMono" || name == "FillRect16BPP")
            result.push_back(line);
    }
    return result;
}

bool matches_legacy_golden(const SDL_Surface& image, const SDL_Rect& rect, std::string_view name,
                           int variant, bool result, const std::string& expected)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto* bytes = static_cast<const std::uint8_t*>(image.pixels);
    for (int index = 0; index < image.pitch * image.h; ++index)
        hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);

    char actual[256];
    std::snprintf(actual, sizeof(actual), "%.*s %d %u %d,%d,%d,%d %08x%08x",
                  static_cast<int>(name.size()), name.data(), variant, result, rect.x, rect.y,
                  rect.x + rect.w, rect.y + rect.h, static_cast<unsigned>(hash >> 32),
                  static_cast<unsigned>(hash));
    return expected == actual;
}
}

TEST_CASE("sprite draws distinguish opaque index zero from transparent runs", "[blit]")
{
    auto target = surface(5, 4);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    auto sprites = sprite_sheet(3, 2, {0, SpriteFrame::kTransparent, 2, 3, 4, 5});

    DrawSprite(*target, sprites, 0, 1, 1);
    REQUIRE(pixels(*target) == golden("sprite-transparent"));
}

TEST_CASE("sprite mirroring respects the target surface clip", "[blit]")
{
    auto target = surface(5, 2);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    auto sprites = sprite_sheet(3, 1, {1, 2, 3});
    const SDL_Rect clip{1, 0, 2, 1};
    REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));

    DrawSprite(*target, sprites, 0, 0, 0, {.mirror = true});

    REQUIRE(pixels(*target) == golden("sprite-mirror-clip"));
}

TEST_CASE("sprite tiling keeps a negative offset phase", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    auto sprites = sprite_sheet(2, 2, {0, 1, 2, 3});
    const SDL_Rect area{0, 0, 5, 5};

    TileSprite(*target, area, sprites, 0, SDL_Point{-1, -1});

    REQUIRE(pixels(*target) == golden("sprite-negative-tile"));
}

TEST_CASE("sprite tiling phase is relative to its destination area", "[blit]")
{
    auto target = surface(5, 1);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    const auto sprites = sprite_sheet(2, 1, {1, 2});
    const SDL_Rect area{1, 0, 3, 1};

    TileSprite(*target, area, sprites, 0, SDL_Point{0, 0});

    REQUIRE(pixel(*target, 0, 0) == 0x7777);
    REQUIRE(pixel(*target, 1, 0) == 0x1001);
    REQUIRE(pixel(*target, 2, 0) == 0x1002);
    REQUIRE(pixel(*target, 3, 0) == 0x1001);
    REQUIRE(pixel(*target, 4, 0) == 0x7777);
}

TEST_CASE("sprite tiling period includes frame offsets", "[blit]")
{
    auto target = surface(7, 1);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    auto sprites = sprite_sheet(2, 1, {1, 2});
    sprites.frames[0].offset.x = 1;
    const SDL_Rect area{0, 0, 7, 1};

    TileSprite(*target, area, sprites, 0, SDL_Point{0, 0});

    REQUIRE(pixel(*target, 0, 0) == 0x7777);
    REQUIRE(pixel(*target, 1, 0) == 0x1001);
    REQUIRE(pixel(*target, 2, 0) == 0x1002);
    REQUIRE(pixel(*target, 3, 0) == 0x7777);
    REQUIRE(pixel(*target, 4, 0) == 0x1001);
    REQUIRE(pixel(*target, 5, 0) == 0x1002);
    REQUIRE(pixel(*target, 6, 0) == 0x7777);
}

TEST_CASE("sprite tiling preserves negative-offset edge cells", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    auto sprites = sprite_sheet(3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    sprites.frames[0].offset = {-1, -1};
    const SDL_Rect area{0, 0, 5, 5};

    TileSprite(*target, area, sprites, 0, SDL_Point{0, 0});

    const std::uint16_t expected[]{0x1005, 0x1004, 0x1005, 0x1004, 0x1005, 0x1002, 0x1001,
                                   0x1002, 0x1001, 0x1002, 0x1005, 0x1004, 0x1005, 0x1004,
                                   0x1005, 0x1002, 0x1001, 0x1002, 0x1001, 0x1002, 0x1005,
                                   0x1004, 0x1005, 0x1004, 0x1005};
    REQUIRE(pixels(*target) == std::vector<std::uint16_t>(expected, expected + 25));
}

TEST_CASE("self blits preserve overlapping rows in either direction", "[blit]")
{
    auto target = surface(4, 6);
    REQUIRE(target);
    const auto reset = [&] {
        for (int y = 0; y < target->h; ++y)
            for (int x = 0; x < target->w; ++x)
                pixel(*target, x, y) = static_cast<std::uint16_t>(y * 10 + x);
    };

    reset();
    const SDL_Rect source_below{0, 0, 3, 4};
    BlitImage(*target, 0, 1, *target, &source_below);
    REQUIRE(pixel(*target, 0, 0) == 0);
    REQUIRE(pixel(*target, 2, 1) == 2);
    REQUIRE(pixel(*target, 1, 2) == 11);
    REQUIRE(pixel(*target, 2, 3) == 22);
    REQUIRE(pixel(*target, 0, 4) == 30);
    REQUIRE(pixel(*target, 3, 4) == 43);
    REQUIRE(pixel(*target, 0, 5) == 50);

    reset();
    const SDL_Rect source_above{0, 1, 3, 4};
    BlitImage(*target, 0, 0, *target, &source_above);
    REQUIRE(pixel(*target, 0, 0) == 10);
    REQUIRE(pixel(*target, 2, 1) == 22);
    REQUIRE(pixel(*target, 1, 2) == 31);
    REQUIRE(pixel(*target, 2, 3) == 42);
    REQUIRE(pixel(*target, 0, 4) == 40);
    REQUIRE(pixel(*target, 3, 4) == 43);
    REQUIRE(pixel(*target, 0, 5) == 50);

    reset();
    const SDL_Rect source_left{1, 0, 3, 1};
    BlitImage(*target, 0, 0, *target, &source_left);
    REQUIRE(pixel(*target, 0, 0) == 1);
    REQUIRE(pixel(*target, 1, 0) == 2);
    REQUIRE(pixel(*target, 2, 0) == 3);
    REQUIRE(pixel(*target, 3, 0) == 3);
}

TEST_CASE("surface fills keep half-open rectangle edges", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    const SDL_Rect area{1, 1, 2, 2};
    REQUIRE(SDL_FillSurfaceRect(target.get(), &area, 0x1234));

    REQUIRE(pixel(*target, 0, 1) == 0x7777);
    REQUIRE(pixel(*target, 1, 1) == 0x1234);
    REQUIRE(pixel(*target, 2, 2) == 0x1234);
    REQUIRE(pixel(*target, 3, 2) == 0x7777);
    REQUIRE(pixel(*target, 2, 3) == 0x7777);
}

TEST_CASE("shadow rectangles preserve inclusive legacy endpoints", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x1234));
    ShadeTable[0x1234] = 0x5678;
    const SDL_Rect inclusive_area{1, 1, 3, 3};
    ShadowRect(*target, inclusive_area);

    REQUIRE(pixel(*target, 0, 1) == 0x1234);
    REQUIRE(pixel(*target, 1, 1) == 0x5678);
    REQUIRE(pixel(*target, 3, 3) == 0x5678);
    REQUIRE(pixel(*target, 4, 3) == 0x1234);
    REQUIRE(pixel(*target, 3, 4) == 0x1234);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Overlay border copies preserve offset source coordinates",
                 "[blit]")
{
    auto source = surface(6, 5);
    for (int y = 0; y < source->h; ++y)
        for (int x = 0; x < source->w; ++x) pixel(*source, x, y) = 0x8000 + y * 16 + x;
    srColorSurface target(srPixelConvert::SURFACE_ARGB1555, 8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) target.setPixelRaw(x, y, 0xeeee);
    const W8ControlsRect rect{2, 1, 5, 3};
    float scale_x, scale_y, mapping_x, mapping_y;
    REQUIRE(
        CopySurfaceWithBorder(&target, &rect, *source, &scale_x, &scale_y, &mapping_x, &mapping_y));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const unsigned expected = x < 5 && y < 4 ? pixel(*source, 2 + std::clamp(x - 1, 0, 2),
                                                             1 + std::clamp(y - 1, 0, 1))
                                                     : 0xeeee;
            REQUIRE(target.getPixelRaw(x, y) == expected);
        }
}

TEST_CASE("Sprite transparency preserves opaque RGB regardless of alpha bits", "[blit]")
{
    struct RestoreFormat {
        SDL_PixelFormat original = g_sgp_pixel_format;
        ~RestoreFormat()
        {
            ReleasePrimarySurface();
            g_sgp_pixel_format = original;
        }
    } restore;
    for (auto format :
         {SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_ARGB1555, SDL_PIXELFORMAT_XRGB1555}) {
        g_sgp_pixel_format = format;
        REQUIRE(InitializePrimarySurface());
        auto sprites = sprite_sheet(4, 1, {0, 1, SpriteFrame::kTransparent, 2});
        for (unsigned i = 0; i < sprites.colors.size(); ++i) sprites.colors[i] = 0x8000 | i;
        const auto image = RenderSpriteToImage(sprites, 0);
        REQUIRE(image);
        auto target = CreateCpuStorage(5, 1, format);
        REQUIRE(target);
        for (bool mirrored : {false, true, false}) {
            REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x1234));
            DrawSprite(*target, sprites, 0, 0, 0, {.mirror = mirrored});
            const std::uint16_t expected[]{0x8000, 0x8001, 0x1234, 0x8002};
            for (int x = 0; x < 4; ++x)
                REQUIRE(pixel(*target, x, 0) == expected[mirrored ? 3 - x : x]);
            REQUIRE(pixel(*target, 4, 0) == 0x1234);
        }
        // Surface consumers must also preserve opaque black when SDL blits it.
        REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x1234));
        REQUIRE(SDL_BlitSurface(image.get(), nullptr, target.get(), nullptr));
        REQUIRE(pixel(*target, 0, 0) == 0x8000);
        REQUIRE(pixel(*target, 1, 0) == 0x8001);
        REQUIRE(pixel(*target, 2, 0) == 0x1234);
        REQUIRE(pixel(*target, 3, 0) == 0x8002);
    }
}

TEST_CASE("tiled sprite fills keep half-open rectangle edges", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    const auto sprites = sprite_sheet(1, 1, {1});
    const SDL_Rect area{1, 1, 2, 2};
    TileSprite(*target, area, sprites, 0, SDL_Point{0, 0});

    REQUIRE(pixel(*target, 0, 1) == 0x7777);
    REQUIRE(pixel(*target, 1, 1) == 0x1001);
    REQUIRE(pixel(*target, 2, 2) == 0x1001);
    REQUIRE(pixel(*target, 3, 2) == 0x7777);
    REQUIRE(pixel(*target, 2, 3) == 0x7777);
}

TEST_CASE("image blits keep half-open source and destination edges", "[blit]")
{
    auto target = surface(5, 2);
    auto source = surface(3, 1);
    REQUIRE(target);
    REQUIRE(source);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    pixel(*source, 0, 0) = 0x1111;
    pixel(*source, 1, 0) = 0x2222;
    pixel(*source, 2, 0) = 0x3333;
    const SDL_Rect source_rect{0, 0, 2, 1};
    BlitImage(*target, 1, 0, *source, &source_rect);

    REQUIRE(pixel(*target, 0, 0) == 0x7777);
    REQUIRE(pixel(*target, 1, 0) == 0x1111);
    REQUIRE(pixel(*target, 2, 0) == 0x2222);
    REQUIRE(pixel(*target, 3, 0) == 0x7777);
    REQUIRE(pixel(*target, 2, 1) == 0x7777);

    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    const SDL_Rect clip{2, 0, 1, 1};
    REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));
    BlitImage(*target, 1, 0, *source, &source_rect);
    REQUIRE(pixel(*target, 1, 0) == 0x7777);
    REQUIRE(pixel(*target, 2, 0) == 0x2222);
    REQUIRE(pixel(*target, 3, 0) == 0x7777);
}

TEST_CASE("hatched rectangles honor the target clip and preserve their phase", "[blit]")
{
    auto target = surface(5, 5);
    REQUIRE(target);
    REQUIRE(SDL_FillSurfaceRect(target.get(), nullptr, 0x7777));
    const SDL_Rect clip{1, 1, 3, 3};
    REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));

    HatchRect(*target, SDL_Rect{0, 0, 5, 5});

    REQUIRE(pixels(*target) == golden("hatch-clip"));
}

TEST_CASE("SDL surface blits preserve equivalent legacy pixel goldens", "[blit]")
{
    auto target = surface(blitter_width, blitter_height);
    auto source = surface(blitter_width, blitter_height);
    auto indexed_target =
        SDLSurfaceOwner{SDL_CreateSurface(blitter_pitch, blitter_height, SDL_PIXELFORMAT_INDEX8),
                        SDL_DestroySurface};
    auto indexed_source =
        SDLSurfaceOwner{SDL_CreateSurface(blitter_pitch, blitter_height, SDL_PIXELFORMAT_INDEX8),
                        SDL_DestroySurface};
    REQUIRE(target);
    REQUIRE(source);
    REQUIRE(indexed_target);
    REQUIRE(indexed_source);
    REQUIRE(target->pitch == blitter_pitch);
    REQUIRE(source->pitch == blitter_pitch);
    REQUIRE(indexed_target->pitch == blitter_pitch);
    REQUIRE(indexed_source->pitch == blitter_pitch);
    class PixelFormatRestore {
    public:
        explicit PixelFormatRestore(SDL_PixelFormat format) : previous_(g_sgp_pixel_format)
        {
            g_sgp_pixel_format = format;
        }
        ~PixelFormatRestore() { g_sgp_pixel_format = previous_; }

    private:
        SDL_PixelFormat previous_;
    } restore_pixel_format(target->format);

    const SDL_Rect full{0, 0, 640, blitter_height};
    std::array<std::uint16_t, 256> palette{};
    for (unsigned index = 0; index < 65536; ++index) {
        ShadeTable[index] = static_cast<std::uint16_t>((index * 37) ^ 0x1234);
        if (index < palette.size())
            palette[index] = static_cast<std::uint16_t>((index * 257) ^ 0x57ab);
    }

    const auto check = [&](std::string_view name, int variant, bool result, const SDL_Rect& rect,
                           std::size_t& expected_index, const std::vector<std::string>& expected) {
        REQUIRE(expected_index < expected.size());
        REQUIRE(
            matches_legacy_golden(*target, rect, name, variant, result, expected[expected_index]));
        ++expected_index;
    };
    const auto expected = legacy_goldens();
    std::size_t expected_index = 0;

    for (int data = 0; data < 3; ++data) {
        const SpriteSheet sprites = legacy_sprite(data, palette);
        REQUIRE(!sprites.frames[0].indices.empty());
        for (int variant = 0; variant < 8; ++variant) {
            SDL_Rect clip =
                (data == 1 || variant == 0 || variant == 6) ? full : SDL_Rect{3, 3, 9, 4};
            const int x = variant == 1 ? -2 : variant == 7 ? 660 : 4;
            const int y = variant == 2 ? 1 : variant == 3 ? 6 : 4;
            reset_surface(*target, 337, 19);
            REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));
            DrawSprite(*target, sprites, 0, x, y);
            check("Blt8BPPDataTo16BPPBufferTransparentClip", data * 8 + variant, true, clip,
                  expected_index, expected);

            reset_surface(*target, 337, 19);
            REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));
            DrawSprite(*target, sprites, 0, x, y,
                       {.blend = SpriteBlend::Mono,
                        .foreground = 0xa351,
                        .background = static_cast<std::uint16_t>(variant % 2 ? 0 : 0x2f14),
                        .shadow = static_cast<std::uint16_t>(variant % 3 ? 0x1234 : 0)});
            check("DrawSpriteMono", data * 8 + variant, true, clip, expected_index, expected);
        }

        reset_surface(*target, 337, 19);
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        DrawSprite(*target, sprites, 0, 4, 4);
        check("Blt8BPPDataTo16BPPBufferTransparent", data, true, full, expected_index, expected);

        reset_surface(*target, 337, 19);
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        DrawSprite(*target, sprites, 0, 4, 4, {.mirror = true});
        check("Blt8BPPDataTo16BPPBufferTransMirror", data, true, full, expected_index, expected);
    }

    for (unsigned width = 1; width <= 9; ++width) {
        const SDL_Rect source_rect{1, 4, static_cast<int>(width), 5};
        reset_surface(*target, 337, 19);
        reset_surface(*source, 113, 7);
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        BlitImage(*target, 3, 2, *source, &source_rect);
        check("Blt16BPPTo16BPP", width, true, full, expected_index, expected);

        reset_surface(*target, 337, 19);
        reset_surface(*source, 113, 7);
        const std::uint16_t key =
            static_cast<const std::uint16_t*>(source->pixels)[4 * blitter_width + 2];
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        BlitImage(*target, 3, 2, *source, &source_rect, {.color_key = key});
        check("Blt16BPPTo16BPPTrans", width, true, full, expected_index, expected);

        reset_surface(*target, 337, 19);
        reset_surface(*source, 113, 7);
        reset_surface(*indexed_target, 337, 19);
        reset_surface(*indexed_source, 113, 7);
        const SDL_Rect indexed_source_rect{1, 4, static_cast<int>(width), 5};
        BlitImage(*indexed_target, 3, 2, *indexed_source, &indexed_source_rect);
        std::memcpy(target->pixels, indexed_target->pixels, blitter_pitch * blitter_height);
        check("Blt8BPPTo8BPP", width, true, full, expected_index, expected);

        reset_surface(*target, 337, 19);
        const SDL_Rect overlap_rect{1, 2, static_cast<int>(width), 5};
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        BlitImage(*target, 2, 2, *target, &overlap_rect);
        check("Blt16BPPTo16BPP-overlap", width, true, full, expected_index, expected);

        reset_surface(*indexed_target, 337, 19);
        const SDL_Rect indexed_overlap_rect{1, 2, static_cast<int>(width), 5};
        BlitImage(*indexed_target, 2, 2, *indexed_target, &indexed_overlap_rect);
        std::memcpy(target->pixels, indexed_target->pixels, blitter_pitch * blitter_height);
        check("Blt8BPPTo8BPP-overlap", width, true, full, expected_index, expected);

        for (int x : {-3, 317, 637}) {
            reset_surface(*target, 337, 19);
            reset_surface(*source, 113, 7);
            REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
            const SDL_Rect mirror_source{0, 0, static_cast<int>(width), 5};
            BlitImage(*target, x, 2, *source, &mirror_source, {.mirror = true});
            check("Blt16BPPTo16BPPMirror", static_cast<int>(width * 1000 + x), true, full,
                  expected_index, expected);
        }

        reset_surface(*target, 337, 19);
        reset_surface(*source, 113, 7);
        BitmapImage bitmap(SDL_CreateSurface(blitter_pitch, blitter_height, SDL_PIXELFORMAT_INDEX8),
                           SDL_DestroySurface);
        REQUIRE(bitmap);
        Palette8 bitmap_palette{};
        const auto* details = SDL_GetPixelFormatDetails(target->format);
        REQUIRE(details);
        for (unsigned index = 0; index < palette.size(); ++index) {
            const std::uint16_t value = palette[index];
            SDL_Color color{0, 0, 0, SDL_ALPHA_OPAQUE};
            SDL_GetRGB(value, details, nullptr, &color.r, &color.g, &color.b);
            bitmap_palette[index] = color;
        }
        auto* sdl_palette = SDL_CreateSurfacePalette(bitmap.get());
        REQUIRE(sdl_palette);
        REQUIRE(SDL_SetPaletteColors(sdl_palette, bitmap_palette.data(), 0, 256));
        for (int y = 0; y < blitter_height; ++y)
            std::memcpy(static_cast<std::uint8_t*>(bitmap->pixels) + y * bitmap->pitch,
                        static_cast<const std::uint8_t*>(source->pixels) + y * blitter_pitch,
                        blitter_pitch);
        REQUIRE(Create16BPPPalette(bitmap_palette) == palette);
        auto indexed_surface = ConvertBitmapToFrameSurface(*bitmap);
        REQUIRE(indexed_surface);
        const SDL_Rect subrect{2, 3, static_cast<int>(width), 4};
        BlitImage(*target, 3, 2, *indexed_surface, &subrect);
        check("Blt8BPPDataSubTo16BPPBuffer", static_cast<int>(width), true, subrect, expected_index,
              expected);
    }

    std::array<std::uint8_t, 64> pattern{};
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            pattern[y * 8 + x] = static_cast<std::uint8_t>((x + 3 * y) % 5);
    for (int variant = 0; variant < 6; ++variant) {
        const SDL_Rect clip = variant % 2 ? SDL_Rect{2, 3, 16, 8} : full;
        const SDL_Rect legacy_area{-1, 1, 10 + variant, 11};
        const SDL_Rect requested{legacy_area.x, legacy_area.y, legacy_area.w + 1,
                                 legacy_area.h + 1};
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &clip));

        reset_surface(*target, 337, 19);
        PixelateRect(*target, requested, pattern, 0x7391);
        check("Blt16BPPBufferPixelateRectWithColor", variant, true, legacy_area, expected_index,
              expected);

        reset_surface(*target, 337, 19);
        HatchRect(*target, requested);
        check("Blt16BPPBufferHatchRect", variant, true, legacy_area, expected_index, expected);

        reset_surface(*target, 337, 19);
        ShadowRect(*target, requested);
        SDL_Rect visible{};
        REQUIRE(SDL_GetRectIntersection(&requested, &clip, &visible));
        const SDL_Rect clipped_legacy_area{visible.x, visible.y, visible.w - 1, visible.h - 1};
        check("Blt16BPPBufferShadowRect", variant, true, clipped_legacy_area, expected_index,
              expected);

        reset_surface(*target, 337, 19);
        REQUIRE(SDL_SetSurfaceClipRect(target.get(), &full));
        const SDL_Rect fill_rect{-1, 1, 11 + variant, 12};
        REQUIRE(SDL_FillSurfaceRect(target.get(), &fill_rect, 0x693d));
        check("FillRect16BPP", variant, true, clipped_legacy_area, expected_index, expected);
    }
    REQUIRE(expected_index == expected.size());
}

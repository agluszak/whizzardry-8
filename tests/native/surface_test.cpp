#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "compat/surfaces.h"
#include "vsurface.h"



namespace
{
std::uint32_t read(const void* data, unsigned bytes)
{
    std::uint32_t result = 0;
    memcpy(&result, data, bytes);
    return result;
}

std::vector<std::uint8_t> pixels(CpuSurface& owner)
{
    const auto& storage = *owner.surface;
    const auto* begin = static_cast<const std::uint8_t*>(storage.pixels);
    std::vector<std::uint8_t> result(begin, begin + storage.pitch * storage.h);
    return result;
}

void seed(CpuSurface& owner, unsigned salt)
{
    const auto& storage = *owner.surface;
    auto* begin = static_cast<std::uint8_t*>(storage.pixels);
    std::fill_n(begin, storage.pitch * storage.h, 0xa5);
    const unsigned bytes = SDL_BYTESPERPIXEL(owner.surface->format);
    for (int y = 0; y < storage.h; ++y)
        for (int x = 0; x < storage.w; ++x)
        {
            const std::uint32_t value = (salt + y * 211 + x * 79) * 0x10307u;
            memcpy(begin + y * storage.pitch + x * bytes, &value, bytes);
        }
}

// Reference is the previous packed-word contract, not SDL's pixel output.
std::vector<std::uint8_t> reference(CpuSurface& dest, const SDL_Rect& d, CpuSurface& source, const SDL_Rect& s,
                            bool sourceKey = false, bool destinationKey = false)
{
    auto result = pixels(dest);
    const auto input = pixels(source);
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.surface->format);
    const auto regions = dest.clipRegions.value_or(std::vector<SDL_Rect>{d});
    for (SDL_Rect region : regions)
    {
        const std::int32_t left = std::max({region.x, d.x, 0});
        const std::int32_t top = std::max({region.y, d.y, 0});
        const std::int32_t right = std::min({region.x + region.w, d.x + d.w, dest.surface->w});
        const std::int32_t bottom = std::min({region.y + region.h, d.y + d.h, dest.surface->h});
        region = SDL_Rect{left, top, right - left, bottom - top};
        if (region.w <= 0 || region.h <= 0)
            continue;
        SDL_Rect sample = s;
        if (dest.clipRegions)
        {
            const float scaleX = float(s.w) / (d.w);
            const float scaleY = float(s.h) / (d.h);
            const std::int32_t left = std::int32_t((region.x - d.x) * scaleX);
            const std::int32_t top = std::int32_t((region.y - d.y) * scaleY);
            sample.x += left;
            sample.y += top;
            sample.w -= std::int32_t(((d.x + d.w) - (region.x + region.w)) * scaleX) + left;
            sample.h -= std::int32_t(((d.y + d.h) - (region.y + region.h)) * scaleY) + top;
        }
        const auto mapping = dest.clipRegions ? region : d;
        for (int y = region.y; y < (region.y + region.h); ++y)
            for (int x = region.x; x < (region.x + region.w); ++x)
            {
                const int sx = sample.x + int(int64_t(x - mapping.x) *
                    (sample.w) / (mapping.w));
                const int sy = sample.y + int(int64_t(y - mapping.y) *
                    (sample.h) / (mapping.h));
                const std::uint32_t value = read(input.data() + sy * source.surface->pitch + sx * bytes, bytes);
                auto* target = result.data() + y * dest.surface->pitch + x * bytes;
                if (sourceKey && value >= source.sourceKey->low && value <= source.sourceKey->high)
                    continue;
                if (destinationKey && (read(target, bytes) < dest.destinationKey->low ||
                                       read(target, bytes) > dest.destinationKey->high))
                    continue;
                memcpy(target, &value, bytes);
            }
    }
    return result;
}

void differential()
{
    const std::array formats{
        SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_BGR24, SDL_PIXELFORMAT_XRGB1555,
        SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_XBGR1555, SDL_PIXELFORMAT_ARGB1555,
        SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_XRGB8888
    };
    for (SDL_PixelFormat format : formats)
    {
        auto src = CreateCpuSurface(7, 5, format);
        auto dest = CreateCpuSurface(17, 13, format);
        seed(*src, 11);
        if (SDL_ISPIXELFORMAT_INDEXED(format))
        {
            const SDL_Color colors[2]{{255, 1, 2, 255}, {3, 255, 4, 255}};
            REQUIRE(SDL_SetPaletteColors(SDL_GetSurfacePalette(src->surface.get()), colors, 0, 2));
            REQUIRE(SDL_GetSurfacePalette(src->surface.get())->colors[1].g == 255);
        }
        const auto input = pixels(*src);
        const unsigned bytes = SDL_BYTESPERPIXEL(format);
        const auto key = read(input.data() + bytes * 3, bytes);
        const auto high = bytes == 4 ? UINT32_MAX : (1u << (bytes * 8)) - 1;
        for (unsigned clipped = 0; clipped < 3; ++clipped)
        {
            dest->clipRegions.reset();
            if (clipped == 1)
            {
                const SDL_Rect clips[]{{0, 0, 9, 7}, {4, 3, 11, 9}, {0, 0, 9, 7},
                                   {14, 0, 3, 3}, {8, 9, 0, 3}};
                SetSurfaceClipRegions(*dest, clips);
                REQUIRE(dest->clipRegions->size() == 4);
            }
            if (clipped == 2)
                SetSurfaceClipRegions(*dest, {});
            for (int width = 1; width <= 18; ++width)
                for (int height : {1, 5, 10, 14})
                    for (const auto& origin : std::array<std::array<int, 2>, 3>{{{-2, -1}, {0, 0}, {1, 2}}})
                    for (unsigned keys = 0; keys < 4; ++keys)
                    {
                        seed(*dest, 3);
                        src->sourceKey = SurfaceColorKey{keys & 2 ? 0u : key, keys & 2 ? high / 2 : key};
                        dest->destinationKey = SurfaceColorKey{high / 4, std::uint32_t(high * uint64_t(3) / 4)};
                        const SDL_Rect d{origin[0], origin[1], width, height};
                        const SDL_Rect s{1, 0, 6, 5};
                        const auto expected = reference(*dest, d, *src, s, keys & 1, keys & 2);
                        BlitCpuSurface(*dest, &d, *src, &s, keys & 1, keys & 2);
                        const auto actual = pixels(*dest);
                        if (actual != expected)
                        {
                            const auto offset = std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin();
                            fprintf(stderr, "format %u/%x, clip %u, %dx%d, keys %u, byte %td: %02x != %02x\n",
                                    SDL_BITSPERPIXEL(format), static_cast<unsigned>(format), clipped,
                                    width, height, keys, offset, actual[offset], expected[offset]);
                            REQUIRE(actual == expected);
                        }
                    }
        }
        dest->clipRegions.reset();
        if (SDL_ISPIXELFORMAT_INDEXED(format))
        {
            std::array<SDL_Color, 256> white;
            white.fill({255, 255, 255, 255});
            REQUIRE(SDL_SetPaletteColors(SDL_GetSurfacePalette(dest->surface.get()), white.data(), 0, 256));
        }
        seed(*dest, 17);
        const SDL_Rect s{0, 0, 12, 10}, d{3, 2, 12, 10};
        const auto expected = reference(*dest, d, *dest, s);
        BlitCpuSurface(*dest, &d, *dest, &s);
        REQUIRE(pixels(*dest) == expected);
    }
}

void videoSurfaceClipping()
{
    REQUIRE(!CreateVideoSurface(0, 7, 16));
    REQUIRE(!CreateVideoSurface(8, 0, 16));
    REQUIRE(!CreateVideoSurface(8, 7, 24));
    auto sourceView = CreateVideoSurface(8, 7, 16);
    auto destView = CreateVideoSurface(8, 7, 16);
    auto& source = sourceView->cpu();
    auto& dest = destView->cpu();
    SDL_Rect destination{-2, -1, 6, 5}, sample{1, 1, 6, 5};
    REQUIRE(ClipCpuBlit(destination, sample, {0, 0, 8, 7}, {0, 0, 8, 7}));
    const SDL_Rect expectedDest{0, 0, 4, 4}, expectedSource{3, 2, 4, 4};
    REQUIRE(SDL_RectsEqual(&destination, &expectedDest));
    REQUIRE(SDL_RectsEqual(&sample, &expectedSource));
    destination = {1, 1, 6, 5};
    sample = {-2, -1, 6, 5};
    REQUIRE(ClipCpuBlit(destination, sample, {0, 0, 8, 7}, {0, 0, 8, 7}));
    REQUIRE(SDL_RectsEqual(&destination, &expectedSource));
    REQUIRE(SDL_RectsEqual(&sample, &expectedDest));
    SDL_Rect clips[]{{2, 1, 3, 2}};
    SetSurfaceClipRegions(destView->cpu(), clips);
    REQUIRE(dest.clipRegions->size() == 1);
    REQUIRE(SDL_RectsEqual(&dest.clipRegions->front(), &clips[0]));
    seed(source, 5);
    for (bool clipped : {false, true}) {
        if (!clipped) dest.clipRegions.reset();
        else SetSurfaceClipRegions(destView->cpu(), clips);
        for (int y : {-4, -1, 0, 3, 7})
            for (int x : {-5, -1, 0, 4, 8}) {
                seed(dest, 17);
                const SDL_Rect sample{1, 1, 6, 5}, mapping{x, y, 6, 5};
                const auto expected = reference(dest, mapping, source, sample);
                REQUIRE((BltVideoSurfaceToVideoSurface(*destView, *sourceView,
                                                    x, y, 0, &sample) ||
                      x >= 8 || y >= 7));
                REQUIRE(pixels(dest) == expected);
            }
    }
}

void fillsAndKeys()
{
    for (unsigned bits : {8, 16, 32})
    {
        const auto format = bits == 8 ? SDL_PIXELFORMAT_INDEX8 :
                            bits == 16 ? SDL_PIXELFORMAT_XRGB1555 : SDL_PIXELFORMAT_ARGB8888;
        auto surface = CreateCpuSurface(3, 3, format);
        const unsigned bytes = bits / 8;
        const auto& storage = *surface->surface;
        REQUIRE((storage.pitch >= int(3 * bytes) && storage.pitch % 4 == 0));
        memset(storage.pixels, 0xa5, storage.pitch * storage.h);
        const auto before = pixels(*surface);
        const SDL_Rect clips[]{{1, 0, 2, 2}};
        SetSurfaceClipRegions(*surface, clips);
        FillCpuSurface(*surface, 0xdeadbeef);
        const auto after = pixels(*surface);
        for (int y = 0; y < 3; ++y)
            for (int offset = 0; offset < storage.pitch; ++offset)
            {
                const auto index = y * storage.pitch + offset;
                if (y < 2 && offset >= int(bytes) && offset < int(3 * bytes))
                    REQUIRE(after[index] == std::uint8_t(0xdeadbeef >> (8 * (offset % bytes))));
                else
                    REQUIRE(after[index] == before[index]);
            }
    }
    auto src = CreateCpuSurface(2, 1, SDL_PIXELFORMAT_ARGB8888);
    auto dest = CreateCpuSurface(2, 1, SDL_PIXELFORMAT_ARGB8888);
    const std::uint32_t input[] {0x00123456, 0xff123456};
    const auto& storage = *src->surface;
    memcpy(storage.pixels, input, sizeof(input));
    FillCpuSurface(*dest, 0x11223344);
    src->sourceKey = SurfaceColorKey{input[0], input[0]};
    BlitCpuSurface(*dest, nullptr, *src, nullptr, true);
    const auto result = pixels(*dest);
    REQUIRE((read(result.data(), 4) == 0x11223344 && read(result.data() + 4, 4) == input[1]));
}
} // namespace

TEST_CASE("surface_test")
{
    differential();
    fillsAndKeys();
    videoSurfaceClipping();
}

TEST_CASE("CPU blit clipping preserves paired coordinates")
{
    SDL_Rect source{-2, 1, 8, 6};
    SDL_Rect destination{1, -2, 8, 6};
    REQUIRE(ClipCpuBlit(destination, source, {0, 0, 6, 4}, {0, 0, 5, 5}));
    REQUIRE((source.x == 0 && source.y == 3 && source.w == 3 && source.h == 2));
    REQUIRE((destination.x == 3 && destination.y == 0 && destination.w == 3 && destination.h == 2));

    source = {5, 0, 2, 2};
    destination = {0, 0, 2, 2};
    REQUIRE_FALSE(ClipCpuBlit(destination, source, {0, 0, 6, 4}, {0, 0, 5, 5}));
    source = {0, 0, 0, 2};
    REQUIRE_FALSE(ClipCpuBlit(destination, source, {0, 0, 6, 4}, {0, 0, 5, 5}));
}

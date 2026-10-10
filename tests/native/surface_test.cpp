#include <catch2/catch_test_macros.hpp>
#include "compat/surfaces.h"
#include "vsurface.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

BOOLEAN ClipReleatedSrcAndDestRectangles(HVSURFACE, HVSURFACE, SDL_Rect*, SDL_Rect*);


namespace
{
UINT32 read(const void* data, unsigned bytes)
{
    UINT32 result = 0;
    memcpy(&result, data, bytes);
    return result;
}

std::vector<BYTE> pixels(CpuSurface& owner)
{
    const auto lock = LockCpuSurface(owner);
    const auto* begin = static_cast<const BYTE*>(lock.pixels);
    std::vector<BYTE> result(begin, begin + lock.pitch * lock.height);
    UnlockCpuSurface(owner);
    return result;
}

void seed(CpuSurface& owner, unsigned salt)
{
    const auto lock = LockCpuSurface(owner);
    auto* begin = static_cast<BYTE*>(lock.pixels);
    std::fill_n(begin, lock.pitch * lock.height, 0xa5);
    const unsigned bytes = SDL_BYTESPERPIXEL(owner.surface->format);
    for (int y = 0; y < lock.height; ++y)
        for (int x = 0; x < lock.width; ++x)
        {
            const UINT32 value = (salt + y * 211 + x * 79) * 0x10307u;
            memcpy(begin + y * lock.pitch + x * bytes, &value, bytes);
        }
    UnlockCpuSurface(owner);
}

// Reference is the previous packed-word contract, not SDL's pixel output.
std::vector<BYTE> reference(CpuSurface& dest, const SDL_Rect& d, CpuSurface& source, const SDL_Rect& s,
                            bool sourceKey = false, bool destinationKey = false)
{
    auto result = pixels(dest);
    const auto input = pixels(source);
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.surface->format);
    const auto regions = dest.clipRegions.value_or(std::vector<SDL_Rect>{d});
    for (SDL_Rect region : regions)
    {
        const INT32 left = std::max({region.x, d.x, 0});
        const INT32 top = std::max({region.y, d.y, 0});
        const INT32 right = std::min({region.x + region.w, d.x + d.w, dest.surface->w});
        const INT32 bottom = std::min({region.y + region.h, d.y + d.h, dest.surface->h});
        region = SDL_Rect{left, top, right - left, bottom - top};
        if (region.w <= 0 || region.h <= 0)
            continue;
        SDL_Rect sample = s;
        if (dest.clipRegions)
        {
            const float scaleX = float(s.w) / (d.w);
            const float scaleY = float(s.h) / (d.h);
            const INT32 left = INT32((region.x - d.x) * scaleX);
            const INT32 top = INT32((region.y - d.y) * scaleY);
            sample.x += left;
            sample.y += top;
            sample.w -= INT32(((d.x + d.w) - (region.x + region.w)) * scaleX) + left;
            sample.h -= INT32(((d.y + d.h) - (region.y + region.h)) * scaleY) + top;
        }
        const auto mapping = dest.clipRegions ? region : d;
        for (int y = region.y; y < (region.y + region.h); ++y)
            for (int x = region.x; x < (region.x + region.w); ++x)
            {
                const int sx = sample.x + int(int64_t(x - mapping.x) *
                    (sample.w) / (mapping.w));
                const int sy = sample.y + int(int64_t(y - mapping.y) *
                    (sample.h) / (mapping.h));
                const UINT32 value = read(input.data() + sy * source.surface->pitch + sx * bytes, bytes);
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
    const std::array<std::array<UINT32, 5>, 7> formats{{
        {8, 0, 0, 0, 0}, {16, 0x7c00, 0x3e0, 0x1f, 0},
        {16, 0xf800, 0x7e0, 0x1f, 0}, {16, 0x1f, 0x3e0, 0x7c00, 0},
        {16, 0x3f, 0xfc0, 0xf000, 0},
        {32, 0xff0000, 0xff00, 0xff, 0xff000000}, {32, 0xff, 0xff00, 0xff0000, 0}
    }};
    for (const auto& f : formats)
    {
        auto src = CreateCpuSurface(7, 5, f[0], f[1], f[2], f[3], f[4]);
        auto dest = CreateCpuSurface(17, 13, f[0], f[1], f[2], f[3], f[4]);
        seed(*src, 11);
        if (f[0] == 8)
        {
            const SDL_Color colors[2]{{255, 1, 2, 255}, {3, 255, 4, 255}};
            REQUIRE(SDL_SetPaletteColors(SDL_GetSurfacePalette(src->surface.get()), colors, 0, 2));
            REQUIRE(SDL_GetSurfacePalette(src->surface.get())->colors[1].g == 255);
        }
        const auto input = pixels(*src);
        const unsigned bytes = f[0] / 8;
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
                        dest->destinationKey = SurfaceColorKey{high / 4, UINT32(high * uint64_t(3) / 4)};
                        const SDL_Rect d{origin[0], origin[1], width, height};
                        const SDL_Rect s{1, 0, 6, 5};
                        const auto expected = reference(*dest, d, *src, s, keys & 1, keys & 2);
                        BlitCpuSurface(*dest, &d, *src, &s, keys & 1, keys & 2);
                        const auto actual = pixels(*dest);
                        if (actual != expected)
                        {
                            const auto offset = std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin();
                            fprintf(stderr, "format %u/%x, clip %u, %dx%d, keys %u, byte %td: %02x != %02x\n",
                                    f[0], f[1], clipped, width, height, keys, offset, actual[offset], expected[offset]);
                            REQUIRE(actual == expected);
                        }
                    }
        }
        dest->clipRegions.reset();
        if (f[0] == 8)
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
    auto source = CreateCpuSurface(8, 7, 16, 0x7c00, 0x3e0, 0x1f);
    auto dest = CreateCpuSurface(8, 7, 16, 0x7c00, 0x3e0, 0x1f);
    std::unique_ptr<SGPVSurface> sourceView(CreateVideoSurfaceFromCpuSurface(source.get()));
    std::unique_ptr<SGPVSurface> destView(CreateVideoSurfaceFromCpuSurface(dest.get()));
    SDL_Rect destination{-2, -1, 6, 5}, sample{1, 1, 6, 5};
    REQUIRE(ClipReleatedSrcAndDestRectangles(destView.get(), sourceView.get(), &destination, &sample));
    const SDL_Rect expectedDest{0, 0, 4, 4}, expectedSource{3, 2, 4, 4};
    REQUIRE(SDL_RectsEqual(&destination, &expectedDest));
    REQUIRE(SDL_RectsEqual(&sample, &expectedSource));
    destination = {1, 1, 6, 5};
    sample = {-2, -1, 6, 5};
    REQUIRE(ClipReleatedSrcAndDestRectangles(destView.get(), sourceView.get(), &destination, &sample));
    REQUIRE(SDL_RectsEqual(&destination, &expectedSource));
    REQUIRE(SDL_RectsEqual(&sample, &expectedDest));
    SDL_Rect clips[]{{2, 1, 3, 2}};
    REQUIRE(SetClipList(destView.get(), clips, 1));
    REQUIRE(dest->clipRegions->size() == 1);
    REQUIRE(SDL_RectsEqual(&dest->clipRegions->front(), &clips[0]));
    seed(*source, 5);
    for (bool clipped : {false, true}) {
        if (!clipped) dest->clipRegions.reset();
        else REQUIRE(SetClipList(destView.get(), clips, 1));
        for (int y : {-4, -1, 0, 3, 7})
            for (int x : {-5, -1, 0, 4, 8}) {
                seed(*dest, 17);
                const SDL_Rect sample{1, 1, 6, 5}, mapping{x, y, 6, 5};
                const auto expected = reference(*dest, mapping, *source, sample);
                blt_vs_fx effects{};
                effects.SrcRect = sample;
                REQUIRE((BltVideoSurfaceToVideoSurface(destView.get(), sourceView.get(), 0,
                                                    x, y, VS_BLT_SRCSUBRECT, &effects) ||
                      x >= 8 || y >= 7));
                REQUIRE(pixels(*dest) == expected);
            }
    }
}

void locksFillsAndKeys()
{
    for (unsigned bits : {8, 16, 32})
    {
        auto surface = CreateCpuSurface(3, 3, bits, 0x7c00, 0x3e0, 0x1f);
        const unsigned bytes = bits / 8;
        auto lock = LockCpuSurface(*surface);
        REQUIRE((lock.pitch >= int(3 * bytes) && lock.pitch % 4 == 0));
        memset(lock.pixels, 0xa5, lock.pitch * lock.height);
        UnlockCpuSurface(*surface);
        const SDL_Rect region{1, 1, 2, 2};
        lock = LockCpuSurface(*surface, &region);
        REQUIRE(lock.pixels == static_cast<BYTE*>(surface->surface->pixels) + lock.pitch + bytes);
        REQUIRE((lock.width == 2 && lock.height == 2));
        bool rejected = false;
        try { FillCpuSurface(*surface, 0); } catch (const std::runtime_error&) { rejected = true; }
        REQUIRE(rejected);
        UnlockCpuSurface(*surface);
        const auto before = pixels(*surface);
        const SDL_Rect clips[]{{1, 0, 2, 2}};
        SetSurfaceClipRegions(*surface, clips);
        FillCpuSurface(*surface, 0xdeadbeef);
        const auto after = pixels(*surface);
        for (int y = 0; y < 3; ++y)
            for (int offset = 0; offset < lock.pitch; ++offset)
            {
                const auto index = y * lock.pitch + offset;
                if (y < 2 && offset >= int(bytes) && offset < int(3 * bytes))
                    REQUIRE(after[index] == BYTE(0xdeadbeef >> (8 * (offset % bytes))));
                else
                    REQUIRE(after[index] == before[index]);
            }
    }
    auto src = CreateCpuSurface(2, 1, 32, 0xff0000, 0xff00, 0xff, 0xff000000);
    auto dest = CreateCpuSurface(2, 1, 32, 0xff0000, 0xff00, 0xff, 0xff000000);
    const UINT32 input[] {0x00123456, 0xff123456};
    auto lock = LockCpuSurface(*src);
    memcpy(lock.pixels, input, sizeof(input));
    UnlockCpuSurface(*src);
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
    locksFillsAndKeys();
    videoSurfaceClipping();
}

#include "compat/surfaces.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (0)

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
std::vector<BYTE> reference(CpuSurface& dest, const SGPRect& d, CpuSurface& source, const SGPRect& s,
                            bool sourceKey = false, bool destinationKey = false)
{
    auto result = pixels(dest);
    const auto input = pixels(source);
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.surface->format);
    const auto regions = dest.clipRegions.value_or(std::vector<SGPRect>{d});
    for (SGPRect region : regions)
    {
        region.iLeft = std::max({region.iLeft, d.iLeft, 0});
        region.iTop = std::max({region.iTop, d.iTop, 0});
        region.iRight = std::min({region.iRight, d.iRight, dest.surface->w});
        region.iBottom = std::min({region.iBottom, d.iBottom, dest.surface->h});
        if (region.iLeft >= region.iRight || region.iTop >= region.iBottom)
            continue;
        SGPRect sample = s;
        if (dest.clipRegions)
        {
            const float scaleX = float(s.iRight - s.iLeft) / (d.iRight - d.iLeft);
            const float scaleY = float(s.iBottom - s.iTop) / (d.iBottom - d.iTop);
            sample.iLeft += INT32((region.iLeft - d.iLeft) * scaleX);
            sample.iTop += INT32((region.iTop - d.iTop) * scaleY);
            sample.iRight -= INT32((d.iRight - region.iRight) * scaleX);
            sample.iBottom -= INT32((d.iBottom - region.iBottom) * scaleY);
        }
        const auto mapping = dest.clipRegions ? region : d;
        for (int y = region.iTop; y < region.iBottom; ++y)
            for (int x = region.iLeft; x < region.iRight; ++x)
            {
                const int sx = sample.iLeft + int(int64_t(x - mapping.iLeft) *
                    (sample.iRight - sample.iLeft) / (mapping.iRight - mapping.iLeft));
                const int sy = sample.iTop + int(int64_t(y - mapping.iTop) *
                    (sample.iBottom - sample.iTop) / (mapping.iBottom - mapping.iTop));
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
            CHECK(SDL_SetPaletteColors(SDL_GetSurfacePalette(src->surface.get()), colors, 0, 2));
            CHECK(SDL_GetSurfacePalette(src->surface.get())->colors[1].g == 255);
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
                const SGPRect clips[]{{0, 0, 9, 7}, {4, 3, 15, 12}, {0, 0, 9, 7},
                                   {14, 0, 17, 3}, {8, 9, 8, 12}};
                SetSurfaceClipRegions(*dest, clips);
                CHECK(dest->clipRegions->size() == 4);
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
                        const SGPRect d{origin[0], origin[1], width + origin[0], height + origin[1]};
                        const SGPRect s{1, 0, 7, 5};
                        const auto expected = reference(*dest, d, *src, s, keys & 1, keys & 2);
                        BlitCpuSurface(*dest, &d, *src, &s, keys & 1, keys & 2);
                        const auto actual = pixels(*dest);
                        if (actual != expected)
                        {
                            const auto offset = std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin();
                            fprintf(stderr, "format %u/%x, clip %u, %dx%d, keys %u, byte %td: %02x != %02x\n",
                                    f[0], f[1], clipped, width, height, keys, offset, actual[offset], expected[offset]);
                            CHECK(actual == expected);
                        }
                    }
        }
        dest->clipRegions.reset();
        if (f[0] == 8)
        {
            std::array<SDL_Color, 256> white;
            white.fill({255, 255, 255, 255});
            CHECK(SDL_SetPaletteColors(SDL_GetSurfacePalette(dest->surface.get()), white.data(), 0, 256));
        }
        seed(*dest, 17);
        const SGPRect s{0, 0, 12, 10}, d{3, 2, 15, 12};
        const auto expected = reference(*dest, d, *dest, s);
        BlitCpuSurface(*dest, &d, *dest, &s);
        CHECK(pixels(*dest) == expected);
    }
}

void locksFillsAndKeys()
{
    for (unsigned bits : {8, 16, 32})
    {
        auto surface = CreateCpuSurface(3, 3, bits, 0x7c00, 0x3e0, 0x1f);
        const unsigned bytes = bits / 8;
        auto lock = LockCpuSurface(*surface);
        CHECK(lock.pitch >= int(3 * bytes) && lock.pitch % 4 == 0);
        memset(lock.pixels, 0xa5, lock.pitch * lock.height);
        UnlockCpuSurface(*surface);
        const SGPRect region{1, 1, 3, 3};
        lock = LockCpuSurface(*surface, &region);
        CHECK(lock.pixels == static_cast<BYTE*>(surface->surface->pixels) + lock.pitch + bytes);
        CHECK(lock.width == 2 && lock.height == 2);
        bool rejected = false;
        try { FillCpuSurface(*surface, 0); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
        UnlockCpuSurface(*surface);
        const auto before = pixels(*surface);
        const SGPRect clips[]{{1, 0, 3, 2}};
        SetSurfaceClipRegions(*surface, clips);
        FillCpuSurface(*surface, 0xdeadbeef);
        const auto after = pixels(*surface);
        for (int y = 0; y < 3; ++y)
            for (int offset = 0; offset < lock.pitch; ++offset)
            {
                const auto index = y * lock.pitch + offset;
                if (y < 2 && offset >= int(bytes) && offset < int(3 * bytes))
                    CHECK(after[index] == BYTE(0xdeadbeef >> (8 * (offset % bytes))));
                else
                    CHECK(after[index] == before[index]);
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
    CHECK(read(result.data(), 4) == 0x11223344 && read(result.data() + 4, 4) == input[1]);
}
} // namespace

int main()
{
    try
    {
        differential();
        locksFillsAndKeys();
        puts("CPU surfaces: exact packed differential, palette, pitch, alpha, clips, keys and overlap passed");
        return 0;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "surfaces: %s\n", failure.what());
        return 1;
    }
}

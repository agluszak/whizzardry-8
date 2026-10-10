#include "compat/surfaces.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
void require(bool condition, const char* operation)
{
    if (!condition)
        throw std::runtime_error(operation);
}
void sdl(bool result)
{
    if (!result)
        throw std::runtime_error(SDL_GetError());
}
RECT bounds(const SDL_Surface& surface)
{
    return {0, 0, surface.w, surface.h};
}
SDL_Rect rectangle(const RECT& rect)
{
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}
bool valid(const RECT& rect, const SDL_Surface& surface)
{
    return rect.left >= 0 && rect.top >= 0 && rect.right > rect.left && rect.bottom > rect.top &&
           rect.right <= surface.w && rect.bottom <= surface.h;
}
UINT32 pixel(const void* source, unsigned bytes)
{
    UINT32 value = 0;
    memcpy(&value, source, bytes);
    return value;
}
bool inKey(UINT32 value, const SurfaceColorKey& key)
{
    return value >= key.low && value <= key.high;
}
RECT intersection(RECT region, const RECT& dest, const SDL_Surface& surface)
{
    region.left = std::max(region.left, std::max<LONG>(dest.left, 0));
    region.top = std::max(region.top, std::max<LONG>(dest.top, 0));
    region.right = std::min(region.right, std::min<LONG>(dest.right, surface.w));
    region.bottom = std::min(region.bottom, std::min<LONG>(dest.bottom, surface.h));
    return region;
}

// Range/destination keys and edge-sampled scaling have no equivalent SDL operation.
void packedBlit(SDL_Surface& dest, const RECT& region, const RECT& mapping,
                const SDL_Surface& source, const RECT& sample,
                const SurfaceColorKey* sourceKey, const SurfaceColorKey* destinationKey)
{
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.format);
    for (int y = region.top; y < region.bottom; ++y)
    {
        const int sy = sample.top + int(int64_t(y - mapping.top) * (sample.bottom - sample.top) /
                                       (mapping.bottom - mapping.top));
        for (int x = region.left; x < region.right; ++x)
        {
            const int sx = sample.left + int(int64_t(x - mapping.left) * (sample.right - sample.left) /
                                            (mapping.right - mapping.left));
            auto* output = static_cast<BYTE*>(dest.pixels) + size_t(y) * dest.pitch + x * bytes;
            const UINT32 value = pixel(static_cast<const BYTE*>(source.pixels) +
                                       size_t(sy) * source.pitch + sx * bytes, bytes);
            if ((sourceKey && inKey(value, *sourceKey)) ||
                (destinationKey && !inKey(pixel(output, bytes), *destinationKey)))
                continue;
            memcpy(output, &value, bytes);
        }
    }
}
} // namespace

std::unique_ptr<CpuSurface> CreateCpuSurface(UINT16 width, UINT16 height, UINT8 bits,
    UINT32 red, UINT32 green, UINT32 blue, UINT32 alpha)
{
    require(width && height && (bits == 8 || bits == 16 || bits == 32),
            "CPU surface dimensions/format");
    auto result = std::make_unique<CpuSurface>();
    auto format = bits == 8 ? SDL_PIXELFORMAT_INDEX8 :
        SDL_GetPixelFormatForMasks(bits, red, green, blue, alpha);
    // Unknown packed masks still need raw storage, never SDL color conversion.
    if (format == SDL_PIXELFORMAT_UNKNOWN)
        format = bits == 16 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888;
    result->surface.reset(SDL_CreateSurface(width, height, format));
    sdl(bool(result->surface));
    sdl(SDL_SetSurfaceBlendMode(result->surface.get(), SDL_BLENDMODE_NONE));
    if (bits == 8)
    {
        auto* palette = SDL_CreateSurfacePalette(result->surface.get());
        sdl(palette != nullptr);
        SDL_Color colors[256]{};
        for (auto& color : colors)
            color.a = SDL_ALPHA_OPAQUE;
        sdl(SDL_SetPaletteColors(palette, colors, 0, 256));
    }
    result->redMask = red;
    result->greenMask = green;
    result->blueMask = blue;
    result->alphaMask = alpha;
    return result;
}

SurfaceLock LockCpuSurface(CpuSurface& owner, const RECT* rect)
{
    auto& surface = *owner.surface;
    const RECT region = rect ? *rect : bounds(surface);
    require(!owner.locked && valid(region, surface), "CPU surface lock state/rectangle");
    sdl(SDL_LockSurface(&surface));
    owner.locked = true;
    return {static_cast<BYTE*>(surface.pixels) + size_t(region.top) * surface.pitch +
            region.left * SDL_BYTESPERPIXEL(surface.format), surface.pitch,
            region.right - region.left, region.bottom - region.top};
}
void UnlockCpuSurface(CpuSurface& surface)
{
    require(surface.locked, "CPU surface unlock state");
    SDL_UnlockSurface(surface.surface.get());
    surface.locked = false;
}

void FillCpuSurface(CpuSurface& owner, UINT32 color, const RECT* rect)
{
    require(!owner.locked, "CPU surface fill while locked");
    auto& dest = *owner.surface;
    const RECT d = rect ? *rect : bounds(dest);
    require(d.right > d.left && d.bottom > d.top, "CPU surface fill rectangle");
    const auto fill = rectangle(d);
    for (const RECT& clip : owner.clipRegions ? std::span<const RECT>(*owner.clipRegions) :
                                              std::span<const RECT>(&d, 1))
    {
        const RECT region = intersection(clip, d, dest);
        if (region.left >= region.right || region.top >= region.bottom)
            continue;
        const auto limit = rectangle(region);
        SDL_SetSurfaceClipRect(&dest, &limit);
        sdl(SDL_FillSurfaceRect(&dest, &fill, color));
    }
    SDL_SetSurfaceClipRect(&dest, nullptr);
}

void BlitCpuSurface(CpuSurface& output, const RECT* destRect, CpuSurface& input,
                   const RECT* sourceRect, bool useSourceKey, bool useDestinationKey)
{
    auto& dest = *output.surface;
    auto& source = *input.surface;
    require(!output.locked && !input.locked &&
            SDL_BYTESPERPIXEL(dest.format) == SDL_BYTESPERPIXEL(source.format),
            "CPU surface blit state/format");
    const RECT d = destRect ? *destRect : bounds(dest);
    const RECT s = sourceRect ? *sourceRect : bounds(source);
    require(d.right > d.left && d.bottom > d.top && valid(s, source),
            "CPU surface blit rectangles");
    require(!useSourceKey || input.sourceKey.has_value(), "source color key missing");
    require(!useDestinationKey || output.destinationKey.has_value(), "destination color key missing");
    require(!useSourceKey || input.sourceKey->low <= input.sourceKey->high, "source color key range");
    require(!useDestinationKey || output.destinationKey->low <= output.destinationKey->high,
            "destination color key range");
    const auto* sourceKey = useSourceKey ? &*input.sourceKey : nullptr;
    const auto* destinationKey = useDestinationKey ? &*output.destinationKey : nullptr;
    SDLSurfaceOwner snapshot{nullptr, SDL_DestroySurface};
    SDL_Surface* data = &source;
    if (&output == &input)
    {
        snapshot.reset(SDL_CreateSurface(source.w, source.h, source.format));
        sdl(bool(snapshot));
        // DuplicateSurface converts indexed colors and rejects an all-white palette.
        // The snapshot must retain indices, alpha and every unused packed bit.
        for (int y = 0; y < source.h; ++y)
            memcpy(static_cast<BYTE*>(snapshot->pixels) + y * snapshot->pitch,
                   static_cast<const BYTE*>(source.pixels) + y * source.pitch,
                   source.w * SDL_BYTESPERPIXEL(source.format));
        data = snapshot.get();
    }
    // An unowned SDL view copies packed words, not palette/mask-remapped colors.
    SDLSurfaceOwner view{SDL_CreateSurfaceFrom(data->w, data->h, dest.format,
                                               data->pixels, data->pitch), SDL_DestroySurface};
    sdl(bool(view));
    sdl(SDL_SetSurfaceBlendMode(view.get(), SDL_BLENDMODE_NONE));
    if (SDL_ISPIXELFORMAT_INDEXED(dest.format))
        sdl(SDL_SetSurfacePalette(view.get(), SDL_GetSurfacePalette(&dest)));
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.format);
    const UINT32 wordMask = bytes == 4 ? UINT32_MAX : (1u << (bytes * 8)) - 1;
    const bool unusedBits = !SDL_ISPIXELFORMAT_INDEXED(dest.format) &&
        (output.redMask | output.greenMask | output.blueMask | output.alphaMask) != wordMask;
    const bool rawKey = sourceKey && (sourceKey->low != sourceKey->high ||
                                     SDL_ISPIXELFORMAT_ALPHA(dest.format) || unusedBits);
    if (sourceKey && !rawKey)
        sdl(SDL_SetSurfaceColorKey(view.get(), true, sourceKey->low));
    for (const RECT& clip : output.clipRegions ? std::span<const RECT>(*output.clipRegions) :
                                               std::span<const RECT>(&d, 1))
    {
        const RECT region = intersection(clip, d, dest);
        if (region.left >= region.right || region.top >= region.bottom)
            continue;
        RECT sample = s;
        if (output.clipRegions)
        {
            const float scaleX = float(s.right - s.left) / float(d.right - d.left);
            const float scaleY = float(s.bottom - s.top) / float(d.bottom - d.top);
            sample.left += LONG((region.left - d.left) * scaleX);
            sample.top += LONG((region.top - d.top) * scaleY);
            sample.right -= LONG((d.right - region.right) * scaleX);
            sample.bottom -= LONG((d.bottom - region.bottom) * scaleY);
        }
        const RECT mapping = output.clipRegions ? region : d;
        const auto target = rectangle(mapping);
        const auto from = rectangle(sample);
        const auto limit = rectangle(region);
        const bool integralScale = target.w % from.w == 0 && target.h % from.h == 0;
        const bool scaledPacked = (SDL_ISPIXELFORMAT_INDEXED(dest.format) || unusedBits) &&
                                  (target.w != from.w || target.h != from.h);
        const bool clippedScale = (target.w != from.w || target.h != from.h) &&
            (mapping.left != region.left || mapping.top != region.top ||
             mapping.right != region.right || mapping.bottom != region.bottom);
        if (destinationKey || rawKey || !integralScale || scaledPacked || clippedScale)
        {
            packedBlit(dest, region, mapping, *data, sample, sourceKey, destinationKey);
            continue;
        }
        SDL_SetSurfaceClipRect(&dest, &limit);
        if (target.w == from.w && target.h == from.h)
            sdl(SDL_BlitSurface(view.get(), &from, &dest, &target));
        else
            sdl(SDL_BlitSurfaceScaled(view.get(), &from, &dest, &target, SDL_SCALEMODE_NEAREST));
    }
    SDL_SetSurfaceClipRect(&dest, nullptr);
}

void SetSurfaceClipRegions(CpuSurface& surface, std::span<const RECT> input)
{
    std::vector<LONG> edges;
    for (const RECT& rect : input)
    {
        if (rect.left >= rect.right || rect.top >= rect.bottom)
            continue;
        edges.push_back(rect.top);
        edges.push_back(rect.bottom);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    surface.clipRegions.emplace();
    auto& rectangles = *surface.clipRegions;
    std::vector<std::pair<LONG, LONG>> previous;
    size_t previousStart = 0;
    for (size_t i = 1; i < edges.size(); ++i)
    {
        std::vector<std::pair<LONG, LONG>> spans, merged;
        for (const RECT& rect : input)
            if (rect.top <= edges[i - 1] && rect.bottom >= edges[i] && rect.left < rect.right)
                spans.emplace_back(rect.left, rect.right);
        std::sort(spans.begin(), spans.end());
        for (auto span : spans)
        {
            if (!merged.empty() && span.first <= merged.back().second)
                merged.back().second = std::max(merged.back().second, span.second);
            else
                merged.push_back(span);
        }
        if (merged == previous)
        {
            for (size_t j = previousStart; j < rectangles.size(); ++j)
                rectangles[j].bottom = edges[i];
        }
        else
        {
            previousStart = rectangles.size();
            for (auto span : merged)
                rectangles.push_back({span.first, edges[i - 1], span.second, edges[i]});
        }
        previous = merged;
    }
}

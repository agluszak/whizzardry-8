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
SDL_Rect bounds(const SDL_Surface& surface)
{
    return {0, 0, surface.w, surface.h};
}
bool valid(const SDL_Rect& rect, const SDL_Surface& surface)
{
    SDL_Rect result;
    const SDL_Rect limit = bounds(surface);
    return SDL_GetRectIntersection(&rect, &limit, &result) && SDL_RectsEqual(&result, &rect);
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
bool intersection(const SDL_Rect& clip, const SDL_Rect& dest,
                  const SDL_Surface& surface, SDL_Rect* region)
{
    const SDL_Rect limit = bounds(surface);
    return SDL_GetRectIntersection(&clip, &dest, region) &&
           SDL_GetRectIntersection(region, &limit, region);
}

// Range/destination keys and edge-sampled scaling have no equivalent SDL operation.
void packedBlit(SDL_Surface& dest, const SDL_Rect& region, const SDL_Rect& mapping,
                const SDL_Surface& source, const SDL_Rect& sample,
                const SurfaceColorKey* sourceKey, const SurfaceColorKey* destinationKey)
{
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.format);
    for (int y = region.y; y < region.y + region.h; ++y)
    {
        const int sy = sample.y + int(int64_t(y - mapping.y) * sample.h / mapping.h);
        for (int x = region.x; x < region.x + region.w; ++x)
        {
            const int sx = sample.x + int(int64_t(x - mapping.x) * sample.w / mapping.w);
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

SurfaceLock LockCpuSurface(CpuSurface& owner, const SDL_Rect* rect)
{
    auto& surface = *owner.surface;
    const SDL_Rect region = rect ? *rect : bounds(surface);
    require(!owner.locked && valid(region, surface), "CPU surface lock state/rectangle");
    sdl(SDL_LockSurface(&surface));
    owner.locked = true;
    return {static_cast<BYTE*>(surface.pixels) + size_t(region.y) * surface.pitch +
            region.x * SDL_BYTESPERPIXEL(surface.format), surface.pitch,
            region.w, region.h};
}
void UnlockCpuSurface(CpuSurface& surface)
{
    require(surface.locked, "CPU surface unlock state");
    SDL_UnlockSurface(surface.surface.get());
    surface.locked = false;
}

void FillCpuSurface(CpuSurface& owner, UINT32 color, const SDL_Rect* rect)
{
    require(!owner.locked, "CPU surface fill while locked");
    auto& dest = *owner.surface;
    const SDL_Rect d = rect ? *rect : bounds(dest);
    require(!SDL_RectEmpty(&d), "CPU surface fill rectangle");
    for (const SDL_Rect& clip : owner.clipRegions ? std::span<const SDL_Rect>(*owner.clipRegions) :
                                              std::span<const SDL_Rect>(&d, 1))
    {
        SDL_Rect region;
        if (!intersection(clip, d, dest, &region))
            continue;
        SDL_SetSurfaceClipRect(&dest, &region);
        sdl(SDL_FillSurfaceRect(&dest, &d, color));
    }
    SDL_SetSurfaceClipRect(&dest, nullptr);
}

void BlitCpuSurface(CpuSurface& output, const SDL_Rect* destRect, CpuSurface& input,
                   const SDL_Rect* sourceRect, bool useSourceKey, bool useDestinationKey)
{
    auto& dest = *output.surface;
    auto& source = *input.surface;
    require(!output.locked && !input.locked &&
            SDL_BYTESPERPIXEL(dest.format) == SDL_BYTESPERPIXEL(source.format),
            "CPU surface blit state/format");
    const SDL_Rect d = destRect ? *destRect : bounds(dest);
    const SDL_Rect s = sourceRect ? *sourceRect : bounds(source);
    require(!SDL_RectEmpty(&d) && valid(s, source),
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
    for (const SDL_Rect& clip : output.clipRegions ? std::span<const SDL_Rect>(*output.clipRegions) :
                                               std::span<const SDL_Rect>(&d, 1))
    {
        SDL_Rect region;
        if (!intersection(clip, d, dest, &region))
            continue;
        SDL_Rect sample = s;
        if (output.clipRegions)
        {
            const float scaleX = float(s.w) / float(d.w);
            const float scaleY = float(s.h) / float(d.h);
            const INT32 left = INT32((region.x - d.x) * scaleX);
            const INT32 top = INT32((region.y - d.y) * scaleY);
            sample.x += left;
            sample.y += top;
            sample.w -= INT32(((d.x + d.w) - (region.x + region.w)) * scaleX) + left;
            sample.h -= INT32(((d.y + d.h) - (region.y + region.h)) * scaleY) + top;
        }
        const SDL_Rect mapping = output.clipRegions ? region : d;
        const bool scaled = mapping.w != sample.w || mapping.h != sample.h;
        const bool integralScale = mapping.w % sample.w == 0 && mapping.h % sample.h == 0;
        const bool scaledPacked = (SDL_ISPIXELFORMAT_INDEXED(dest.format) || unusedBits) && scaled;
        const bool clippedScale = scaled && !SDL_RectsEqual(&mapping, &region);
        if (destinationKey || rawKey || !integralScale || scaledPacked || clippedScale)
        {
            packedBlit(dest, region, mapping, *data, sample, sourceKey, destinationKey);
            continue;
        }
        SDL_SetSurfaceClipRect(&dest, &region);
        if (!scaled)
            sdl(SDL_BlitSurface(view.get(), &sample, &dest, &mapping));
        else
            sdl(SDL_BlitSurfaceScaled(view.get(), &sample, &dest, &mapping, SDL_SCALEMODE_NEAREST));
    }
    SDL_SetSurfaceClipRect(&dest, nullptr);
}

void SetSurfaceClipRegions(CpuSurface& surface, std::span<const SDL_Rect> input)
{
    std::vector<INT32> edges;
    for (const SDL_Rect& rect : input)
    {
        if (SDL_RectEmpty(&rect))
            continue;
        edges.push_back(rect.y);
        edges.push_back(rect.y + rect.h);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    surface.clipRegions.emplace();
    auto& rectangles = *surface.clipRegions;
    std::vector<std::pair<INT32, INT32>> previous;
    size_t previousStart = 0;
    for (size_t i = 1; i < edges.size(); ++i)
    {
        std::vector<std::pair<INT32, INT32>> spans, merged;
        for (const SDL_Rect& rect : input)
            if (rect.y <= edges[i - 1] && (rect.y + rect.h) >= edges[i] && rect.w > 0)
                spans.emplace_back(rect.x, (rect.x + rect.w));
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
                rectangles[j].h = (edges[i]) - rectangles[j].y;
        }
        else
        {
            previousStart = rectangles.size();
            for (auto span : merged)
                rectangles.push_back(
                    {span.first, edges[i - 1], span.second - span.first, edges[i] - edges[i - 1]});
        }
        previous = merged;
    }
}

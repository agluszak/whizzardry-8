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
SGPRect bounds(const SDL_Surface& surface)
{
    return {0, 0, surface.w, surface.h};
}
SDL_Rect rectangle(const SGPRect& rect)
{
    return {rect.iLeft, rect.iTop, rect.iRight - rect.iLeft, rect.iBottom - rect.iTop};
}
bool valid(const SGPRect& rect, const SDL_Surface& surface)
{
    return rect.iLeft >= 0 && rect.iTop >= 0 && rect.iRight > rect.iLeft && rect.iBottom > rect.iTop &&
           rect.iRight <= surface.w && rect.iBottom <= surface.h;
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
SGPRect intersection(SGPRect region, const SGPRect& dest, const SDL_Surface& surface)
{
    region.iLeft = std::max(region.iLeft, std::max<INT32>(dest.iLeft, 0));
    region.iTop = std::max(region.iTop, std::max<INT32>(dest.iTop, 0));
    region.iRight = std::min(region.iRight, std::min<INT32>(dest.iRight, surface.w));
    region.iBottom = std::min(region.iBottom, std::min<INT32>(dest.iBottom, surface.h));
    return region;
}

// Range/destination keys and edge-sampled scaling have no equivalent SDL operation.
void packedBlit(SDL_Surface& dest, const SGPRect& region, const SGPRect& mapping,
                const SDL_Surface& source, const SGPRect& sample,
                const SurfaceColorKey* sourceKey, const SurfaceColorKey* destinationKey)
{
    const unsigned bytes = SDL_BYTESPERPIXEL(dest.format);
    for (int y = region.iTop; y < region.iBottom; ++y)
    {
        const int sy = sample.iTop + int(int64_t(y - mapping.iTop) * (sample.iBottom - sample.iTop) /
                                       (mapping.iBottom - mapping.iTop));
        for (int x = region.iLeft; x < region.iRight; ++x)
        {
            const int sx = sample.iLeft + int(int64_t(x - mapping.iLeft) * (sample.iRight - sample.iLeft) /
                                            (mapping.iRight - mapping.iLeft));
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

SurfaceLock LockCpuSurface(CpuSurface& owner, const SGPRect* rect)
{
    auto& surface = *owner.surface;
    const SGPRect region = rect ? *rect : bounds(surface);
    require(!owner.locked && valid(region, surface), "CPU surface lock state/rectangle");
    sdl(SDL_LockSurface(&surface));
    owner.locked = true;
    return {static_cast<BYTE*>(surface.pixels) + size_t(region.iTop) * surface.pitch +
            region.iLeft * SDL_BYTESPERPIXEL(surface.format), surface.pitch,
            region.iRight - region.iLeft, region.iBottom - region.iTop};
}
void UnlockCpuSurface(CpuSurface& surface)
{
    require(surface.locked, "CPU surface unlock state");
    SDL_UnlockSurface(surface.surface.get());
    surface.locked = false;
}

void FillCpuSurface(CpuSurface& owner, UINT32 color, const SGPRect* rect)
{
    require(!owner.locked, "CPU surface fill while locked");
    auto& dest = *owner.surface;
    const SGPRect d = rect ? *rect : bounds(dest);
    require(d.iRight > d.iLeft && d.iBottom > d.iTop, "CPU surface fill rectangle");
    const auto fill = rectangle(d);
    for (const SGPRect& clip : owner.clipRegions ? std::span<const SGPRect>(*owner.clipRegions) :
                                              std::span<const SGPRect>(&d, 1))
    {
        const SGPRect region = intersection(clip, d, dest);
        if (region.iLeft >= region.iRight || region.iTop >= region.iBottom)
            continue;
        const auto limit = rectangle(region);
        SDL_SetSurfaceClipRect(&dest, &limit);
        sdl(SDL_FillSurfaceRect(&dest, &fill, color));
    }
    SDL_SetSurfaceClipRect(&dest, nullptr);
}

void BlitCpuSurface(CpuSurface& output, const SGPRect* destRect, CpuSurface& input,
                   const SGPRect* sourceRect, bool useSourceKey, bool useDestinationKey)
{
    auto& dest = *output.surface;
    auto& source = *input.surface;
    require(!output.locked && !input.locked &&
            SDL_BYTESPERPIXEL(dest.format) == SDL_BYTESPERPIXEL(source.format),
            "CPU surface blit state/format");
    const SGPRect d = destRect ? *destRect : bounds(dest);
    const SGPRect s = sourceRect ? *sourceRect : bounds(source);
    require(d.iRight > d.iLeft && d.iBottom > d.iTop && valid(s, source),
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
    for (const SGPRect& clip : output.clipRegions ? std::span<const SGPRect>(*output.clipRegions) :
                                               std::span<const SGPRect>(&d, 1))
    {
        const SGPRect region = intersection(clip, d, dest);
        if (region.iLeft >= region.iRight || region.iTop >= region.iBottom)
            continue;
        SGPRect sample = s;
        if (output.clipRegions)
        {
            const float scaleX = float(s.iRight - s.iLeft) / float(d.iRight - d.iLeft);
            const float scaleY = float(s.iBottom - s.iTop) / float(d.iBottom - d.iTop);
            sample.iLeft += INT32((region.iLeft - d.iLeft) * scaleX);
            sample.iTop += INT32((region.iTop - d.iTop) * scaleY);
            sample.iRight -= INT32((d.iRight - region.iRight) * scaleX);
            sample.iBottom -= INT32((d.iBottom - region.iBottom) * scaleY);
        }
        const SGPRect mapping = output.clipRegions ? region : d;
        const auto target = rectangle(mapping);
        const auto from = rectangle(sample);
        const auto limit = rectangle(region);
        const bool integralScale = target.w % from.w == 0 && target.h % from.h == 0;
        const bool scaledPacked = (SDL_ISPIXELFORMAT_INDEXED(dest.format) || unusedBits) &&
                                  (target.w != from.w || target.h != from.h);
        const bool clippedScale = (target.w != from.w || target.h != from.h) &&
            (mapping.iLeft != region.iLeft || mapping.iTop != region.iTop ||
             mapping.iRight != region.iRight || mapping.iBottom != region.iBottom);
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

void SetSurfaceClipRegions(CpuSurface& surface, std::span<const SGPRect> input)
{
    std::vector<INT32> edges;
    for (const SGPRect& rect : input)
    {
        if (rect.iLeft >= rect.iRight || rect.iTop >= rect.iBottom)
            continue;
        edges.push_back(rect.iTop);
        edges.push_back(rect.iBottom);
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
        for (const SGPRect& rect : input)
            if (rect.iTop <= edges[i - 1] && rect.iBottom >= edges[i] && rect.iLeft < rect.iRight)
                spans.emplace_back(rect.iLeft, rect.iRight);
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
                rectangles[j].iBottom = edges[i];
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

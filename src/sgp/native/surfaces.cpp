#include "compat/surfaces.h"

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <limits>
#include <utility>
#include <vector>

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
uint32_t pixel(const void* source, unsigned bytes)
{
    uint32_t value = 0;
    memcpy(&value, source, bytes);
    return value;
}
bool inKey(uint32_t value, const SurfaceColorKey& key)
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
            auto* output = static_cast<uint8_t*>(dest.pixels) + size_t(y) * dest.pitch + x * bytes;
            const uint32_t value = pixel(static_cast<const uint8_t*>(source.pixels) +
                                       size_t(sy) * source.pitch + sx * bytes, bytes);
            if ((sourceKey && inKey(value, *sourceKey)) ||
                (destinationKey && !inKey(pixel(output, bytes), *destinationKey)))
                continue;
            memcpy(output, &value, bytes);
        }
    }
}
} // namespace

std::unique_ptr<CpuSurface> CreateCpuSurface(uint16_t width, uint16_t height, uint8_t bits,
    uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
    require(width && height && (bits == 8 || bits == 16 || bits == 24 || bits == 32),
            "CPU surface dimensions/format");
    auto result = std::make_unique<CpuSurface>();
    auto format = bits == 8 ? SDL_PIXELFORMAT_INDEX8 :
        SDL_GetPixelFormatForMasks(bits, red, green, blue, alpha);
    // Unknown packed masks still need raw storage, never SDL color conversion.
    if (format == SDL_PIXELFORMAT_UNKNOWN)
        format = bits == 16 ? SDL_PIXELFORMAT_RGB565 :
                 bits == 24 ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_ARGB8888;
    result->surface = CreateCpuStorage(width, height, format);
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

void FillCpuSurface(CpuSurface& owner, uint32_t color, const SDL_Rect* rect)
{
    require(!(owner.surface->flags & SDL_SURFACE_LOCKED),
            "CPU surface fill while locked");
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
    require(!(dest.flags & SDL_SURFACE_LOCKED) && !(source.flags & SDL_SURFACE_LOCKED) &&
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
    const auto sourceBegin = reinterpret_cast<uintptr_t>(source.pixels);
    const auto destBegin = reinterpret_cast<uintptr_t>(dest.pixels);
    if (sourceBegin < destBegin + size_t(dest.pitch) * dest.h &&
        destBegin < sourceBegin + size_t(source.pitch) * source.h)
    {
        snapshot.reset(SDL_CreateSurface(source.w, source.h, source.format));
        sdl(bool(snapshot));
        // DuplicateSurface converts indexed colors and rejects an all-white palette.
        // The snapshot must retain indices, alpha and every unused packed bit.
        for (int y = 0; y < source.h; ++y)
            memcpy(static_cast<uint8_t*>(snapshot->pixels) + y * snapshot->pitch,
                   static_cast<const uint8_t*>(source.pixels) + y * source.pitch,
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
    const uint32_t wordMask = bytes == 4 ? UINT32_MAX : (1u << (bytes * 8)) - 1;
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
            const int32_t left = int32_t((region.x - d.x) * scaleX);
            const int32_t top = int32_t((region.y - d.y) * scaleY);
            sample.x += left;
            sample.y += top;
            sample.w -= int32_t(((d.x + d.w) - (region.x + region.w)) * scaleX) + left;
            sample.h -= int32_t(((d.y + d.h) - (region.y + region.h)) * scaleY) + top;
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
    std::vector<int32_t> edges;
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
    std::vector<std::pair<int32_t, int32_t>> previous;
    size_t previousStart = 0;
    for (size_t i = 1; i < edges.size(); ++i)
    {
        std::vector<std::pair<int32_t, int32_t>> spans, merged;
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

CpuSurface ShareCpuSurface(SDL_Surface& surface)
{
    require(!SDL_MUSTLOCK(&surface) && !(surface.flags & SDL_SURFACE_LOCKED),
            "CPU storage must allow direct pixel access");
    CpuSurface result;
    ++surface.refcount;
    result.surface.reset(&surface);
    int bits;
    SDL_GetMasksForPixelFormat(surface.format, &bits, &result.redMask, &result.greenMask,
                              &result.blueMask, &result.alphaMask);
    return result;
}

void ConvertCpuPixels(void* destination, const void* source, size_t count,
                      SDL_PixelFormat sourceFormat, SDL_PixelFormat destinationFormat)
{
    if (!count)
        return;
    const unsigned sourceBytes = SDL_BYTESPERPIXEL(sourceFormat);
    const unsigned destinationBytes = SDL_BYTESPERPIXEL(destinationFormat);
    require(sourceBytes > 0 && sourceBytes <= 4 && destinationBytes > 0 && destinationBytes <= 4 &&
            !SDL_ISPIXELFORMAT_FOURCC(sourceFormat) && !SDL_ISPIXELFORMAT_FOURCC(destinationFormat),
            "CPU pixel conversion format");
    if (count > size_t(std::numeric_limits<int>::max() / 4))
        throw std::length_error("Pixel conversion row is too large");
    const int sourcePitch = int(count) * SDL_BYTESPERPIXEL(sourceFormat);
    const int destinationPitch = int(count) * SDL_BYTESPERPIXEL(destinationFormat);
    std::vector<unsigned char> snapshot;
    const auto src = reinterpret_cast<uintptr_t>(source);
    const auto dst = reinterpret_cast<uintptr_t>(destination);
    if (dst < src + sourcePitch && src < dst + destinationPitch) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        snapshot.assign(bytes, bytes + sourcePitch);
        source = snapshot.data();
    }
    sdl(SDL_ConvertPixels(int(count), 1, sourceFormat, source, sourcePitch,
                         destinationFormat, destination, destinationPitch));
}

SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format,
                                 void* pixels, int pitch)
{
    SDLSurfaceOwner result{pitch >= 0 ? SDL_CreateSurfaceFrom(width, height, format, pixels, pitch) :
                                   SDL_CreateSurface(width, height, format), SDL_DestroySurface};
    sdl(bool(result));
    sdl(SDL_SetSurfaceBlendMode(result.get(), SDL_BLENDMODE_NONE));
    if (SDL_ISPIXELFORMAT_INDEXED(format))
        sdl(SDL_CreateSurfacePalette(result.get()) != nullptr);
    return result;
}

SDLSurfaceOwner CopyCpuStorage(SDL_Surface& source)
{
    auto result = CreateCpuStorage(source.w, source.h, source.format);
    if (auto* palette = SDL_GetSurfacePalette(&source))
        sdl(SDL_SetPaletteColors(SDL_GetSurfacePalette(result.get()), palette->colors,
                                0, palette->ncolors));
    auto destination = ShareCpuSurface(*result);
    auto input = ShareCpuSurface(source);
    BlitCpuSurface(destination, nullptr, input);
    return result;
}

void FlipCpuSurface(CpuSurface& owner, SDL_FlipMode flip, const SDL_Rect* rectangle)
{
    auto& surface = *owner.surface;
    const SDL_Rect region = rectangle ? *rectangle : bounds(surface);
    require(!(surface.flags & SDL_SURFACE_LOCKED) && valid(region, surface),
            "CPU surface flip state/rectangle");
    auto* pixels = static_cast<uint8_t*>(surface.pixels) + size_t(region.y) * surface.pitch +
                   region.x * SDL_BYTESPERPIXEL(surface.format);
    auto view = CreateCpuStorage(region.w, region.h, surface.format, pixels, surface.pitch);
    // SDL's vertical flip swaps complete pitches. A subrectangle therefore needs
    // isolated storage so pixels to either side of the region stay untouched.
    auto storage = CopyCpuStorage(*view);
    sdl(SDL_FlipSurface(storage.get(), flip));
    auto flipped = ShareCpuSurface(*storage);
    BlitCpuSurface(owner, &region, flipped);
}

bool ClipCpuBlit(SDL_Rect& destination, SDL_Rect& source,
                 const SDL_Rect& destinationBounds, const SDL_Rect& sourceBounds)
{
    const auto clip = [](SDL_Rect& rect, SDL_Rect& paired, const SDL_Rect& bounds) {
        SDL_Rect intersection;
        if (!SDL_GetRectIntersection(&rect, &bounds, &intersection))
            return false;
        paired.x += intersection.x - rect.x;
        paired.y += intersection.y - rect.y;
        paired.w = intersection.w;
        paired.h = intersection.h;
        rect = intersection;
        return true;
    };
    return clip(source, destination, sourceBounds) && clip(destination, source, destinationBounds);
}

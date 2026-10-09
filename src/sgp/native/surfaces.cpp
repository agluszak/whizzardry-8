#include "compat/surfaces.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

struct IDirectDrawPalette
{
    unsigned references = 1;
    std::array<PALETTEENTRY, 256> entries{};
};
struct IDirectDrawClipper
{
    unsigned references = 1;
    std::vector<RECT> rectangles;
};
struct IDirectDrawSurface2
{
    DDSURFACEDESC description{};
    std::vector<BYTE> pixels;
    IDirectDrawPalette* palette = nullptr;
    IDirectDrawClipper* clipper = nullptr;
    DDCOLORKEY source_key{}, destination_key{};
    bool has_source_key = false, has_destination_key = false, locked = false;
    ~IDirectDrawSurface2()
    {
        if (palette)
            DDReleasePalette(palette);
        if (clipper)
            DDReleaseClipper(clipper);
    }
};
namespace
{
void require(bool condition, const char* operation)
{
    if (!condition)
        throw std::runtime_error(operation);
}
RECT bounds(const IDirectDrawSurface2* surface)
{
    return {0, 0, LONG(surface->description.dwWidth), LONG(surface->description.dwHeight)};
}
bool valid_rect(const RECT& rect, const RECT& limit)
{
    return rect.left >= 0 && rect.top >= 0 && rect.right > rect.left && rect.bottom > rect.top &&
           rect.right <= limit.right && rect.bottom <= limit.bottom;
}
DWORD pixel(const BYTE* source, unsigned bytes)
{
    DWORD value = 0;
    memcpy(&value, source, bytes);
    return value;
}
bool in_key(DWORD value, const DDCOLORKEY& key)
{
    return value >= key.dwColorSpaceLowValue && value <= key.dwColorSpaceHighValue;
}
} // namespace
void DDCreateSurface(LPDIRECTDRAW2, DDSURFACEDESC* desc, LPDIRECTDRAWSURFACE* first,
                     LPDIRECTDRAWSURFACE2* second)
{
    require(desc && first && second, "DDCreateSurface arguments");
    *first = nullptr;
    *second = nullptr;
    unsigned bits = desc->ddpfPixelFormat.dwRGBBitCount;
    require((bits == 8 || bits == 16 || bits == 32) && desc->dwWidth && desc->dwHeight &&
                desc->dwWidth <= 65535 && desc->dwHeight <= 65535,
            "DDCreateSurface dimensions/format");
    require((desc->ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) == 0,
            "native surfaces require system memory");
    size_t pitch = (size_t(desc->dwWidth) * (bits / 8) + 7) & ~size_t(7);
    auto surface = new IDirectDrawSurface2;
    try
    {
        surface->pixels.resize(pitch * desc->dwHeight);
    }
    catch (...)
    {
        delete surface;
        throw;
    }
    surface->description = *desc;
    surface->description.dwSize = sizeof(DDSURFACEDESC);
    surface->description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    surface->description.lPitch = LONG(pitch);
    surface->description.lpSurface = surface->pixels.data();
    /* The two SGP interface slots refer to one native owner. They are released
       together by DDReleaseSurface, rather than emulating COM identities. */
    *first = *second = surface;
}
void DDReleaseSurface(LPDIRECTDRAWSURFACE* first, LPDIRECTDRAWSURFACE2* second)
{
    auto a = first ? *first : nullptr;
    auto b = second ? *second : nullptr;
    if (first)
        *first = nullptr;
    if (second)
        *second = nullptr;
    delete a;
    if (b != a)
        delete b;
}
void DDGetSurfaceDescription(LPDIRECTDRAWSURFACE2 surface, DDSURFACEDESC* output)
{
    require(surface && output, "DDGetSurfaceDescription arguments");
    *output = surface->description;
}
void DDLockSurface(LPDIRECTDRAWSURFACE2 surface, LPRECT rect, LPDDSURFACEDESC output, UINT32 flags,
                   HANDLE event)
{
    require(surface && output && !surface->locked && !event && !flags,
            "DDLockSurface arguments/state");
    RECT region = rect ? *rect : bounds(surface);
    require(valid_rect(region, bounds(surface)), "DDLockSurface rectangle");
    *output = surface->description;
    output->lpSurface = surface->pixels.data() + size_t(region.top) * output->lPitch +
                        region.left * (output->ddpfPixelFormat.dwRGBBitCount / 8);
    output->dwWidth = region.right - region.left;
    output->dwHeight = region.bottom - region.top;
    surface->locked = true;
}
void DDUnlockSurface(LPDIRECTDRAWSURFACE2 surface, PTR)
{
    require(surface && surface->locked, "DDUnlockSurface state");
    surface->locked = false;
}
void DDRestoreSurface(LPDIRECTDRAWSURFACE2 surface)
{
    require(surface, "DDRestoreSurface arguments");
    /* Host storage persists across focus/display changes; there is no lost
       video-memory allocation to recreate. */
}
void DDBltSurface(LPDIRECTDRAWSURFACE2 dest, LPRECT dest_rect, LPDIRECTDRAWSURFACE2 source,
                  LPRECT source_rect, UINT32 flags, LPDDBLTFX effects)
{
    require(dest && !dest->locked &&
                !(flags & ~(DDBLT_WAIT | DDBLT_COLORFILL | DDBLT_KEYSRC | DDBLT_KEYDEST)),
            "DDBltSurface destination/flags");
    RECT d = dest_rect ? *dest_rect : bounds(dest);
    require(d.right > d.left && d.bottom > d.top, "DDBltSurface destination rectangle");
    unsigned bytes = dest->description.ddpfPixelFormat.dwRGBBitCount / 8;
    bool fill = flags & DDBLT_COLORFILL;
    require(!fill || (effects && !source && !(flags & (DDBLT_KEYSRC | DDBLT_KEYDEST))),
            "DDBltSurface fill");
    RECT s{};
    std::vector<BYTE> snapshot;
    if (!fill)
    {
        require(source && !source->locked &&
                    source->description.ddpfPixelFormat.dwRGBBitCount == bytes * 8,
                "DDBltSurface source/format");
        s = source_rect ? *source_rect : bounds(source);
        require(valid_rect(s, bounds(source)), "DDBltSurface source rectangle");
        require(!(flags & DDBLT_KEYSRC) || source->has_source_key, "source color key missing");
        require(!(flags & DDBLT_KEYDEST) || dest->has_destination_key,
                "destination color key missing");
        if (source == dest)
            snapshot = source->pixels;
    }
    std::vector<RECT> regions = dest->clipper ? dest->clipper->rectangles : std::vector<RECT>{d};
    for (RECT region : regions)
    {
        region.left = std::max<LONG>(region.left, std::max<LONG>(d.left, 0));
        region.top = std::max<LONG>(region.top, std::max<LONG>(d.top, 0));
        region.right =
            std::min<LONG>(region.right, std::min<LONG>(d.right, dest->description.dwWidth));
        region.bottom =
            std::min<LONG>(region.bottom, std::min<LONG>(d.bottom, dest->description.dwHeight));
        if (region.left >= region.right || region.top >= region.bottom)
            continue;
        RECT sample = s;
        if (!fill && dest->clipper)
        {
            /* DirectDraw clips the source endpoints for each canonical region,
               then restarts nearest-neighbour scaling inside that rectangle. */
            float scale_x = float(s.right - s.left) / float(d.right - d.left);
            float scale_y = float(s.bottom - s.top) / float(d.bottom - d.top);
            sample.left += LONG((region.left - d.left) * scale_x);
            sample.top += LONG((region.top - d.top) * scale_y);
            sample.right -= LONG((d.right - region.right) * scale_x);
            sample.bottom -= LONG((d.bottom - region.bottom) * scale_y);
        }
        RECT mapping = dest->clipper ? region : d;
        for (int y = region.top; y < region.bottom; ++y)
        {
            for (int x = region.left; x < region.right; ++x)
            {
                BYTE* output =
                    dest->pixels.data() + size_t(y) * dest->description.lPitch + x * bytes;
                DWORD value;
                if (fill)
                    value = effects->dwFillColor;
                else
                {
                    int sx =
                        sample.left + int(int64_t(x - mapping.left) * (sample.right - sample.left) /
                                          (mapping.right - mapping.left));
                    int sy =
                        sample.top + int(int64_t(y - mapping.top) * (sample.bottom - sample.top) /
                                         (mapping.bottom - mapping.top));
                    const BYTE* data = snapshot.empty() ? source->pixels.data() : snapshot.data();
                    value =
                        pixel(data + size_t(sy) * source->description.lPitch + sx * bytes, bytes);
                    if ((flags & DDBLT_KEYSRC) && in_key(value, source->source_key))
                        continue;
                    if ((flags & DDBLT_KEYDEST) &&
                        !in_key(pixel(output, bytes), dest->destination_key))
                        continue;
                }
                memcpy(output, &value, bytes);
            }
        }
    }
}
void DDBltFastSurface(LPDIRECTDRAWSURFACE2 dest, UINT32 x, UINT32 y, LPDIRECTDRAWSURFACE2 source,
                      LPRECT rect, UINT32 flags)
{
    require(source && dest && !(flags & ~3u), "DDBltFastSurface arguments/flags");
    require(!dest->clipper && x <= INT32_MAX && y <= INT32_MAX,
            "DDBltFastSurface clipper/position");
    RECT s = rect ? *rect : bounds(source);
    require(valid_rect(s, bounds(source)), "DDBltFastSurface source rectangle");
    require(uint64_t(x) + s.right - s.left <= dest->description.dwWidth &&
                uint64_t(y) + s.bottom - s.top <= dest->description.dwHeight,
            "DDBltFastSurface destination bounds");
    RECT d{LONG(x), LONG(y), LONG(x + s.right - s.left), LONG(y + s.bottom - s.top)};
    DDBltSurface(dest, &d, source, &s,
                 (flags & DDBLTFAST_SRCCOLORKEY ? DDBLT_KEYSRC : 0) |
                     (flags & DDBLTFAST_DESTCOLORKEY ? DDBLT_KEYDEST : 0),
                 nullptr);
}
void DDSetSurfaceColorKey(LPDIRECTDRAWSURFACE2 surface, UINT32 flags, LPDDCOLORKEY key)
{
    require(surface && key && key->dwColorSpaceLowValue <= key->dwColorSpaceHighValue,
            "DDSetSurfaceColorKey arguments");
    UINT32 kind = flags & ~DDCKEY_COLORSPACE;
    require(kind == DDCKEY_SRCBLT || kind == DDCKEY_DESTBLT, "DDSetSurfaceColorKey flags");
    DDCOLORKEY stored = *key;
    if (!(flags & DDCKEY_COLORSPACE))
        stored.dwColorSpaceHighValue = stored.dwColorSpaceLowValue;
    if (kind == DDCKEY_SRCBLT)
    {
        surface->source_key = stored;
        surface->has_source_key = true;
    }
    else
    {
        surface->destination_key = stored;
        surface->has_destination_key = true;
    }
}
void DDCreatePalette(LPDIRECTDRAW2, UINT32 flags, LPPALETTEENTRY data, LPDIRECTDRAWPALETTE* output,
                     void* outer)
{
    require(output && data && !outer && flags == (DDPCAPS_8BIT | DDPCAPS_ALLOW256),
            "DDCreatePalette arguments");
    auto palette = new IDirectDrawPalette;
    std::copy_n(data, 256, palette->entries.begin());
    *output = palette;
}
void DDReleasePalette(LPDIRECTDRAWPALETTE palette)
{
    if (palette && !--palette->references)
        delete palette;
}
void DDSetPaletteEntries(LPDIRECTDRAWPALETTE palette, UINT32 flags, UINT32 first, UINT32 count,
                         LPPALETTEENTRY data)
{
    require(palette && !flags && data && first <= 256 && count <= 256 - first,
            "DDSetPaletteEntries arguments");
    std::copy_n(data, count, palette->entries.begin() + first);
}
void DDGetPaletteEntries(LPDIRECTDRAWPALETTE palette, UINT32 flags, UINT32 first, UINT32 count,
                         LPPALETTEENTRY data)
{
    require(palette && !flags && data && first <= 256 && count <= 256 - first,
            "DDGetPaletteEntries arguments");
    std::copy_n(palette->entries.begin() + first, count, data);
}
void DDSetSurfacePalette(LPDIRECTDRAWSURFACE2 surface, LPDIRECTDRAWPALETTE palette)
{
    require(surface && surface->description.ddpfPixelFormat.dwRGBBitCount == 8,
            "DDSetSurfacePalette arguments");
    if (palette)
        ++palette->references;
    if (surface->palette)
        DDReleasePalette(surface->palette);
    surface->palette = palette;
}
HRESULT W8SurfaceGetPalette(LPDIRECTDRAWSURFACE2 surface, LPDIRECTDRAWPALETTE* output)
{
    if (!surface || !output || !surface->palette)
        return -1;
    *output = surface->palette;
    ++(*output)->references;
    return DD_OK;
}
void DDCreateClipper(LPDIRECTDRAW2, UINT32 flags, LPDIRECTDRAWCLIPPER* output)
{
    require(output && !flags, "DDCreateClipper arguments");
    *output = new IDirectDrawClipper;
}
void DDReleaseClipper(LPDIRECTDRAWCLIPPER clipper)
{
    if (clipper && !--clipper->references)
        delete clipper;
}
void DDSetClipperList(LPDIRECTDRAWCLIPPER clipper, LPRGNDATA regions, UINT32 flags)
{
    require(clipper && regions && !flags && regions->rdh.iType == RDH_RECTANGLES &&
                regions->rdh.nRgnSize >= uint64_t(regions->rdh.nCount) * sizeof(RECT),
            "DDSetClipperList arguments");
    std::vector<RECT> input(regions->rdh.nCount);
    memcpy(input.data(), regions->Buffer, input.size() * sizeof(RECT));
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
    clipper->rectangles.clear();
    std::vector<std::pair<LONG, LONG>> previous;
    size_t previous_start = 0;
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
            for (size_t j = previous_start; j < clipper->rectangles.size(); ++j)
                clipper->rectangles[j].bottom = edges[i];
        }
        else
        {
            previous_start = clipper->rectangles.size();
            for (auto span : merged)
                clipper->rectangles.push_back({span.first, edges[i - 1], span.second, edges[i]});
        }
        previous = merged;
    }
}
void DDSetClipper(LPDIRECTDRAWSURFACE2 surface, LPDIRECTDRAWCLIPPER clipper)
{
    require(surface, "DDSetClipper arguments");
    if (clipper)
        ++clipper->references;
    if (surface->clipper)
        DDReleaseClipper(surface->clipper);
    surface->clipper = clipper;
}

/* The same operations run against native CPU surfaces and Wine DirectDraw.
   Captures contain destination bytes, including row padding. */
#if defined(WIZ8_NATIVE)
#include "compat/surfaces.h"
#else
#include <ddraw.h>
#include <windows.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
static void check(bool value, const char* where)
{
    if (!value)
        throw std::runtime_error(where);
}
#if !defined(WIZ8_NATIVE)
static IDirectDraw2* draw;
#endif
struct Surface
{
    LPDIRECTDRAWSURFACE first = nullptr;
    LPDIRECTDRAWSURFACE2 second = nullptr;
    Surface(int width, int height, int bits)
    {
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwWidth = width;
        desc.dwHeight = height;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB | (bits == 8 ? DDPF_PALETTEINDEXED8 : 0);
        desc.ddpfPixelFormat.dwRGBBitCount = bits;
        if (bits == 16)
        {
            desc.ddpfPixelFormat.dwRBitMask = 0x7c00;
            desc.ddpfPixelFormat.dwGBitMask = 0x3e0;
            desc.ddpfPixelFormat.dwBBitMask = 0x1f;
        }
        else if (bits == 32)
        {
            desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
            desc.ddpfPixelFormat.dwGBitMask = 0xff00;
            desc.ddpfPixelFormat.dwBBitMask = 0xff;
        }
#if defined(WIZ8_NATIVE)
        DDCreateSurface(nullptr, &desc, &first, &second);
#else
        check(draw->CreateSurface(&desc, &first, nullptr) == DD_OK, "CreateSurface");
        check(first->QueryInterface(IID_IDirectDrawSurface2, (void**)&second) == DD_OK,
              "surface QI");
#endif
    }
    ~Surface()
    {
#if defined(WIZ8_NATIVE)
        DDReleaseSurface(&first, &second);
#else
        second->Release();
        first->Release();
#endif
    }
    DDSURFACEDESC lock()
    {
        DDSURFACEDESC desc{};
        desc.dwSize = sizeof(desc);
#if defined(WIZ8_NATIVE)
        DDLockSurface(second, nullptr, &desc, 0, nullptr);
#else
        check(second->Lock(nullptr, &desc, 0, nullptr) == DD_OK, "Lock");
#endif
        return desc;
    }
    void unlock()
    {
#if defined(WIZ8_NATIVE)
        DDUnlockSurface(second, nullptr);
#else
        check(second->Unlock(nullptr) == DD_OK, "Unlock");
#endif
    }
    void pattern(int seed)
    {
        auto desc = lock();
        int bytes = desc.ddpfPixelFormat.dwRGBBitCount / 8;
        memset(desc.lpSurface, 0xd6, desc.lPitch * desc.dwHeight);
        for (unsigned y = 0; y < desc.dwHeight; ++y)
            for (unsigned x = 0; x < desc.dwWidth; ++x)
            {
                uint32_t value = (x + y) % 5 == 0 ? 0 : (seed + x * 19 + y * 37);
                memcpy((char*)desc.lpSurface + y * desc.lPitch + x * bytes, &value, bytes);
            }
        unlock();
    }
    void keys()
    {
        DDCOLORKEY source{0, 0}, dest{0, 100};
#if defined(WIZ8_NATIVE)
        DDSetSurfaceColorKey(second, DDCKEY_SRCBLT, &source);
        DDSetSurfaceColorKey(second, DDCKEY_DESTBLT, &dest);
#else
        check(second->SetColorKey(DDCKEY_SRCBLT, &source) == DD_OK, "src key");
        check(second->SetColorKey(DDCKEY_DESTBLT, &dest) == DD_OK, "dest key");
#endif
    }
    void clip()
    {
        struct Regions
        {
            RGNDATAHEADER header;
            RECT rects[2];
        } regions{};
        regions.header.dwSize = sizeof(RGNDATAHEADER);
        regions.header.iType = RDH_RECTANGLES;
        regions.header.nCount = 2;
        regions.header.nRgnSize = sizeof(regions.rects);
        regions.header.rcBound = {0, 0, 13, 9};
        regions.rects[0] = {1, 1, 7, 6};
        regions.rects[1] = {5, 4, 12, 8};
        LPDIRECTDRAWCLIPPER clipper = nullptr;
#if defined(WIZ8_NATIVE)
        DDCreateClipper(nullptr, 0, &clipper);
        DDSetClipperList(clipper, (LPRGNDATA)&regions, 0);
        DDSetClipper(second, clipper);
        DDReleaseClipper(clipper);
#else
        check(draw->CreateClipper(0, &clipper, nullptr) == DD_OK, "CreateClipper");
        check(clipper->SetClipList((LPRGNDATA)&regions, 0) == DD_OK, "SetClipList");
        check(second->SetClipper(clipper) == DD_OK, "SetClipper");
        clipper->Release();
#endif
    }
};
static void blt(Surface& dest, RECT* d, Surface* source, RECT* s, DWORD flags, DWORD color = 0)
{
    DDBLTFX fx{};
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;
#if defined(WIZ8_NATIVE)
    DDBltSurface(dest.second, d, source ? source->second : nullptr, s, flags, &fx);
#else
    check(dest.second->Blt(d, source ? source->second : nullptr, s, flags, &fx) == DD_OK, "Blt");
#endif
}
int main(int argc, char** argv)
{
    try
    {
        bool capture = argc > 1 && !strcmp(argv[1], "--capture");
        FILE* oracle =
            capture ? nullptr : fopen(argc > 1 ? argv[1] : "tests/native/surfaces_legacy.txt", "r");
        check(capture || oracle, "open oracle");
#if !defined(WIZ8_NATIVE)
        IDirectDraw* base = nullptr;
        check(DirectDrawCreate(nullptr, &base, nullptr) == DD_OK, "DirectDrawCreate");
        check(base->QueryInterface(IID_IDirectDraw2, (void**)&draw) == DD_OK, "draw QI");
        base->Release();
        check(draw->SetCooperativeLevel(nullptr, DDSCL_NORMAL) == DD_OK, "cooperative level");
#endif
        int cases = 0;
        for (int bits : {8, 16, 32})
            for (int mode = 0; mode < 10; ++mode)
                for (int seed : {3, 79, 191})
                {
                    Surface source(9, 7, bits), dest(13, 9, bits);
                    source.pattern(seed);
                    dest.pattern(seed + 10);
                    source.keys();
                    dest.keys();
                    RECT s{1, 1, 8, 6}, d{2, 2, 9, 7};
                    if (mode == 0)
                        blt(dest, nullptr, nullptr, nullptr, DDBLT_COLORFILL, 0x5341);
                    if (mode == 1)
                        blt(dest, &d, &source, &s, 0);
                    if (mode == 2)
                        blt(dest, &d, &source, &s, DDBLT_KEYSRC);
                    if (mode == 3)
                        blt(dest, &d, &source, &s, DDBLT_KEYDEST);
                    if (mode == 4)
                        blt(dest, &d, &source, &s, DDBLT_KEYDEST | DDBLT_KEYSRC);
                    if (mode == 5)
                    {
                        d = {0, 0, 13, 9};
                        blt(dest, &d, &source, &s, 0);
                    }
                    if (mode == 6)
                    {
                        s = {0, 0, 10, 9};
                        d = {3, 0, 13, 9};
                        blt(dest, &d, &dest, &s, 0);
                    }
                    if (mode == 7)
                    {
                        s = {0, 0, 13, 6};
                        d = {0, 3, 13, 9};
                        blt(dest, &d, &dest, &s, 0);
                    }
                    if (mode == 8)
                    {
                        dest.clip();
                        d = {0, 0, 13, 9};
                        blt(dest, &d, &source, nullptr, 0);
                    }
                    if (mode == 9)
                    {
                        dest.clip();
                        d = {0, 0, 13, 9};
                        blt(dest, &d, nullptr, nullptr, DDBLT_COLORFILL, 0x2674);
                    }
                    auto desc = dest.lock();
                    uint64_t hash = 14695981039346656037ull;
                    for (size_t i = 0; i < size_t(desc.lPitch) * desc.dwHeight; ++i)
                    {
                        hash ^= ((unsigned char*)desc.lpSurface)[i];
                        hash *= 1099511628211ull;
                    }
                    dest.unlock();
                    char line[150], actual[150];
                    snprintf(actual, sizeof(actual), "%d %d %d %ld %016llx", bits, mode, seed,
                             (long)desc.lPitch, (unsigned long long)hash);
                    if (capture)
                        puts(actual);
                    else
                    {
                        check(fgets(line, sizeof(line), oracle) != nullptr, "oracle truncated");
                        line[strcspn(line, "\r\n")] = 0;
                        if (strcmp(actual, line))
                        {
                            fprintf(stderr, "expected %s\nactual   %s\n", line, actual);
                            return 1;
                        }
                    }
                    ++cases;
                }
        if (oracle)
        {
            check(fgetc(oracle) == EOF, "oracle extra cases");
            fclose(oracle);
        }
#if !defined(WIZ8_NATIVE)
        draw->Release();
#endif
        if (!capture)
            printf("ok: %d DirectDraw surface captures\n", cases);
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "surface oracle: %s\n", failure.what());
        return 1;
    }
}

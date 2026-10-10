#include <wiz8/filesystem.h>
#include <sstream>
// The same inputs run against the legacy assembly and the native C++ paths.
// --capture emits per-case hashes of every destination byte, the return value
// and the caller's rectangle (some legacy blitters modify it).
#include "compat/kernel32.h"
#include "vobject_blitters.h"
#include <stdint.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>

BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16*, UINT32, SGPRect*);

UINT16 ShadeTable[65536];
UINT16 IntensityTable[65536];

static const unsigned pitch = 1296; // 640 pixels plus row padding
static const unsigned rows = 20;
static UINT16 destination[pitch * rows / 2];
static UINT16 source[pitch * rows / 2];
static UINT16 palette[256];
// ShadowClip indexes pShade8 with destination words, so provide backing for
// every offset rather than capturing undefined reads past a 256-byte table.
static UINT8 palette8[65537];
static UINT8 encoded[4096];
static unsigned cases;
static std::istringstream reference;
static bool capture;

static void reset()
{
    for (unsigned i = 0; i < sizeof(destination) / sizeof(destination[0]); ++i) {
        destination[i] = static_cast<UINT16>(i * 337 + 19);
        source[i] = static_cast<UINT16>(i * 113 + 7);
    }
}

static bool check(const char* name, int variant, BOOLEAN result, const SGPRect& rect)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const UINT8* bytes = reinterpret_cast<const UINT8*>(destination);
    for (unsigned i = 0; i < sizeof(destination); ++i) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    char actual[256];
    sprintf(actual, "%s %d %u %d,%d,%d,%d %08x%08x\n", name, variant, result,
            rect.iLeft, rect.iTop, rect.iRight, rect.iBottom,
            static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash));
    ++cases;
    if (capture) {
        fputs(actual, stdout);
        return true;
    }
    std::string expected_line;
    const bool has_line = bool(std::getline(reference, expected_line));
    if (!expected_line.empty() && expected_line.back() == '\r') expected_line.pop_back();
    if (!has_line || expected_line + "\n" != actual) {
        fprintf(stderr, "case %u mismatch: %s", cases, actual);
        return false;
    }
    return true;
}

static void sprite_data(ETRLEObject& frame, int variant)
{
    frame.usWidth = variant == 2 ? 140 : 15;
    frame.usHeight = 5;
    frame.sOffsetX = 1;
    frame.sOffsetY = -1;
    frame.uiDataOffset = 3;
    unsigned out = frame.uiDataOffset;
    for (unsigned y = 0; y < frame.usHeight; ++y) {
        unsigned x = 0;
        unsigned run = 0;
        const unsigned lengths[] = {1, 2, 4, 7, 127};
        while (x < frame.usWidth) {
            unsigned count = lengths[(run + y) % 5];
            if (count > frame.usWidth - x) {
                count = frame.usWidth - x;
            }
            const bool transparent = (run + y) % 3 == 1;
            encoded[out++] = static_cast<UINT8>(count | (transparent ? 0x80 : 0));
            if (!transparent) {
                for (unsigned i = 0; i < count; ++i) {
                    // Include font-mask zero only without right clipping;
                    // the assembly's clipped tail scans bytes for zero.
                    encoded[out++] = variant == 1 ? static_cast<UINT8>((x + i + y) % 4) :
                                                    static_cast<UINT8>((x + i + 11 * y) % 255 + 1);
                }
            }
            x += count;
            ++run;
        }
        encoded[out++] = 0;
    }
    frame.uiDataLength = out - frame.uiDataOffset;
}

int main(int argc, char** argv)
{
    capture = argc == 2 && strcmp(argv[1], "--capture") == 0;
    if (!capture && argc != 2) {
        fprintf(stderr, "usage: blitter_test --capture | REFERENCE\n");
        return 1;
    }
    if (!capture) {
        try {
            auto file = wiz8::open_host_file(wiz8::path_from_utf8(argv[1]));
            std::string text(static_cast<std::size_t>(file->size()), '\0');
            if (file->read(text.data(), text.size()).bytes != text.size()) return 2;
            reference.str(text);
        } catch (const std::exception&) { return 2; }
    }
    for (unsigned i = 0; i < 65536; ++i) {
        ShadeTable[i] = static_cast<UINT16>((i * 37) ^ 0x1234);
        IntensityTable[i] = static_cast<UINT16>((i * 83) ^ 0x5678);
    }
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = static_cast<UINT16>((i * 257) ^ 0x57ab);
    }
    for (unsigned i = 0; i < sizeof(palette8); ++i) {
        palette8[i] = static_cast<UINT8>(i * 73 + 11);
    }
    SGPVObject object = {};
    object.pETRLEObject.resize(1);
    auto& frame = object.pETRLEObject[0];
    object.pPixData.resize(sizeof(encoded));
    object.pShade8 = palette8;
    object.pShadeCurrent = palette;
    ClippingRect = {0, 0, 640, static_cast<INT32>(rows)};
    SGPRect full = ClippingRect;
    bool ok = true;

    for (int data = 0; data < 3; ++data) {
        sprite_data(frame, data);
        std::copy_n(encoded, sizeof(encoded), object.pPixData.data());
        // Decoded-surface paths must produce the legacy assembly bytes as
        // well; data 1 carries literal index 0 in opaque runs and forces the
        // streaming fallbacks.
        DecodeVideoObjectSprites(&object);
        for (int variant = 0; variant < 8; ++variant) {
            // Whole, partial opaque/transparent runs, every clipped edge,
            // null/default clip rectangle and complete rejection.
            SGPRect clip = {3, 3, 12, 7};
            if (data == 1 || variant == 0 || variant == 6) {
                clip = full;
            }
            int x = variant == 1 ? -2 : variant == 7 ? 660 : 4;
            int y = variant == 2 ? 1 : variant == 3 ? 6 : 4;
            SGPRect* selected = variant == 6 ? nullptr : &clip;
            unsigned id = data * 8 + variant;
#define SPRITE(name, ...) \
            reset(); ok = check(#name, id, name(destination, pitch, &object, x, y, 0, selected, ##__VA_ARGS__), clip) && ok
            // The byte-output font path has a distinct buffer parameter.
            reset(); ok = check("Blt8BPPDataTo8BPPBufferMonoShadowClip", id,
                Blt8BPPDataTo8BPPBufferMonoShadowClip(reinterpret_cast<UINT8*>(destination), pitch,
                    &object, x, y, 0, selected, 0xa3, variant % 2 ? 0 : 0x2f), clip) && ok;
            SPRITE(Blt8BPPDataTo8BPPBufferTransparentClip);
            SPRITE(Blt8BPPDataTo8BPPBufferShadowClip);
            SPRITE(Blt8BPPDataTo16BPPBufferMonoShadowClip, 0xa351, variant % 2 ? 0 : 0x2f14, variant % 3 ? 0x1234 : 0);
            SPRITE(Blt8BPPDataTo16BPPBufferTransparentClip);
            SPRITE(Blt8BPPDataTo16BPPBufferShadowClip);
#undef SPRITE
        }
#define UNCLIPPED(name) \
            reset(); ok = check(#name, data, name(destination, pitch, &object, 4, 4, 0), full) && ok
        UNCLIPPED(Blt8BPPDataTo8BPPBufferTransparent);
        UNCLIPPED(Blt8BPPDataTo8BPPBufferShadow);
        UNCLIPPED(Blt8BPPDataTo16BPPBufferShadow);
        UNCLIPPED(Blt8BPPDataTo16BPPBufferTransparent);
        UNCLIPPED(Blt8BPPDataTo16BPPBufferTransMirror);
#undef UNCLIPPED
    }

    for (unsigned width = 1; width <= 9; ++width) {
        reset(); ok = check("Blt16BPPTo16BPP", width,
            Blt16BPPTo16BPP(destination, pitch, source, pitch, 3, 2, 1, 4, width, 5), full) && ok;
        reset(); ok = check("Blt16BPPTo16BPPTrans", width,
            Blt16BPPTo16BPPTrans(destination, pitch, source, pitch, 3, 2, 1, 4, width, 5,
                source[4 * pitch / 2 + 2]), full) && ok;
        reset(); ok = check("Blt8BPPTo8BPP", width,
            Blt8BPPTo8BPP(reinterpret_cast<UINT8*>(destination), pitch, reinterpret_cast<UINT8*>(source), pitch,
                3, 2, 1, 4, width, 5), full) && ok;
        reset(); ok = check("Blt16BPPTo16BPP-overlap", width,
            Blt16BPPTo16BPP(destination, pitch, destination, pitch, 2, 2, 1, 2, width, 5), full) && ok;
        reset(); ok = check("Blt8BPPTo8BPP-overlap", width,
            Blt8BPPTo8BPP(reinterpret_cast<UINT8*>(destination), pitch, reinterpret_cast<UINT8*>(destination), pitch,
                2, 2, 1, 2, width, 5), full) && ok;
        // The mirror ignores source position and clips against 640x480.
        for (int x = -3; x <= 637; x += 320) {
            reset(); ok = check("Blt16BPPTo16BPPMirror", width * 1000 + x,
                Blt16BPPTo16BPPMirror(destination, pitch, source, pitch, x, 2, 1, 4, width, 5), full) && ok;
        }
        SGPVSurface surface = {};
        surface.usWidth = pitch;
        surface.usHeight = rows;
        surface.p16BPPPalette = std::make_unique<UINT16[]>(256);
        std::copy_n(palette, 256, surface.p16BPPPalette.get());
        SGPRect area = {2, 3, static_cast<INT32>(2 + width), 7};
        reset(); ok = check("Blt8BPPDataSubTo16BPPBuffer", width,
            Blt8BPPDataSubTo16BPPBuffer(destination, pitch, &surface, reinterpret_cast<UINT8*>(source), pitch,
                3, 2, &area), area) && ok;
    }
    UINT8 pattern[8][8];
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned x = 0; x < 8; ++x) {
            pattern[y][x] = static_cast<UINT8>((x + 3 * y) % 5);
        }
    }
    for (int variant = 0; variant < 6; ++variant) {
        ClippingRect = variant % 2 ? SGPRect{2, 3, 18, 11} : full;
        SGPRect area = {-1, 1, 9 + variant, 12};
        reset(); ok = check("Blt16BPPBufferPixelateRectWithColor", variant,
            Blt16BPPBufferPixelateRectWithColor(destination, pitch, &area, pattern, 0x7391), area) && ok;
        reset(); ok = check("Blt16BPPBufferHatchRect", variant,
            Blt16BPPBufferHatchRect(destination, pitch, &area), area) && ok;
        area = {-1, 1, 9 + variant, 12};
        reset(); ok = check("Blt16BPPBufferShadowRect", variant,
            Blt16BPPBufferShadowRect(destination, pitch, &area), area) && ok;
        area = {-1, 1, 9 + variant, 12};
        reset(); ok = check("Blt16BPPBufferShadowRectAlternateTable", variant,
            Blt16BPPBufferShadowRectAlternateTable(destination, pitch, &area), area) && ok;
        reset(); ok = check("FillRect16BPP", variant,
            FillRect16BPP(destination, pitch, -1, 1, 9 + variant, 12, 0x693d), area) && ok;
    }
    if (!capture) {
        if (reference.peek() != EOF) {
            fprintf(stderr, "unexpected trailing cases\n");
            ok = false;
        }

        printf("%u blitter cases checked against legacy assembly output\n", cases);
    }
    return ok ? 0 : 1;
}

#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>
#include <sstream>
// The same inputs run against the legacy assembly and the native C++ paths.
// --capture emits per-case hashes of every destination byte, the return value
// and the caller's rectangle (some legacy blitters modify it).
#include "Types.h"
#include "vobject_blitters.h"
#include <stdint.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>

BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16*, UINT32, SDL_Rect*);

UINT16 ShadeTable[65536];
UINT16 IntensityTable[65536];

static const unsigned pitch = 1296; // 640 pixels plus row padding
static const unsigned rows = 20;
static UINT16 destination[pitch * rows / 2];
static UINT16 source[pitch * rows / 2];
static UINT16 palette[256];
static UINT8 encoded[4096];
static unsigned cases;
static std::istringstream reference;

static void reset()
{
    for (unsigned i = 0; i < sizeof(destination) / sizeof(destination[0]); ++i) {
        destination[i] = static_cast<UINT16>(i * 337 + 19);
        source[i] = static_cast<UINT16>(i * 113 + 7);
    }
}

static bool check(const char* name, int variant, BOOLEAN result, const SDL_Rect& rect)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const UINT8* bytes = reinterpret_cast<const UINT8*>(destination);
    for (unsigned i = 0; i < sizeof(destination); ++i) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    char actual[256];
    sprintf(actual, "%s %d %u %d,%d,%d,%d %08x%08x\n", name, variant, result,
            rect.x, rect.y, (rect.x + rect.w), (rect.y + rect.h),
            static_cast<unsigned>(hash >> 32), static_cast<unsigned>(hash));
    ++cases;
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

TEST_CASE("blitter_test")
{
    auto file = wiz8::open_host_file(wiz8::path_from_utf8(WIZ8_BLITTER_EXPECTED));
    std::string text(static_cast<std::size_t>(file->size()), '\0');
    REQUIRE(file->read(text.data(), text.size()).bytes == text.size());
    reference.str(text);
    for (unsigned i = 0; i < 65536; ++i) {
        ShadeTable[i] = static_cast<UINT16>((i * 37) ^ 0x1234);
        IntensityTable[i] = static_cast<UINT16>((i * 83) ^ 0x5678);
    }
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = static_cast<UINT16>((i * 257) ^ 0x57ab);
    }
    SGPVObject object = {};
    ETRLEObject frame{};
    object.pShadeCurrent = palette;
    ClippingRect = {0, 0, 640, static_cast<INT32>(rows)};
    SDL_Rect full = ClippingRect;
    bool ok = true;

    for (int data = 0; data < 3; ++data) {
        sprite_data(frame, data);
        // The single decoded representation must produce the legacy assembly
        // bytes; data 1 carries literal index 0 in opaque runs and exercises
        // the explicit opacity mask.
        if (!DecodeVideoObjectSprites(&object, {&frame, 1}, {encoded, sizeof(encoded)}))
            return 3;
        for (int variant = 0; variant < 8; ++variant) {
            // Whole, partial opaque/transparent runs, every clipped edge,
            // null/default clip rectangle and complete rejection.
            SDL_Rect clip = {3, 3, 9, 4};
            if (data == 1 || variant == 0 || variant == 6) {
                clip = full;
            }
            int x = variant == 1 ? -2 : variant == 7 ? 660 : 4;
            int y = variant == 2 ? 1 : variant == 3 ? 6 : 4;
            SDL_Rect* selected = variant == 6 ? nullptr : &clip;
            unsigned id = data * 8 + variant;
            const SpriteFrame& sprite = object.sprites[0];
#define SPRITE(name, ...) \
            reset(); ok = check(#name, id, \
                BltSpriteTo16BPPBuffer(destination, pitch, sprite, object.pShadeCurrent, \
                                       x, y, selected, __VA_ARGS__), clip) && ok
            SPRITE(Blt8BPPDataTo16BPPBufferMonoShadowClip, false, SpriteDrawOp::MonoShadow,
                   0xa351, variant % 2 ? 0 : 0x2f14, variant % 3 ? 0x1234 : 0);
            SPRITE(Blt8BPPDataTo16BPPBufferTransparentClip, false, SpriteDrawOp::Transparent);
            SPRITE(Blt8BPPDataTo16BPPBufferShadowClip, false, SpriteDrawOp::Shadow);
#undef SPRITE
        }
#define UNCLIPPED(name, ...) \
            reset(); ok = check(#name, data, \
                BltSpriteTo16BPPBuffer(destination, pitch, object.sprites[0], \
                                       object.pShadeCurrent, 4, 4, nullptr, __VA_ARGS__), full) && ok
        UNCLIPPED(Blt8BPPDataTo16BPPBufferShadow, false, SpriteDrawOp::Shadow);
        UNCLIPPED(Blt8BPPDataTo16BPPBufferTransparent, false, SpriteDrawOp::Transparent);
        UNCLIPPED(Blt8BPPDataTo16BPPBufferTransMirror, true, SpriteDrawOp::Transparent);
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
        SDL_Rect area = {2, 3, static_cast<INT32>(width), 4};
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
        ClippingRect = variant % 2 ? SDL_Rect{2, 3, 16, 8} : full;
        SDL_Rect area = {-1, 1, 10 + variant, 11};
        reset(); ok = check("Blt16BPPBufferPixelateRectWithColor", variant,
            Blt16BPPBufferPixelateRectWithColor(destination, pitch, &area, pattern, 0x7391), area) && ok;
        reset(); ok = check("Blt16BPPBufferHatchRect", variant,
            Blt16BPPBufferHatchRect(destination, pitch, &area), area) && ok;
        area = {-1, 1, 10 + variant, 11};
        reset(); ok = check("Blt16BPPBufferShadowRect", variant,
            Blt16BPPBufferShadowRect(destination, pitch, &area), area) && ok;
        area = {-1, 1, 10 + variant, 11};
        reset(); ok = check("Blt16BPPBufferShadowRectAlternateTable", variant,
            Blt16BPPBufferShadowRectAlternateTable(destination, pitch, &area), area) && ok;
        reset(); ok = check("FillRect16BPP", variant,
            FillRect16BPP(destination, pitch, -1, 1, 9 + variant, 12, 0x693d), area) && ok;
    }
    // Functional checks for behavior the hash fixtures cannot express: a
    // literal palette index 0 must draw its palette entry (not behave as
    // transparency), and a mirrored sprite crossing the left edge must still
    // draw its visible portion instead of being skipped entirely.
    {
        ClippingRect = full;
        SGPVObject probe = {};
        probe.pShadeCurrent = palette;
        ETRLEObject probe_frame{};
        probe_frame.usWidth = 3;
        probe_frame.usHeight = 1;
        probe_frame.uiDataOffset = 0;
        // opaque literal 0, transparent run of 1, opaque literal 2
        static const UINT8 probe_data[] = {1, 0, 0x81, 1, 2, 0};
        probe_frame.uiDataLength = sizeof(probe_data);
        if (!DecodeVideoObjectSprites(&probe, {&probe_frame, 1},
                                      {probe_data, sizeof(probe_data)})) {
            fprintf(stderr, "probe sprite failed to decode\n");
            ok = false;
        } else {
            const SpriteFrame& sprite = probe.sprites[0];
            reset();
            BltSpriteTo16BPPBuffer(destination, pitch, sprite, probe.pShadeCurrent, 4, 4,
                                   nullptr, false, SpriteDrawOp::Transparent);
            auto untouched = [](unsigned i) {
                return static_cast<UINT16>(i * 337 + 19);
            };
            const unsigned base = 4 * pitch / 2;
            if (destination[base + 4] != palette[0] ||
                destination[base + 5] != untouched(base + 5) ||
                destination[base + 6] != palette[2]) {
                fprintf(stderr, "functional mismatch: literal zero / transparency\n");
                ok = false;
            }
            // Mirrored at x = -1: only source columns 1 and 0 are visible,
            // reversed onto destination columns 0 and 1. The transparent
            // source column leaves the destination, the literal zero draws.
            reset();
            BltSpriteTo16BPPBuffer(destination, pitch, sprite, probe.pShadeCurrent, -1, 4,
                                   nullptr, true, SpriteDrawOp::Transparent);
            if (destination[base] != untouched(base) ||
                destination[base + 1] != palette[0] ||
                destination[base + 2] != untouched(base + 2)) {
                fprintf(stderr, "functional mismatch: clipped mirror\n");
                ok = false;
            }
        }
    }

    // Malformed frames must fail without replacing previously decoded sprites.
    {
        SGPVObject object{};
        object.sprites.emplace_back().usWidth = 7;
        ETRLEObject frame{};
        frame.usWidth = 1;
        frame.usHeight = 1;
        const UINT8 data[] = {0x80, 1, 2, 0};
        frame.uiDataLength = sizeof(data);
        auto rejected = [&] {
            return !DecodeVideoObjectSprites(&object, {&frame, 1}, data) &&
                   object.sprites.size() == 1 && object.sprites[0].usWidth == 7;
        };
        ok = rejected() && ok; // Zero-length encoded run.
        frame.usWidth = frame.usHeight = 65535;
        ok = rejected() && ok; // Reject before allocating gigabytes.
        frame.usWidth = frame.usHeight = 0;
        frame.uiDataOffset = sizeof(data) + 1;
        ok = rejected() && ok; // Empty frames still need valid encoded ranges.
        ok = !DecodeVideoObjectSprites(nullptr, {}, {}) && ok;
    }

    {
        if (reference.peek() != EOF) {
            fprintf(stderr, "unexpected trailing cases\n");
            ok = false;
        }

        printf("%u blitter cases checked against legacy assembly output\n", cases);
    }
    REQUIRE(ok);
}

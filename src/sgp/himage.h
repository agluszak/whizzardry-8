#pragma once

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <array>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "wiz8/surface_storage.h"

using Palette8 = std::array<SDL_Color, 256>;
using Palette16 = std::array<std::uint16_t, 256>;

// Decoded bitmap: INDEX8 with a 256-colour opaque palette, or direct-colour
// pixels in the format the decoder produced.
using BitmapImage = SDLSurfaceOwner;

struct SpriteFrame {
    static constexpr std::uint16_t kTransparent = 256;

    // Row-major palette indices 0-255 or kTransparent; opaque index 0 is not a transparent run.
    std::vector<std::uint16_t> indices;
    SDL_Point offset{};
    int width = 0;
    int height = 0;

    std::optional<std::uint8_t> paletteIndexAt(int x, int y) const;
};

struct SpriteSheet {
    Palette8 palette{};
    std::vector<SpriteFrame> frames;
    // `palette` packed in g_sgp_pixel_format.
    Palette16 colors{};
};

using DecodedImage = std::variant<BitmapImage, SpriteSheet>;

// *****************************************************************************
//
// Function prototypes
//
// *****************************************************************************

// Virtual game paths (including SLF entries), never host paths. The final
// basename extension selects STI or SDL_image PCX/TGA/JPEG; extensionless names
// default to PCX without changing the caller's path.
// STI zlib decoding and ETRLE frame decoding finish
// before a result is returned. Encoded file records never escape the loader.
// Returns no image on failure.
std::optional<DecodedImage> LoadImage(std::string_view filename);

// UTILITY FUNCTIONS

// Used to create a 16BPP Palette from an 8 bit palette, found in himage.c
Palette16 Create16BPPPaletteShaded(const Palette8& pPalette, std::uint32_t rscale, std::uint32_t gscale,
                                   std::uint32_t bscale, bool mono);
Palette16 Create16BPPPalette(const Palette8& pPalette);
std::uint16_t Get16BPPColor(SDL_Color color);
// The rendering session's 16-bit format: the framebuffer, frame surfaces, cached
// images and every packed palette or colour use it.
extern SDL_PixelFormat g_sgp_pixel_format;

#ifndef __VOBJECT_H
#define __VOBJECT_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

#include "himage.h"

// ************************************************************************************
//
// Video Object SGP Module
//
// ************************************************************************************

// This definition mimics what is found in WINDOWS.H ( for Direct Draw compatiblity )
typedef std::uint32_t COLORVAL;

// Defines for blitting
#define VO_BLT_SRCTRANSPARENCY 0x000000002
#define VO_BLT_DESTTRANSPARENCY 0x000000120
#define VO_BLT_SHADOW 0x000000200
#define VO_BLT_TRANSSHADOW 0x000000003
#define VO_BLT_CLIP 0x000000001
#define VO_BLT_MIRROR_Y 0x000001000 // must be the same as VS_BLT_MIRROR_Y for Wiz!!!

// Defines for SGPVObject& limits
#define HVOBJECT_SHADE_TABLES 48

#define HVOBJECT_GLOW_GREEN 0
#define HVOBJECT_GLOW_BLUE 1
#define HVOBJECT_GLOW_YELLOW 2
#define HVOBJECT_GLOW_RED 3

// Effects structure for specialized blitting
typedef struct {
    std::uint32_t uiShadowLevel;
    SDL_Rect ClipRect;

} blt_fx;

// This definition mimics what is found in WINDOWS.H ( for Direct Draw compatiblity )
// From RGB to COLORVAL
#define FROMRGB(r, g, b)                                                                           \
    ((std::uint32_t)(((std::uint8_t)(r) | ((std::uint16_t)(g) << 8)) | (((std::uint32_t)(std::uint8_t)(b)) << 16)))

// Owned indexed sprite frames and renderer palette tables.
struct SGPVObject {
    SGPVObject() = default;
    explicit SGPVObject(SpriteSheet image);
    Palette8 pPaletteEntry{}; // 8BPP Palette
    Palette16 palette{};

    std::vector<SpriteFrame> sprites;   // Decoded frames, one per ETRLE subimage
    std::vector<std::optional<Palette16>> pShades;
    void setPalette(const Palette8& colors);
    unsigned currentShade = 4;
    const Palette16& currentPalette() const {
        return currentShade == 4 ? palette : pShades[currentShade].value();
    }

};

// Palette generation and shade selection preserve the game's RGB555 tables.
std::uint16_t CreateObjectPaletteTables(SGPVObject& object, std::uint32_t type);
bool SetObjectShade(SGPVObject& object, std::uint32_t shade);

struct SGPVSurface;
bool BltVideoObject(SGPVSurface& destination, SGPVObject& source, std::uint16_t frame,
                       std::int32_t x, std::int32_t y, std::uint32_t flags, blt_fx* effects);
bool BltVideoObjectToBuffer(std::uint16_t* pixels, std::uint32_t pitch, SGPVObject& source,
                               std::uint16_t frame, std::int32_t x, std::int32_t y, std::int32_t flags, blt_fx* effects);

// Load an STI sprite sheet from the virtual filesystem.
std::unique_ptr<SGPVObject> LoadVideoObject(std::string_view filename);

#endif

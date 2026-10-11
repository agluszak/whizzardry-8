#ifndef __VOBJECT_H
#define __VOBJECT_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <array>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

#include "Types.h"
#include "himage.h"

// ************************************************************************************
//
// Video Object SGP Module
//
// ************************************************************************************

// Defines for blitting
#define VO_BLT_SRCTRANSPARENCY 0x000000002
#define VO_BLT_DESTTRANSPARENCY 0x000000120
#define VO_BLT_SHADOW 0x000000200
#define VO_BLT_TRANSSHADOW 0x000000003
#define VO_BLT_CLIP 0x000000001
#define VO_BLT_MIRROR_Y 0x000001000 // must be the same as VS_BLT_MIRROR_Y for Wiz!!!

#define HVOBJECT_GLOW_GREEN 0
#define HVOBJECT_GLOW_BLUE 1
#define HVOBJECT_GLOW_YELLOW 2
#define HVOBJECT_GLOW_RED 3

// Owned indexed sprite frames and renderer palette tables.
struct SGPVObject {
    SGPVObject() = default;
    explicit SGPVObject(SpriteSheet image);
    Palette8 pPaletteEntry{}; // 8BPP Palette
    Palette16 palette{};

    std::vector<SpriteFrame> sprites;   // Decoded frames, one per ETRLE subimage
    // Shade normalShade is the unshaded palette; the others exist after
    // CreateObjectPaletteTables.
    static constexpr unsigned normalShade = 4;
    std::array<std::optional<Palette16>, 16> pShades;
    void setPalette(const Palette8& colors);
    unsigned currentShade = normalShade;
    const Palette16& currentPalette() const {
        return currentShade == normalShade ? palette : pShades[currentShade].value();
    }

};

// Palette generation and shade selection preserve the game's RGB555 tables.
void CreateObjectPaletteTables(SGPVObject& object, UINT32 type);
bool SetObjectShade(SGPVObject& object, UINT32 shade);

struct SGPVSurface;
BOOLEAN BltVideoObject(SGPVSurface& destination, SGPVObject& source, UINT16 frame,
                       INT32 x, INT32 y, UINT32 flags);
BOOLEAN BltVideoObjectToBuffer(UINT16* pixels, UINT32 pitch, SGPVObject& source,
                               UINT16 frame, INT32 x, INT32 y, INT32 flags);

// Load an STI sprite sheet from the virtual filesystem.
std::unique_ptr<SGPVObject> LoadVideoObject(std::string_view filename);

#endif

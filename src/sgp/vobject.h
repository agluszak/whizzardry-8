#ifndef __VOBJECT_H
#define __VOBJECT_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_rect.h>
#include <string>
#include <array>
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
#define VO_BLT_UNCOMPRESSED 0x000004000
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
    UINT32 uiShadowLevel;
    SDL_Rect ClipRect;

} blt_fx;

// Video object creation flags
// Used in the VOBJECT_DESC structure to describe creation flags

#define VOBJECT_CREATE_DEFAULT 0x00000020 // Creates and empty object of given width, height and BPP
#define VOBJECT_CREATE_FROMFILE 0x00000040   // Creates a video object from a file ( using HIMAGE )
#define VOBJECT_CREATE_FROMHIMAGE 0x00000080 // Creates a video object from a pre-loaded hImage

// This structure is a video object.
// One decoded sprite frame: pixels are palette indices 0-255 or
// kTransparent for ETRLE transparent runs, so opaque palette index 0
// stays distinguishable without a separate mask. Placement metadata
// travels with the pixels; the compressed source is never retained.
struct SpriteFrame {
    static constexpr UINT16 kTransparent = 256;

    std::vector<UINT16> pixels;
    std::optional<UINT8> paletteIndexAt(int x, int y) const;
    INT16 sOffsetX = 0;
    INT16 sOffsetY = 0;
    UINT16 usHeight = 0;
    UINT16 usWidth = 0;
};

// The video object contains different data based on it's type, compressed or not
struct SGPVObject {
    Palette8 pPaletteEntry{}; // 8BPP Palette
    Palette16 palette{};

    std::vector<SpriteFrame> sprites;   // Decoded frames, one per ETRLE subimage
    std::vector<std::optional<Palette16>> pShades;
    void setPalette(const Palette8& colors);
    unsigned currentShade = 4;
    const Palette16& currentPalette() const {
        return currentShade == 4 ? palette : pShades[currentShade].value();
    }

    UINT8 ubBitDepth;         // BPP

};

// This structure describes the creation parameters for a Video Object
struct VOBJECT_DESC {
    UINT32 fCreateFlags{};
    std::string ImageFile;
    HIMAGE hImage = nullptr;
    UINT8 ubBitDepth{};
};

// Palette generation and shade selection preserve the game's RGB555 tables.
UINT16 CreateObjectPaletteTables(SGPVObject& object, UINT32 type);
UINT16 SetObjectShade(SGPVObject& object, UINT32 shade);

struct SGPVSurface;
BOOLEAN BltVideoObject(SGPVSurface& destination, SGPVObject& source, UINT16 frame,
                       INT32 x, INT32 y, UINT32 flags, blt_fx* effects);
BOOLEAN BltVideoObjectToBuffer(UINT16* pixels, UINT32 pitch, SGPVObject& source,
                               UINT16 frame, INT32 x, INT32 y, INT32 flags, blt_fx* effects);

std::unique_ptr<SGPVObject> CreateVideoObject(const VOBJECT_DESC& description);

#endif

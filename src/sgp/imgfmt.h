/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#if !defined(STCI_H)

#define STCI_H

// Sir-Tech's Crazy Image (STCI) file format specifications.  Each file is composed of:
// 1		ImageFileHeader, uncompressed
// *		Palette (STCI_INDEXED, size = uiNumberOfColours * PALETTE_ELEMENT_SIZE), uncompressed
// *		SubRectInfo's (usNumberOfRects > 0, size = usNumberOfSubRects * sizeof(SubRectInfo) ), uncompressed
// *		Bytes of image data, possibly compressed

#include <cstdint>

#define STCI_ID_STRING "STCI"
#define STCI_ID_LEN 4

#define STCI_ETRLE_COMPRESSED 0x0020
#define STCI_ZLIB_COMPRESSED 0x0010
#define STCI_INDEXED 0x0008
#define STCI_RGB 0x0004
#define STCI_ALPHA 0x0002
#define STCI_TRANSPARENT 0x0001

// ETRLE defines
#define COMPRESS_TRANSPARENT 0x80
#define COMPRESS_NON_TRANSPARENT 0x00
#define COMPRESS_RUN_LIMIT 0x7F

// NB if you're going to change the header definition:
// - make sure that everything in this header is nicely aligned
// - don't exceed the 64-byte maximum
typedef struct {
    std::uint8_t cID[STCI_ID_LEN];
    std::uint32_t uiOriginalSize;
    std::uint32_t uiStoredSize; // equal to uiOriginalSize if data uncompressed
    std::uint32_t uiTransparentValue;
    std::uint32_t fFlags;
    std::uint16_t usHeight;
    std::uint16_t usWidth;
    union {
        struct {
            std::uint32_t uiRedMask;
            std::uint32_t uiGreenMask;
            std::uint32_t uiBlueMask;
            std::uint32_t uiAlphaMask;
            std::uint8_t ubRedDepth;
            std::uint8_t ubGreenDepth;
            std::uint8_t ubBlueDepth;
            std::uint8_t ubAlphaDepth;
        } RGB;
        struct
        { // For indexed files, the palette will contain 3 separate bytes for red, green, and blue
            std::uint32_t uiNumberOfColours;
            std::uint16_t usNumberOfSubImages;
            std::uint8_t ubRedDepth;
            std::uint8_t ubGreenDepth;
            std::uint8_t ubBlueDepth;
            std::uint8_t cIndexedUnused[11];
        } Indexed;
    };
    std::uint8_t ubDepth; // size in bits of one pixel as stored in the file
    std::uint8_t padding[3];
    std::uint32_t uiAppDataSize;
    std::uint8_t cUnused[12];
} STCIHeader;

#define STCI_HEADER_SIZE 64

typedef struct {
    std::uint32_t uiDataOffset;
    std::uint32_t uiDataLength;
    std::int16_t sOffsetX;
    std::int16_t sOffsetY;
    std::uint16_t usHeight;
    std::uint16_t usWidth;
} STCISubImage;

#define STCI_SUBIMAGE_SIZE 16

typedef struct {
    std::uint8_t ubRed;
    std::uint8_t ubGreen;
    std::uint8_t ubBlue;
} STCIPaletteElement;

#define STCI_PALETTE_ELEMENT_SIZE 3
#define STCI_8BIT_PALETTE_SIZE 768

#endif

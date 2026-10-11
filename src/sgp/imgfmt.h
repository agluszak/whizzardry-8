/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

// Sir-Tech's Crazy Image (STCI) file layout:
// - STCIHeader
// - palette of STCIPaletteElement (indexed files only)
// - STCISubImage records (indexed files with sub-images)
// - pixel data, possibly ETRLE- or zlib-compressed

#include <cstddef>
#include <cstdint>

inline constexpr char STCI_ID_STRING[] = "STCI";
inline constexpr std::size_t STCI_ID_LEN = 4;

inline constexpr std::uint32_t STCI_ETRLE_COMPRESSED = 0x0020;
inline constexpr std::uint32_t STCI_ZLIB_COMPRESSED = 0x0010;
inline constexpr std::uint32_t STCI_INDEXED = 0x0008;
inline constexpr std::uint32_t STCI_RGB = 0x0004;

struct STCIHeader {
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
        struct {
            std::uint32_t uiNumberOfColours;
            std::uint16_t usNumberOfSubImages;
            std::uint8_t ubRedDepth;
            std::uint8_t ubGreenDepth;
            std::uint8_t ubBlueDepth;
            std::uint8_t cIndexedUnused[11];
        } Indexed;
    };
    std::uint8_t ubDepth; // bits per stored pixel
    std::uint8_t padding[3];
    std::uint32_t uiAppDataSize;
    std::uint8_t cUnused[12];
};

struct STCISubImage {
    std::uint32_t uiDataOffset;
    std::uint32_t uiDataLength;
    std::int16_t sOffsetX;
    std::int16_t sOffsetY;
    std::uint16_t usHeight;
    std::uint16_t usWidth;
};

struct STCIPaletteElement {
    std::uint8_t ubRed;
    std::uint8_t ubGreen;
    std::uint8_t ubBlue;
};

inline constexpr std::size_t STCI_HEADER_SIZE = 64;
inline constexpr std::size_t STCI_SUBIMAGE_SIZE = 16;
inline constexpr std::size_t STCI_8BIT_PALETTE_SIZE = 256 * sizeof(STCIPaletteElement);

static_assert(sizeof(STCIHeader) == STCI_HEADER_SIZE);
static_assert(sizeof(STCISubImage) == STCI_SUBIMAGE_SIZE);
static_assert(sizeof(STCIPaletteElement) == 3);

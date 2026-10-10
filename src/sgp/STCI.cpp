#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <string.h>
#include <array>
#include <algorithm>
#include "FileMan.h"
#include "imgfmt.h"
#include "himage.h"
#include "Types.h"
#include "WCheck.h"

BOOLEAN STCILoadRGB(HIMAGE hImage, UINT16 fContents, HWFILE hFile, STCIHeader* pHeader);
BOOLEAN STCILoadIndexed(HIMAGE hImage, UINT16 fContents, HWFILE hFile, STCIHeader* pHeader);
BOOLEAN STCISetPalette(PTR pSTCIPalette, HIMAGE hImage);

// FUNCTION: WIZ8 0x00415130
BOOLEAN LoadSTCIFileToImage(HIMAGE hImage, UINT16 fContents)
try
{
    HWFILE hFile;
    STCIHeader Header;
    UINT32 uiBytesRead;
    image_type TempImage{};

    // Check that hImage is valid, and that the file in question exists
    Assert(hImage != NULL);

    memcpy(TempImage.ImageFile, hImage->ImageFile, sizeof(TempImage.ImageFile));
    TempImage.iFileLoader = hImage->iFileLoader;

    CHECKF(FileExists(TempImage.ImageFile));

    // Open the file and read the header
    hFile = FileOpen(TempImage.ImageFile, FILE_ACCESS_READ, FALSE);
    CHECKF(hFile);
    struct FileCloser { HWFILE file; ~FileCloser() { FileClose(file); } } closer{hFile};

    if (!FileRead(hFile, &Header, STCI_HEADER_SIZE, &uiBytesRead) ||
        uiBytesRead != STCI_HEADER_SIZE || memcmp(Header.cID, STCI_ID_STRING, STCI_ID_LEN) != 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Problem reading STCI header.");
        return (FALSE);
    }

    // Determine from the header the data stored in the file. and run the appropriate loader
    if (Header.fFlags & STCI_RGB) {
        if (!STCILoadRGB(&TempImage, fContents, hFile, &Header)) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Problem loading RGB image.");
            return (FALSE);
        }
    } else if (Header.fFlags & STCI_INDEXED) {
        if (!STCILoadIndexed(&TempImage, fContents, hFile, &Header)) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Problem loading palettized image.");
            return (FALSE);
        }
    } else { // unsupported type of data, or the right flags weren't set!
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Unknown data organization in STCI file.");
        return (FALSE);
    }

    // Requested data loaded successfully.

    // Set some more flags in the temporary image structure, copy it so that hImage points
    // to it, and return.
    if (Header.fFlags & STCI_ZLIB_COMPRESSED) {
        TempImage.fFlags |= IMAGE_COMPRESSED;
    }
    TempImage.usWidth = Header.usWidth;
    TempImage.usHeight = Header.usHeight;
    TempImage.ubBitDepth = Header.ubDepth;
    ReleaseImageData(hImage, fContents);
    hImage->usWidth = TempImage.usWidth;
    hImage->usHeight = TempImage.usHeight;
    hImage->ubBitDepth = TempImage.ubBitDepth;
    hImage->fFlags |= TempImage.fFlags;
    if (fContents & IMAGE_PALETTE) {
        hImage->pPalette = std::move(TempImage.pPalette);
        hImage->pui16BPPPalette = std::move(TempImage.pui16BPPPalette);
    }
    if (fContents & IMAGE_BITMAPDATA) {
        hImage->pImageData = std::move(TempImage.pImageData);
        hImage->pETRLEObject = std::move(TempImage.pETRLEObject);
        hImage->usNumberOfObjects = TempImage.usNumberOfObjects;
        hImage->uiSizePixData = TempImage.uiSizePixData;
    }
    if (fContents & IMAGE_APPDATA) {
        hImage->pAppData = std::move(TempImage.pAppData);
        hImage->uiAppDataSize = TempImage.uiAppDataSize;
    }

    return (TRUE);
}

catch (...)
{
    return FALSE;
}

// FUNCTION: WIZ8 0x00415250
BOOLEAN STCILoadRGB(HIMAGE hImage, UINT16 fContents, HWFILE hFile, STCIHeader* pHeader)
{
    UINT32 uiBytesRead;

    if (fContents & IMAGE_PALETTE &&
        !(fContents & IMAGE_ALLIMAGEDATA)) { // RGB doesn't have a palette!
        return (FALSE);
    }

    if (fContents & IMAGE_BITMAPDATA) {
        // Allocate memory for the image data and read it in
        hImage->pImageData = std::make_unique<UINT8[]>(pHeader->uiStoredSize);
        if (hImage->pImageData == NULL) {
            return (FALSE);
        } else if (!FileRead(hFile, hImage->pImageData.get(), pHeader->uiStoredSize, &uiBytesRead) ||
                   uiBytesRead != pHeader->uiStoredSize) {
            return (FALSE);
        }

        hImage->fFlags |= IMAGE_BITMAPDATA;
        hImage->uiSizePixData = pHeader->uiStoredSize;

        if (pHeader->ubDepth == 16) {
            // ASSUMPTION: file data is 565 R,G,B

            if (gusRedMask != (UINT16)pHeader->RGB.uiRedMask ||
                gusGreenMask != (UINT16)pHeader->RGB.uiGreenMask ||
                gusBlueMask != (UINT16)pHeader->RGB.uiBlueMask) {
                // colour distribution of the file is different from hardware!  We have to change it!
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Converting to current RGB distribution!");
                // Convert the image to the current hardware's specifications
                if (gusRedMask > gusGreenMask && gusGreenMask > gusBlueMask) {
                    // hardware wants RGB!
                    if (gusRedMask == 0x7C00 && gusGreenMask == 0x03E0 &&
                        gusBlueMask == 0x001F) { // hardware is 555
                        ConvertRGBDistribution565To555(reinterpret_cast<UINT16*>(hImage->pImageData.get()),
                                                       pHeader->usWidth * pHeader->usHeight);
                        return (TRUE);
                    } else if (gusRedMask == 0xFC00 && gusGreenMask == 0x03E0 &&
                               gusBlueMask == 0x001F) {
                        ConvertRGBDistribution565To655(reinterpret_cast<UINT16*>(hImage->pImageData.get()),
                                                       pHeader->usWidth * pHeader->usHeight);
                        return (TRUE);
                    } else if (gusRedMask == 0xF800 && gusGreenMask == 0x07C0 &&
                               gusBlueMask == 0x003F) {
                        ConvertRGBDistribution565To556(reinterpret_cast<UINT16*>(hImage->pImageData.get()),
                                                       pHeader->usWidth * pHeader->usHeight);
                        return (TRUE);
                    } else {
                        // take the long route
                        ConvertRGBDistribution565ToAny(reinterpret_cast<UINT16*>(hImage->pImageData.get()),
                                                       pHeader->usWidth * pHeader->usHeight);
                        return (TRUE);
                    }
                } else {
                    // hardware distribution is not R-G-B so we have to take the long route!
                    ConvertRGBDistribution565ToAny(reinterpret_cast<UINT16*>(hImage->pImageData.get()),
                                                   pHeader->usWidth * pHeader->usHeight);
                    return (TRUE);
                }
            }
        }
    }
    // Anything else is an ERROR! --DB
    return (FALSE);
}

// FUNCTION: WIZ8 0x004153f0
BOOLEAN STCILoadIndexed(HIMAGE hImage, UINT16 fContents, HWFILE hFile, STCIHeader* pHeader)
{
    UINT32 uiBytesRead;
    const auto paletteBytes = pHeader->Indexed.uiNumberOfColours * STCI_PALETTE_ELEMENT_SIZE;
    if (fContents & IMAGE_PALETTE) {
        if (pHeader->Indexed.uiNumberOfColours != 256)
            return FALSE;
        std::array<STCIPaletteElement, 256> palette{};
        if (!FileRead(hFile, palette.data(), paletteBytes, &uiBytesRead) || uiBytesRead != paletteBytes ||
            !STCISetPalette(palette.data(), hImage))
            return FALSE;
        hImage->fFlags |= IMAGE_PALETTE;
    } else if ((fContents & (IMAGE_BITMAPDATA | IMAGE_APPDATA)) &&
               !FileSeek(hFile, paletteBytes, FILE_SEEK_FROM_CURRENT)) {
        return FALSE;
    }
    const auto objectCount = (pHeader->fFlags & STCI_ETRLE_COMPRESSED) ?
        pHeader->Indexed.usNumberOfSubImages : 0;
    const auto objectBytes = objectCount * STCI_SUBIMAGE_SIZE;
    if (fContents & IMAGE_BITMAPDATA) {
        if (pHeader->fFlags & STCI_ETRLE_COMPRESSED) {
            Assert(sizeof(ETRLEObject) == STCI_SUBIMAGE_SIZE);
            hImage->pETRLEObject = std::make_unique<ETRLEObject[]>(objectCount);
            if (!FileRead(hFile, hImage->pETRLEObject.get(), objectBytes, &uiBytesRead) ||
                uiBytesRead != objectBytes)
                return FALSE;
            hImage->usNumberOfObjects = objectCount;
            hImage->uiSizePixData = pHeader->uiStoredSize;
            hImage->fFlags |= IMAGE_TRLECOMPRESSED;
        }
        hImage->pImageData = std::make_unique<UINT8[]>(pHeader->uiStoredSize);
        if (!FileRead(hFile, hImage->pImageData.get(), pHeader->uiStoredSize, &uiBytesRead) ||
            uiBytesRead != pHeader->uiStoredSize)
            return FALSE;
        hImage->uiSizePixData = pHeader->uiStoredSize;
        hImage->fFlags |= IMAGE_BITMAPDATA;
    } else if ((fContents & IMAGE_APPDATA) &&
               !FileSeek(hFile, objectBytes + pHeader->uiStoredSize, FILE_SEEK_FROM_CURRENT)) {
        return FALSE;
    }
    if ((fContents & IMAGE_APPDATA) && pHeader->uiAppDataSize) {
        hImage->pAppData = std::make_unique<UINT8[]>(pHeader->uiAppDataSize);
        if (!FileRead(hFile, hImage->pAppData.get(), pHeader->uiAppDataSize, &uiBytesRead) ||
            uiBytesRead != pHeader->uiAppDataSize)
            return FALSE;
        hImage->uiAppDataSize = pHeader->uiAppDataSize;
        hImage->fFlags |= IMAGE_APPDATA;
    }
    return TRUE;
}

BOOLEAN STCISetPalette(PTR pSTCIPalette, HIMAGE hImage)
{
    UINT16 usIndex;
    STCIPaletteElement* pubPalette;

    pubPalette = (STCIPaletteElement*)pSTCIPalette;

    // Allocate memory for palette
    hImage->pPalette = std::make_unique<SGPPaletteEntry[]>(256);

    if (hImage->pPalette == NULL) {
        return (FALSE);
    }

    // Initialize the proper palette entries
    for (usIndex = 0; usIndex < 256; usIndex++) {
        hImage->pPalette[usIndex].peRed = pubPalette->ubRed;
        hImage->pPalette[usIndex].peGreen = pubPalette->ubGreen;
        hImage->pPalette[usIndex].peBlue = pubPalette->ubBlue;
        hImage->pPalette[usIndex].peFlags = 0;
        pubPalette++;
    }
    return TRUE;
}

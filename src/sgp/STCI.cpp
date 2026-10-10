#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <string.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <utility>
#include "wiz8/filesystem.h"
#include "imgfmt.h"
#include "himage.h"
#include "Types.h"
#include "WCheck.h"

BOOLEAN STCILoadRGB(HIMAGE hImage, UINT16 fContents, wiz8::File* hFile, STCIHeader* pHeader);
BOOLEAN STCILoadIndexed(HIMAGE hImage, UINT16 fContents, wiz8::File* hFile, STCIHeader* pHeader);

// FUNCTION: WIZ8 0x00415130
BOOLEAN LoadSTCIFileToImage(HIMAGE hImage, UINT16 fContents)
try
{
    STCIHeader Header{};
    image_type TempImage{};

    // Check that hImage is valid, and that the file in question exists
    Assert(hImage != nullptr);

    TempImage.ImageFile = hImage->ImageFile;
    TempImage.iFileLoader = hImage->iFileLoader;

    const auto hFile = wiz8::open_file(TempImage.ImageFile);
    if (hFile->read(&Header, STCI_HEADER_SIZE).bytes != STCI_HEADER_SIZE ||
        memcmp(Header.cID, STCI_ID_STRING, STCI_ID_LEN) != 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Problem reading STCI header.");
        return (FALSE);
    }

    // Determine from the header the data stored in the file. and run the appropriate loader
    if (Header.fFlags & STCI_RGB) {
        if (!STCILoadRGB(&TempImage, fContents, hFile.get(), &Header)) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Problem loading RGB image.");
            return (FALSE);
        }
    } else if (Header.fFlags & STCI_INDEXED) {
        if (!STCILoadIndexed(&TempImage, fContents, hFile.get(), &Header)) {
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
    }
    if (fContents & IMAGE_APPDATA) {
        hImage->pAppData = std::move(TempImage.pAppData);
    }

    return (TRUE);
}

catch (...)
{
    return FALSE;
}

// FUNCTION: WIZ8 0x00415250
BOOLEAN STCILoadRGB(HIMAGE hImage, UINT16 fContents, wiz8::File* hFile, STCIHeader* pHeader)
try
{
    if (pHeader->ubDepth != 16 && pHeader->ubDepth != 24)
        return FALSE;
    if (!(fContents & IMAGE_BITMAPDATA))
        return fContents == 0;
    const auto remaining = hFile->size() - hFile->tell();
    if (remaining < 0 || pHeader->uiStoredSize > static_cast<std::uint64_t>(remaining))
        return FALSE;
    if (!(pHeader->fFlags & STCI_ZLIB_COMPRESSED) &&
        std::size_t(pHeader->usWidth) * pHeader->usHeight * (pHeader->ubDepth / 8) > pHeader->uiStoredSize)
        return FALSE;
    hImage->pImageData.resize(pHeader->uiStoredSize);
    if (hFile->read(hImage->pImageData.data(), hImage->pImageData.size()).bytes != hImage->pImageData.size())
        return FALSE;
    hImage->fFlags |= IMAGE_BITMAPDATA;
    if (pHeader->ubDepth == 16 && !(pHeader->fFlags & STCI_ZLIB_COMPRESSED) &&
        (gusRedMask != pHeader->RGB.uiRedMask || gusGreenMask != pHeader->RGB.uiGreenMask ||
         gusBlueMask != pHeader->RGB.uiBlueMask)) {
        auto* pixels = reinterpret_cast<UINT16*>(hImage->pImageData.data());
        const UINT32 count = UINT32(pHeader->usWidth) * pHeader->usHeight;
        if (gusRedMask == 0x7C00 && gusGreenMask == 0x03E0 && gusBlueMask == 0x001F)
            ConvertRGBDistribution565To555(pixels, count);
        else if (gusRedMask == 0xFC00 && gusGreenMask == 0x03E0 && gusBlueMask == 0x001F)
            ConvertRGBDistribution565To655(pixels, count);
        else if (gusRedMask == 0xF800 && gusGreenMask == 0x07C0 && gusBlueMask == 0x003F)
            ConvertRGBDistribution565To556(pixels, count);
        else
            ConvertRGBDistribution565ToAny(pixels, count);
    }
    return TRUE;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004153f0
BOOLEAN STCILoadIndexed(HIMAGE hImage, UINT16 fContents, wiz8::File* hFile, STCIHeader* pHeader)
try
{
    UINT32 uiBytesRead;
    const auto paletteBytes = std::size_t(pHeader->Indexed.uiNumberOfColours) * STCI_PALETTE_ELEMENT_SIZE;
    if (fContents & IMAGE_PALETTE) {
        if (pHeader->Indexed.uiNumberOfColours != 256)
            return FALSE;
        std::array<STCIPaletteElement, 256> palette{};
        if (!((uiBytesRead = hFile->read(palette.data(), paletteBytes).bytes) == static_cast<std::size_t>(paletteBytes)) || uiBytesRead != paletteBytes)
            return FALSE;
        hImage->pPalette = std::make_unique<SGPPaletteEntry[]>(256);
        std::transform(palette.begin(), palette.end(), hImage->pPalette.get(),
            [](const STCIPaletteElement& color) {
                return SGPPaletteEntry{color.ubRed, color.ubGreen, color.ubBlue, 0};
            });
        hImage->fFlags |= IMAGE_PALETTE;
    } else if (fContents & (IMAGE_BITMAPDATA | IMAGE_APPDATA)) {
        hFile->seek(paletteBytes, wiz8::SeekOrigin::current);
    }
    const std::size_t objectCount = (pHeader->fFlags & STCI_ETRLE_COMPRESSED) ?
        pHeader->Indexed.usNumberOfSubImages : 0;
    const auto objectBytes = objectCount * STCI_SUBIMAGE_SIZE;
    if (fContents & IMAGE_BITMAPDATA) {
        const auto remaining = hFile->size() - hFile->tell();
        if (remaining < 0 || objectBytes + std::uint64_t(pHeader->uiStoredSize) >
            static_cast<std::uint64_t>(remaining))
            return FALSE;
        if (pHeader->fFlags & STCI_ETRLE_COMPRESSED) {
            Assert(sizeof(ETRLEObject) == STCI_SUBIMAGE_SIZE);
            hImage->pETRLEObject.resize(objectCount);
            if (!((uiBytesRead = hFile->read(hImage->pETRLEObject.data(), objectBytes).bytes) == static_cast<std::size_t>(objectBytes)) ||
                uiBytesRead != objectBytes)
                return FALSE;

            hImage->fFlags |= IMAGE_TRLECOMPRESSED;
        }
        hImage->pImageData.resize(pHeader->uiStoredSize);
        if (!((uiBytesRead = hFile->read(hImage->pImageData.data(), pHeader->uiStoredSize).bytes) == static_cast<std::size_t>(pHeader->uiStoredSize)) ||
            uiBytesRead != pHeader->uiStoredSize)
            return FALSE;
        for (const auto& frame : hImage->pETRLEObject) {
            if (std::uint64_t(frame.uiDataOffset) + frame.uiDataLength > hImage->pImageData.size())
                return FALSE;
        }

        hImage->fFlags |= IMAGE_BITMAPDATA;
    } else if (fContents & IMAGE_APPDATA) {
        hFile->seek(objectBytes + pHeader->uiStoredSize, wiz8::SeekOrigin::current);
    }
    if ((fContents & IMAGE_APPDATA) && pHeader->uiAppDataSize) {
        const auto remaining = hFile->size() - hFile->tell();
        if (remaining < 0 || pHeader->uiAppDataSize > static_cast<std::uint64_t>(remaining))
            return FALSE;
        hImage->pAppData.resize(pHeader->uiAppDataSize);
        if (!((uiBytesRead = hFile->read(hImage->pAppData.data(), pHeader->uiAppDataSize).bytes) == static_cast<std::size_t>(pHeader->uiAppDataSize)) ||
            uiBytesRead != pHeader->uiAppDataSize)
            return FALSE;

        hImage->fFlags |= IMAGE_APPDATA;
    }
    return TRUE;
}
catch (const std::exception&) { return false; }

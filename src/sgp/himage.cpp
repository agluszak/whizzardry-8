#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <math.h>
#include <stdlib.h>
#include "Types.h"
#include "string.h"
#include "wiz8/filesystem.h"
#include "himage.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "STCI.h"
#include "WCheck.h"
#include "Compression.h"
#include "vobject.h"

// This is the color substituted to keep a 24bpp -> 16bpp color
// from going transparent (0x0000) -- DB

#define BLACK_SUBSTITUTE 0x0001

// GLOBAL: WIZ8 0x00650f48
UINT16 gusAlphaMask = 0;
// GLOBAL: WIZ8 0x00650f4a
UINT16 gusRedMask = 0;
// GLOBAL: WIZ8 0x00650f4c
UINT16 gusGreenMask = 0;
// GLOBAL: WIZ8 0x00650f4e
UINT16 gusBlueMask = 0;
// GLOBAL: WIZ8 0x00650f50
INT16 gusRedShift = 0;
// GLOBAL: WIZ8 0x00650f52
INT16 gusBlueShift = 0;
// GLOBAL: WIZ8 0x00650f54
INT16 gusGreenShift = 0;

// this funky union is used for fast 16-bit pixel format conversions
typedef union {
    struct {
        UINT16 usLower;
        UINT16 usHigher;
    };
    UINT32 uiValue;
} SplitUINT32;

namespace
{
constexpr std::size_t max_image_bytes = 256 * 1024 * 1024;

using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

bool safe_dimensions(std::size_t width, std::size_t height)
{
    return width && height && width <= std::numeric_limits<UINT16>::max() &&
           height <= std::numeric_limits<UINT16>::max() &&
           width * height <= max_image_bytes / 4;
}

unsigned little_word(const UINT8* bytes)
{
    return bytes[0] | (unsigned(bytes[1]) << 8);
}

bool valid_image_header(const std::vector<UINT8>& bytes, UINT32 loader)
{
    if (loader == PCX_FILE_READER)
    {
        if (bytes.size() < 128 || bytes[0] != 10 || bytes[2] > 1)
            return false;
        const auto left = little_word(bytes.data() + 4), top = little_word(bytes.data() + 6);
        const auto right = little_word(bytes.data() + 8), bottom = little_word(bytes.data() + 10);
        const auto stride = little_word(bytes.data() + 66);
        return right >= left && bottom >= top && right < 32768 && bottom < 32768 &&
               safe_dimensions(right - left + 1, bottom - top + 1) &&
               bytes[3] == 8 && (bytes[65] == 1 || bytes[65] == 3) &&
               stride >= right - left + 1 && stride < 32768;
    }
    if (loader == TGA_FILE_READER)
    {
        if (bytes.size() < 18)
            return false;
        if ((bytes[2] == 1 || bytes[2] == 9) &&
            little_word(bytes.data() + 3) + little_word(bytes.data() + 5) > 256)
            return false;
        return (bytes[16] == 8 || bytes[16] == 16 || bytes[16] == 24) &&
               safe_dimensions(little_word(bytes.data() + 12), little_word(bytes.data() + 14));
    }
    if (loader == JPEG_FILE_READER)
    {
        if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xd8)
            return false;
        std::size_t position = 2;
        while (position < bytes.size())
        {
            if (bytes[position++] != 0xff)
                return false;
            while (position < bytes.size() && bytes[position] == 0xff)
                ++position;
            if (position == bytes.size())
                return false;
            const auto marker = bytes[position++];
            if (marker == 0xda || marker == 0xd9)
                return false;
            if (marker == 1 || (marker >= 0xd0 && marker <= 0xd8))
                continue;
            if (bytes.size() - position < 2)
                return false;
            const std::size_t length = (unsigned(bytes[position]) << 8) | bytes[position + 1];
            if (length < 2 || length > bytes.size() - position)
                return false;
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc)
            {
                if (length < 8)
                    return false;
                const auto height = (unsigned(bytes[position + 3]) << 8) | bytes[position + 4];
                const auto width = (unsigned(bytes[position + 5]) << 8) | bytes[position + 6];
                return safe_dimensions(width, height);
            }
            position += length;
        }
    }
    return false;
}

BOOLEAN LoadOrdinaryImage(HIMAGE image, UINT16 contents)
try
{
    const auto file = wiz8::open_file(image->ImageFile);
    if (!file)
        return FALSE;
    const auto size = file->size();
    if (size <= 0 || std::uint64_t(size) > max_image_bytes)
        return FALSE;
    std::vector<UINT8> bytes(size);
    UINT32 read = 0;
    if (!((read = file->read(bytes.data(), size).bytes) == static_cast<std::size_t>(size)) || read != size ||
        !valid_image_header(bytes, image->iFileLoader))
        return FALSE;

    // A bounded owned buffer gives SDL an independent cursor even for SLF entries.
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(
        SDL_IOFromConstMem(bytes.data(), bytes.size()), SDL_CloseIO);
    if (!stream)
        return FALSE;
    const char* type = image->iFileLoader == PCX_FILE_READER ? "PCX" :
                       image->iFileLoader == TGA_FILE_READER ? "TGA" : "JPG";
    Surface surface(IMG_LoadTyped_IO(stream.get(), false, type), SDL_DestroySurface);
    if (!surface || !safe_dimensions(surface->w, surface->h))
        return FALSE;

    auto* palette = SDL_GetSurfacePalette(surface.get());
    const bool indexed = surface->format == SDL_PIXELFORMAT_INDEX8 && palette;
    const bool rgb555 = surface->format == SDL_PIXELFORMAT_XRGB1555;
    if (!indexed && !rgb555)
    {
        Surface converted(SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGB24), SDL_DestroySurface);
        if (!converted)
            return FALSE;
        surface = std::move(converted);
    }
    const unsigned depth = indexed ? 8 : rgb555 ? 16 : 24;
    const std::size_t row_bytes = surface->w * (depth / 8);
    std::unique_ptr<UINT8[]> data;
    std::unique_ptr<SGPPaletteEntry[]> colors;
    std::unique_ptr<UINT16[]> packed_colors;
    if (contents & IMAGE_BITMAPDATA)
    {
        data = std::make_unique<UINT8[]>(row_bytes * surface->h);
        if (!data)
            return FALSE;
        for (int y = 0; y < surface->h; ++y)
            memcpy(data.get() + y * row_bytes,
                   static_cast<const UINT8*>(surface->pixels) + y * surface->pitch, row_bytes);
    }
    if (indexed && (contents & IMAGE_PALETTE))
    {
        colors = std::make_unique<SGPPaletteEntry[]>(256);
        if (!colors)
            return FALSE;
        memset(colors.get(), 0, 256 * sizeof(SGPPaletteEntry));
        const unsigned first_color = image->iFileLoader == TGA_FILE_READER &&
            (bytes[2] == 1 || bytes[2] == 9) ? little_word(bytes.data() + 3) : 0;
        for (unsigned i = 0; i < std::min(unsigned(palette->ncolors), 256 - first_color); ++i)
            colors.get()[first_color + i] = {palette->colors[i].r, palette->colors[i].g, palette->colors[i].b, 0};
        packed_colors = Create16BPPPalette(colors.get());
        if (!packed_colors)
            return FALSE;
    }

    // Publish only fully decoded data; failed reloads leave existing contents intact.
    ReleaseImageData(image, contents & IMAGE_ALLIMAGEDATA);
    image->usWidth = static_cast<UINT16>(surface->w);
    image->usHeight = static_cast<UINT16>(surface->h);
    image->ubBitDepth = static_cast<UINT8>(depth);
    if (data)
    {
        image->pImageData = std::move(data);
        image->fFlags |= IMAGE_BITMAPDATA;
    }
    if (colors)
    {
        image->pPalette = std::move(colors);
        image->pui16BPPPalette = std::move(packed_colors);
        image->fFlags |= IMAGE_PALETTE;
    }
    return TRUE;
}
catch (...)
{
    return FALSE;
}
}

// FUNCTION: WIZ8 0x0040f850
HIMAGE CreateImage(const char* ImageFile, UINT16 fContents)
try
{
    if (!ImageFile)
        return nullptr;
    std::string path(ImageFile);
    const auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
        path += ".PCX";
        dot = path.size() - 4;
    }
    if (path.size() >= sizeof(SGPFILENAME))
        return nullptr;
    const auto extension = path.substr(dot + 1);
    UINT32 iFileLoader = UNKNOWN_FILE_READER;
    if (_stricmp(extension.c_str(), "PCX") == 0)
        iFileLoader = PCX_FILE_READER;
    else if (_stricmp(extension.c_str(), "TGA") == 0)
        iFileLoader = TGA_FILE_READER;
    else if (_stricmp(extension.c_str(), "STI") == 0)
        iFileLoader = STCI_FILE_READER;
    else if (_stricmp(extension.c_str(), "JPG") == 0 || _stricmp(extension.c_str(), "JPEG") == 0)
        iFileLoader = JPEG_FILE_READER;
    if (iFileLoader == UNKNOWN_FILE_READER)
        return nullptr;

    // Determine if resource exists before creating image structure
    if (![&]() { const auto status = wiz8::file_status(path.data()); return status && status->info.type == SDL_PATHTYPE_FILE; }()) {
        //If in debig, make fatal!
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Resource file %s does not exist.", ImageFile);
        return (nullptr);
    }

    // Create memory for image structure
    auto hImage = std::make_unique<image_type>();

    if (!hImage)
        return nullptr;
    // Initialize some values

    // Set filename and loader
    memcpy(hImage->ImageFile, path.c_str(), path.size() + 1);
    hImage->iFileLoader = iFileLoader;

    if (!LoadImageData(hImage.get(), fContents)) {
        return (nullptr);
    }

    // All is fine, image is loaded and allocated, return pointer
    return hImage.release();
}
catch (...)
{
    return nullptr;
}

// FUNCTION: WIZ8 0x0040f9f0
BOOLEAN DestroyImage(HIMAGE hImage)
{
    Assert(hImage != nullptr);

    delete hImage;

    return (TRUE);
}

// FUNCTION: WIZ8 0x0040fa10
BOOLEAN ReleaseImageData(HIMAGE hImage, UINT16 fContents)
{

    Assert(hImage != nullptr);

    if (fContents & IMAGE_PALETTE) {
        hImage->pPalette.reset();
        hImage->pui16BPPPalette.reset();
        hImage->fFlags &= ~IMAGE_PALETTE;
    }
    if (fContents & IMAGE_BITMAPDATA) {
        hImage->pImageData.reset();
        hImage->pETRLEObject.reset();
        hImage->uiSizePixData = 0;
        hImage->usNumberOfObjects = 0;
        hImage->fFlags &= ~(IMAGE_BITMAPDATA | IMAGE_COMPRESSED | IMAGE_TRLECOMPRESSED);
    }
    if (fContents & IMAGE_APPDATA) {
        hImage->pAppData.reset();
        hImage->uiAppDataSize = 0;
        hImage->fFlags &= ~IMAGE_APPDATA;
    }

    return (TRUE);
}

BOOLEAN LoadImageData(HIMAGE hImage, UINT16 fContents)
{
    BOOLEAN fReturnVal = FALSE;

    Assert(hImage != nullptr);

    // Switch on file loader
    switch (hImage->iFileLoader) {
    case TGA_FILE_READER:
    case PCX_FILE_READER:
    case JPEG_FILE_READER:
        fReturnVal = LoadOrdinaryImage(hImage, fContents);
        break;

    case STCI_FILE_READER:
        fReturnVal = LoadSTCIFileToImage(hImage, fContents);
        break;

    default:

        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Unknown image loader was specified.");
    }

    if (!fReturnVal) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "Error occured while reading image data.");
    }

    return (fReturnVal);
}

// FUNCTION: WIZ8 0x0040fad0
BOOLEAN CopyImageToBuffer(HIMAGE hImage, UINT32 fBufferType, BYTE* pDestBuf, UINT16 usDestWidth,
                          UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect* srcRect)
{
    // Use blitter based on type of image
    Assert(hImage != nullptr);

    if (hImage->ubBitDepth == 8 && fBufferType == BUFFER_8BPP) {
#ifndef NO_ZLIB_COMPRESSION
        if (hImage->fFlags & IMAGE_COMPRESSED) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Copying Compressed 8 BPP Imagery.");
            return (Copy8BPPCompressedImageTo8BPPBuffer(hImage, pDestBuf, usDestWidth, usDestHeight,
                                                        usX, usY, srcRect));
        }
#endif

        // Default do here
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Copying 8 BPP Imagery.");
        return (Copy8BPPImageTo8BPPBuffer(hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY,
                                          srcRect));
    }

    if (hImage->ubBitDepth == 8 && fBufferType == BUFFER_16BPP) {
#ifndef NO_ZLIB_COMPRESSION
        if (hImage->fFlags & IMAGE_COMPRESSED) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Copying Compressed 8 BPP Imagery to 16BPP Buffer.");
            return (Copy8BPPCompressedImageTo16BPPBuffer(hImage, pDestBuf, usDestWidth,
                                                         usDestHeight, usX, usY, srcRect));
        }
#endif

        // Default do here
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Copying 8 BPP Imagery to 16BPP Buffer.");
        return (Copy8BPPImageTo16BPPBuffer(hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY,
                                           srcRect));
    }

    if (hImage->ubBitDepth == 16 && fBufferType == BUFFER_16BPP) {
#ifndef NO_ZLIB_COMPRESSION
        if (hImage->fFlags & IMAGE_COMPRESSED) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Automatically Copying Compressed 16 BPP Imagery.");
            return (Copy16BPPCompressedImageTo16BPPBuffer(hImage, pDestBuf, usDestWidth,
                                                          usDestHeight, usX, usY, srcRect));
        }
#endif

        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Automatically Copying 16 BPP Imagery.");
        return (Copy16BPPImageTo16BPPBuffer(hImage, pDestBuf, usDestWidth, usDestHeight, usX, usY,
                                            srcRect));
    }

    return (FALSE);
}

#ifndef NO_ZLIB_COMPRESSION

// FUNCTION: WIZ8 0x0040fba0
BOOLEAN Copy8BPPCompressedImageTo8BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                            UINT16 usDestHeight, UINT16 usX, UINT16 usY,
                                            SGPRect* srcRect)
{
    Assert(hImage != nullptr);
    Assert(hImage->pImageData.get() != nullptr);

    // Validations
    CHECKF(usX >= 0);
    CHECKF(usX < usDestWidth);
    CHECKF(usY >= 0);
    CHECKF(usY < usDestHeight);
    CHECKF(srcRect->iRight > srcRect->iLeft);
    CHECKF(srcRect->iBottom > srcRect->iTop);

    /* Retail decompressed scanlines through the requested rectangle into a
       scratch line but never copied them: its memcpy calls are commented out. */
    return (TRUE);
}

// FUNCTION: WIZ8 0x0040fca0
BOOLEAN Copy8BPPCompressedImageTo16BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                             UINT16 usDestHeight, UINT16 usX, UINT16 usY,
                                             SGPRect* srcRect)
{
    UINT32 uiNumLines;
    UINT32 uiLineSize;
    UINT32 uiLine;
    UINT32 uiCol;

    UINT16* pDest;
    UINT16* pDestTemp;
    UINT32 uiDestStart;

    UINT8* pScanLine;
    UINT8* pScanLineTemp;

    z_stream* pDecompPtr;
    UINT32 uiDecompressed;

    UINT16* p16BPPPalette;

    // Assertions
    Assert(hImage != nullptr);
    Assert(hImage->pImageData.get() != nullptr);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Start check");
    // Validations
    CHECKF(usX >= 0);
    CHECKF(usX < usDestWidth);
    CHECKF(usY >= 0);
    CHECKF(usY < usDestHeight);
    CHECKF(srcRect->iRight > srcRect->iLeft);
    CHECKF(srcRect->iBottom > srcRect->iTop);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "End check");
    p16BPPPalette = hImage->pui16BPPPalette.get();

    // determine where to start Copying and rectangle size
    uiDestStart = usY * usDestWidth + usX;
    uiNumLines = srcRect->iBottom - srcRect->iTop;
    uiLineSize = srcRect->iRight - srcRect->iLeft;

    Assert(usDestWidth >= uiLineSize);
    Assert(usDestHeight >= uiNumLines);

    pDest = (UINT16*)pDestBuf;
    pDest += uiDestStart;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Start Copying at %p", pDest);

    // Copying a portion of a compressed image is rather messy
    // because we have to decompress past all the data we want
    // to skip.

    // To keep memory requirements small and regular, we will
    // decompress one scanline at a time even if none of the data will
    // be blitted (but stop when the bottom line of the rectangle
    // to blit has been done).

    // initialize the decompression routines
    auto decompressor = DecompressInit(hImage->pImageData.get(), hImage->uiSizePixData);
    pDecompPtr = decompressor.get();
    CHECKF(pDecompPtr);

    // Allocate memory for one scanline
    auto scanline = std::make_unique<UINT8[]>(hImage->usWidth);
    pScanLine = scanline.get();
    CHECKF(pScanLine);

    // go past all the scanlines we don't need to process
    for (uiLine = 0; uiLine < (UINT32)srcRect->iTop; uiLine++) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Skipping scanline");
        uiDecompressed = Decompress(pDecompPtr, pScanLine, hImage->usWidth);
        Assert(uiDecompressed == hImage->usWidth);
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Actually Copying");
    // now we start Copying
    for (uiLine = 0; uiLine < uiNumLines - 1; uiLine++) {
        // decompress a scanline
        Decompress(pDecompPtr, pScanLine, hImage->usWidth);

        // set pointers and blit
        pDestTemp = pDest;
        pScanLineTemp = pScanLine + srcRect->iLeft;
        for (uiCol = 0; uiCol < uiLineSize; uiCol++) {
            *pDestTemp = p16BPPPalette[*pScanLineTemp];
            pDestTemp++;
            pScanLineTemp++;
        }
        pDest += usDestWidth;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "End Copying at %p", pDest);

    return (TRUE);
}

BOOLEAN Copy16BPPCompressedImageTo16BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                              UINT16 usDestHeight, UINT16 usX, UINT16 usY,
                                              SGPRect* srcRect)
{
    // 16BPP Compressed image has not been implemented yet
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "16BPP Compressed imagery blitter has not been implemented yet.");
    return (FALSE);
}
#endif //NO_ZLIB_COMPRESSION

// FUNCTION: WIZ8 0x0040fe40
BOOLEAN Copy8BPPImageTo8BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                  UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect* srcRect)
{
    UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
    UINT32 cnt;
    UINT8 *pDest, *pSrc;

    // Assertions
    Assert(hImage != nullptr);
    Assert(reinterpret_cast<UINT16*>(hImage->pImageData.get()) != nullptr);

    // Validations
    CHECKF(usX >= 0);
    CHECKF(usX < usDestWidth);
    CHECKF(usY >= 0);
    CHECKF(usY < usDestHeight);
    CHECKF(srcRect->iRight > srcRect->iLeft);
    CHECKF(srcRect->iBottom > srcRect->iTop);

    // Determine memcopy coordinates
    uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
    uiDestStart = usY * usDestWidth + usX;
    uiNumLines = srcRect->iBottom - srcRect->iTop;
    uiLineSize = srcRect->iRight - srcRect->iLeft;

    Assert(usDestWidth >= uiLineSize);
    Assert(usDestHeight >= uiNumLines);

    // Copy line by line
    pDest = (UINT8*)pDestBuf + uiDestStart;
    pSrc = hImage->pImageData.get() + uiSrcStart;

    for (cnt = 0; cnt < uiNumLines - 1; cnt++) {
        memcpy(pDest, pSrc, uiLineSize);
        pDest += usDestWidth;
        pSrc += hImage->usWidth;
    }
    // Do last line
    memcpy(pDest, pSrc, uiLineSize);

    return (TRUE);
}

// FUNCTION: WIZ8 0x0040ff30
BOOLEAN Copy16BPPImageTo16BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                    UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect* srcRect)
{
    UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
    UINT32 cnt;
    UINT16 *pDest, *pSrc;

    Assert(hImage != nullptr);
    Assert(reinterpret_cast<UINT16*>(hImage->pImageData.get()) != nullptr);

    // Validations
    CHECKF(usX >= 0);
    CHECKF(usX < hImage->usWidth);
    CHECKF(usY >= 0);
    CHECKF(usY < hImage->usHeight);
    CHECKF(srcRect->iRight > srcRect->iLeft);
    CHECKF(srcRect->iBottom > srcRect->iTop);

    // Determine memcopy coordinates
    uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
    uiDestStart = usY * usDestWidth + usX;
    uiNumLines = srcRect->iBottom - srcRect->iTop;
    uiLineSize = srcRect->iRight - srcRect->iLeft;

    CHECKF(usDestWidth >= uiLineSize);
    CHECKF(usDestHeight >= uiNumLines);

    // Copy line by line
    pDest = (UINT16*)pDestBuf + uiDestStart;
    pSrc = reinterpret_cast<UINT16*>(hImage->pImageData.get()) + uiSrcStart;

    for (cnt = 0; cnt < uiNumLines - 1; cnt++) {
        memcpy(pDest, pSrc, uiLineSize * 2);
        pDest += usDestWidth;
        pSrc += hImage->usWidth;
    }
    // Do last line
    memcpy(pDest, pSrc, uiLineSize * 2);

    return (TRUE);
}

// FUNCTION: WIZ8 0x00410050
BOOLEAN Copy8BPPImageTo16BPPBuffer(HIMAGE hImage, BYTE* pDestBuf, UINT16 usDestWidth,
                                   UINT16 usDestHeight, UINT16 usX, UINT16 usY, SGPRect* srcRect)
{
    UINT32 uiSrcStart, uiDestStart, uiNumLines, uiLineSize;
    UINT32 rows, cols;
    UINT8 *pSrc, *pSrcTemp;
    UINT16 *pDest, *pDestTemp;
    UINT16* p16BPPPalette;

    p16BPPPalette = hImage->pui16BPPPalette.get();

    // Assertions
    Assert(p16BPPPalette != nullptr);
    Assert(hImage != nullptr);

    // Validations
    CHECKF(reinterpret_cast<UINT16*>(hImage->pImageData.get()) != nullptr);
    CHECKF(usX >= 0);
    CHECKF(usX < usDestWidth);
    CHECKF(usY >= 0);
    CHECKF(usY < usDestHeight);
    CHECKF(srcRect->iRight > srcRect->iLeft);
    CHECKF(srcRect->iBottom > srcRect->iTop);

    // Determine memcopy coordinates
    uiSrcStart = srcRect->iTop * hImage->usWidth + srcRect->iLeft;
    uiDestStart = usY * usDestWidth + usX;
    uiNumLines = (srcRect->iBottom - srcRect->iTop);
    uiLineSize = (srcRect->iRight - srcRect->iLeft);

    CHECKF(usDestWidth >= uiLineSize);
    CHECKF(usDestHeight >= uiNumLines);

    // Convert to Pixel specification
    pDest = (UINT16*)pDestBuf + uiDestStart;
    pSrc = hImage->pImageData.get() + uiSrcStart;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Start Copying at %p", pDest);

    // For every entry, look up into 16BPP palette
    for (rows = 0; rows < uiNumLines - 1; rows++) {
        pDestTemp = pDest;
        pSrcTemp = pSrc;

        for (cols = 0; cols < uiLineSize; cols++) {
            *pDestTemp = p16BPPPalette[*pSrcTemp];
            pDestTemp++;
            pSrcTemp++;
        }

        pDest += usDestWidth;
        pSrc += hImage->usWidth;
    }
    // Do last line
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "End Copying at %p", pDest);

    return (TRUE);
}

// FUNCTION: WIZ8 0x00410190
std::unique_ptr<UINT16[]> Create16BPPPalette(SGPPaletteEntry* pPalette)
{
    auto p16BPPPalette = std::make_unique<UINT16[]>(256);
    UINT16 r16, g16, b16, usColor;
    UINT32 cnt;
    UINT8 r, g, b;

    Assert(pPalette != nullptr);


    if (!p16BPPPalette)
        return nullptr;

    for (cnt = 0; cnt < 256; cnt++) {
        r = pPalette[cnt].peRed;
        g = pPalette[cnt].peGreen;
        b = pPalette[cnt].peBlue;

        if (gusRedShift < 0)
            r16 = ((UINT16)r >> abs(gusRedShift));
        else
            r16 = ((UINT16)r << gusRedShift);

        if (gusGreenShift < 0)
            g16 = ((UINT16)g >> abs(gusGreenShift));
        else
            g16 = ((UINT16)g << gusGreenShift);

        if (gusBlueShift < 0)
            b16 = ((UINT16)b >> abs(gusBlueShift));
        else
            b16 = ((UINT16)b << gusBlueShift);

        usColor = (r16 & gusRedMask) | (g16 & gusGreenMask) | (b16 & gusBlueMask);

        if (usColor == 0) {
            if ((r + g + b) != 0)
                usColor = BLACK_SUBSTITUTE | gusAlphaMask;
        } else
            usColor |= gusAlphaMask;

        p16BPPPalette[cnt] = usColor;
    }

    return (p16BPPPalette);
}

/**********************************************************************************************
 Create16BPPPaletteShaded

	Creates an 8 bit to 16 bit palette table, and modifies the colors as it builds.

	Parameters:
		rscale, gscale, bscale:
				Color mode: Percentages (255=100%) of color to translate into destination palette.
				Mono mode:  Color for monochrome palette.
		mono:
				TRUE or FALSE to create a monochrome palette. In mono mode, Luminance values for
				colors are calculated, and the RGB color is shaded according to each pixel's brightness.

	This can be used in several ways:

	1) To "brighten" a palette, pass down RGB values that are higher than 100% ( > 255) for all
			three. mono=FALSE.
	2) To "darken" a palette, do the same with less than 100% ( < 255) values. mono=FALSE.

	3) To create a "glow" palette, select mono=TRUE, and pass the color in the RGB parameters.

	4) For gamma correction, pass in weighted values for each color.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004102c0
std::unique_ptr<UINT16[]> Create16BPPPaletteShaded(SGPPaletteEntry* pPalette, UINT32 rscale, UINT32 gscale,
                                 UINT32 bscale, BOOLEAN mono)
{
    auto p16BPPPalette = std::make_unique<UINT16[]>(256);
    UINT16 r16, g16, b16, usColor;
    UINT32 cnt, lumin;
    UINT32 rmod, gmod, bmod;
    UINT8 r, g, b;

    Assert(pPalette != nullptr);


    for (cnt = 0; cnt < 256; cnt++) {
        if (mono) {
            lumin = (pPalette[cnt].peRed * 299 / 1000) + (pPalette[cnt].peGreen * 587 / 1000) +
                    (pPalette[cnt].peBlue * 114 / 1000);
            rmod = (rscale * lumin) / 256;
            gmod = (gscale * lumin) / 256;
            bmod = (bscale * lumin) / 256;
        } else {
            rmod = (rscale * pPalette[cnt].peRed / 256);
            gmod = (gscale * pPalette[cnt].peGreen / 256);
            bmod = (bscale * pPalette[cnt].peBlue / 256);
        }

        r = (UINT8)__min(rmod, 255);
        g = (UINT8)__min(gmod, 255);
        b = (UINT8)__min(bmod, 255);

        if (gusRedShift < 0)
            r16 = ((UINT16)r >> (-gusRedShift));
        else
            r16 = ((UINT16)r << gusRedShift);

        if (gusGreenShift < 0)
            g16 = ((UINT16)g >> (-gusGreenShift));
        else
            g16 = ((UINT16)g << gusGreenShift);

        if (gusBlueShift < 0)
            b16 = ((UINT16)b >> (-gusBlueShift));
        else
            b16 = ((UINT16)b << gusBlueShift);

        // Prevent creation of pure black color
        usColor = (r16 & gusRedMask) | (g16 & gusGreenMask) | (b16 & gusBlueMask);

        if (usColor == 0) {
            if ((r + g + b) != 0)
                usColor = BLACK_SUBSTITUTE | gusAlphaMask;
        } else
            usColor |= gusAlphaMask;

        p16BPPPalette[cnt] = usColor;
    }
    return (p16BPPPalette);
}

// Convert from RGB to 16 bit value
// FUNCTION: WIZ8 0x004104b0
UINT16 Get16BPPColor(UINT32 RGBValue)
{
    UINT16 r16, g16, b16, usColor;
    UINT8 r, g, b;

    r = SGPGetRValue(RGBValue);
    g = SGPGetGValue(RGBValue);
    b = SGPGetBValue(RGBValue);

    if (gusRedShift < 0)
        r16 = ((UINT16)r >> abs(gusRedShift));
    else
        r16 = ((UINT16)r << gusRedShift);

    if (gusGreenShift < 0)
        g16 = ((UINT16)g >> abs(gusGreenShift));
    else
        g16 = ((UINT16)g << gusGreenShift);

    if (gusBlueShift < 0)
        b16 = ((UINT16)b >> abs(gusBlueShift));
    else
        b16 = ((UINT16)b << gusBlueShift);

    usColor = (r16 & gusRedMask) | (g16 & gusGreenMask) | (b16 & gusBlueMask);

    // if our color worked out to absolute black, and the original wasn't
    // absolute black, convert it to a VERY dark grey to avoid transparency
    // problems

    if (usColor == 0) {
        if (RGBValue != 0)
            usColor = BLACK_SUBSTITUTE | gusAlphaMask;
    } else
        usColor |= gusAlphaMask;

    return (usColor);
}
// ConvertToPaletteEntry
// Parameter List : Converts from RGB to SGPPaletteEntry
// Return Value  pointer to the SGPPaletteEntry
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry

// FUNCTION: WIZ8 0x00410580
BOOLEAN GetETRLEImageData(HIMAGE hImage, ETRLEData* pBuffer)
{
    // Assertions
    Assert(hImage != nullptr);
    Assert(pBuffer != nullptr);

    ETRLEData data{};
    data.usNumberOfObjects = hImage->usNumberOfObjects;
    data.uiSizePixData = hImage->uiSizePixData;
    data.pETRLEObject = std::make_unique<ETRLEObject[]>(data.usNumberOfObjects);
    data.pPixData = std::make_unique<UINT8[]>(data.uiSizePixData);
    std::copy_n(hImage->pETRLEObject.get(), data.usNumberOfObjects, data.pETRLEObject.get());
    std::copy_n(hImage->pImageData.get(), data.uiSizePixData, data.pPixData.get());
    *pBuffer = std::move(data);

    return (TRUE);
}

// FUNCTION: WIZ8 0x00410620
void ConvertRGBDistribution565To555(UINT16* p16BPPData, UINT32 uiNumberOfPixels)
{
    UINT16* pPixel;
    UINT32 uiLoop;

    SplitUINT32 Pixel;

    pPixel = p16BPPData;
    for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++) {
        // If the pixel is completely black, don't bother converting it -- DB
        if (*pPixel != 0) {
            // we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
            // right shift the blue value (at the bottom) into the LOWER word to protect it
            Pixel.usHigher = *pPixel;
            Pixel.uiValue >>= 5;
            // get rid of the least significant bit of green
            Pixel.usHigher >>= 1;
            // now shift back into the upper word
            Pixel.uiValue <<= 5;
            // and copy back
            *pPixel = Pixel.usHigher | gusAlphaMask;
        }
        pPixel++;
    }
}

// FUNCTION: WIZ8 0x00410670
void ConvertRGBDistribution565To655(UINT16* p16BPPData, UINT32 uiNumberOfPixels)
{
    UINT16* pPixel;
    UINT32 uiLoop;

    SplitUINT32 Pixel;

    pPixel = p16BPPData;
    for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++) {
        // we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
        // right shift the blue value (at the bottom) into the LOWER word to protect it
        Pixel.usHigher = *pPixel;
        Pixel.uiValue >>= 5;
        // get rid of the least significant bit of green
        Pixel.usHigher >>= 1;
        // shift to the right some more...
        Pixel.uiValue >>= 5;
        // so we can left-shift the red value alone to give it an extra bit
        Pixel.usHigher <<= 1;
        // now shift back and copy
        Pixel.uiValue <<= 10;
        *pPixel = Pixel.usHigher;
        pPixel++;
    }
}

// FUNCTION: WIZ8 0x004106c0
void ConvertRGBDistribution565To556(UINT16* p16BPPData, UINT32 uiNumberOfPixels)
{
    UINT16* pPixel;
    UINT32 uiLoop;

    SplitUINT32 Pixel;

    pPixel = p16BPPData;
    for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++) {
        // we put the 16 pixel bits in the UPPER word of uiPixel, so that we can
        // right shift the blue value (at the bottom) into the LOWER word to protect it
        Pixel.usHigher = *pPixel;
        Pixel.uiValue >>= 5;
        // get rid of the least significant bit of green
        Pixel.usHigher >>= 1;
        // shift back into the upper word
        Pixel.uiValue <<= 5;
        // give blue an extra bit (blank in the least significant spot)
        Pixel.usHigher <<= 1;
        // copy back
        *pPixel = Pixel.usHigher;
        pPixel++;
    }
}

// FUNCTION: WIZ8 0x00410700
void ConvertRGBDistribution565ToAny(UINT16* p16BPPData, UINT32 uiNumberOfPixels)
{
    UINT16* pPixel;
    UINT32 uiRed, uiGreen, uiBlue, uiTemp, uiLoop;

    pPixel = p16BPPData;
    for (uiLoop = 0; uiLoop < uiNumberOfPixels; uiLoop++) {
        // put the 565 RGB 16-bit value into a 32-bit RGB value
        uiRed = (*pPixel) >> 11;
        uiGreen = (*pPixel & 0x07E0) >> 5;
        uiBlue = (*pPixel & 0x001F);
        uiTemp = FROMRGB(uiRed, uiGreen, uiBlue);
        // then convert the 32-bit RGB value to whatever 16 bit format is used
        *pPixel = Get16BPPColor(uiTemp);
        pPixel++;
    }
}

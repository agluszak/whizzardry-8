#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
// font.c
#include "Types.h"
#include <stdio.h>
#include <stdarg.h>
#include "compat/kernel32.h"
#include <stdarg.h>
#include <wchar.h>
#include <algorithm>
#include "sgp.h"
#include "wiz8/filesystem.h"
#include "Font.h"

#include "Video2.h"

#include "himage.h"
#include "vobject.h"
#include "vobject_blitters.h"
//   Defines

#define PALETTE_SIZE 768
#define STRING_DELIMITER 0
#define ID_BLACK 0
#define MAX_FONTS 25
//   Typedefs

SGPPaletteEntry gSgpPalette[256];

// GLOBAL: WIZ8 0x006eb704
static std::vector<UINT16> g_font_translation;
// GLOBAL: WIZ8 0x006eb6a0
std::array<std::unique_ptr<SGPVObject>, MAX_FONTS> FontObjs;
INT32 FontsLoaded = 0;

// Destination printing parameters
// GLOBAL: WIZ8 0x005ff5f0
INT32 FontDefault = (-1);
// GLOBAL: WIZ8 0x005ff5f4
UINT32 FontDestBuffer = BACKBUFFER;
// GLOBAL: WIZ8 0x005ff5f8
UINT32 FontDestPitch = 640 * 2;
// GLOBAL: WIZ8 0x005ff5fc
UINT32 FontDestBPP = 16;
// GLOBAL: WIZ8 0x005ff600
SGPRect FontDestRegion = {0, 0, 640, 480};
// GLOBAL: WIZ8 0x00650e38
BOOLEAN FontDestWrap = FALSE;
// GLOBAL: WIZ8 0x00650e3a
UINT16 FontForeground16 = 0;
// GLOBAL: WIZ8 0x00650e3c
UINT16 FontBackground16 = 0;
// GLOBAL: WIZ8 0x005ff610
UINT16 FontShadow16 = DEFAULT_SHADOW;
// GLOBAL: WIZ8 0x00650e3e
UINT8 FontForeground8 = 0;
// GLOBAL: WIZ8 0x00650e3f
UINT8 FontBackground8 = 0;

// Temp, for saving printing parameters
// GLOBAL: WIZ8 0x005ff614
INT32 SaveFontDefault = (-1);
// GLOBAL: WIZ8 0x005ff618
UINT32 SaveFontDestBuffer = BACKBUFFER;
// GLOBAL: WIZ8 0x005ff61c
UINT32 SaveFontDestPitch = 640 * 2;
// GLOBAL: WIZ8 0x005ff620
UINT32 SaveFontDestBPP = 16;
// GLOBAL: WIZ8 0x005ff628
SGPRect SaveFontDestRegion = {0, 0, 640, 480};
// GLOBAL: WIZ8 0x00650e40
BOOLEAN SaveFontDestWrap = FALSE;
// GLOBAL: WIZ8 0x00650e42
UINT16 SaveFontForeground16 = 0;
// GLOBAL: WIZ8 0x00650e44
UINT16 SaveFontShadow16 = 0;
// GLOBAL: WIZ8 0x00650e46
UINT16 SaveFontBackground16 = 0;
// GLOBAL: WIZ8 0x00650e48
UINT8 SaveFontForeground8 = 0;
// GLOBAL: WIZ8 0x00650e49
UINT8 SaveFontBackground8 = 0;

// SetFontForeground
//	Sets the foreground color of the currently selected font. The parameter is
// the index into the 8-bit palette. In 8BPP mode, that index number is used
// for the pixel value to be drawn for nontransparent pixels. In 16BPP mode,
// the RGB values from the palette are used to create the pixel color. Note
// that if you change fonts, the selected foreground/background colors will
// stay at what they are currently set to.

// FUNCTION: WIZ8 0x00406c20
void SetFontForeground(UINT8 ubForeground)
{
    UINT32 uiRed, uiGreen, uiBlue;

    if ((FontDefault < 0) || (FontDefault > MAX_FONTS))
        return;

    FontForeground8 = ubForeground;

    uiRed = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubForeground].peRed;
    uiGreen = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubForeground].peGreen;
    uiBlue = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubForeground].peBlue;

    FontForeground16 = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}

// FUNCTION: WIZ8 0x00406c90
void SetFontShadow(UINT8 ubShadow)
{
    UINT32 uiRed, uiGreen, uiBlue;

    if ((FontDefault < 0) || (FontDefault > MAX_FONTS))
        return;

    //FontForeground8=ubForeground;

    uiRed = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubShadow].peRed;
    uiGreen = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubShadow].peGreen;
    uiBlue = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubShadow].peBlue;

    FontShadow16 = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));

    if (ubShadow != 0) {
        if (FontShadow16 == 0) {
            FontShadow16 = 1;
        }
    }
}

// SetFontBackground
//	Sets the Background color of the currently selected font. The parameter is
// the index into the 8-bit palette. In 8BPP mode, that index number is used
// for the pixel value to be drawn for nontransparent pixels. In 16BPP mode,
// the RGB values from the palette are used to create the pixel color. If the
// background value is zero, the background of the font will be transparent.
// Note that if you change fonts, the selected foreground/background colors will
// stay at what they are currently set to.

// FUNCTION: WIZ8 0x00406d10
void SetFontBackground(UINT8 ubBackground)
{
    UINT32 uiRed, uiGreen, uiBlue;

    if ((FontDefault < 0) || (FontDefault > MAX_FONTS))
        return;

    FontBackground8 = ubBackground;

    uiRed = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubBackground].peRed;
    uiGreen = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubBackground].peGreen;
    uiBlue = (UINT32)FontObjs[FontDefault]->pPaletteEntry[ubBackground].peBlue;

    FontBackground16 = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}

// FUNCTION: WIZ8 0x00406d80
void SetRGBFontShadow(UINT32 uiRed, UINT32 uiGreen, UINT32 uiBlue)
{
    if ((FontDefault < 0) || (FontDefault > MAX_FONTS))
        return;
    FontShadow16 = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}
//end Kris

// SetFontObjectPalette16BPP
//	Sets the palette of a font, using a 16 bit palette.

// FUNCTION: WIZ8 0x00406dc0
UINT16* SetFontObjectPalette16BPP(INT32 iFont, UINT16* pPal16)
{
    Assert(iFont >= 0);
    Assert(iFont <= MAX_FONTS);
    Assert(FontObjs[iFont] != nullptr);

    FontObjs[iFont]->p16BPPPalette = pPal16;
    FontObjs[iFont]->pShadeCurrent = pPal16;

    return (pPal16);
}

// GetFontObjectPalette16BPP
//	Sets the palette of a font, using a 16 bit palette.

// FUNCTION: WIZ8 0x00406de0
UINT16* GetFontObjectPalette16BPP(INT32 iFont)
{
    Assert(iFont >= 0);
    Assert(iFont <= MAX_FONTS);
    Assert(FontObjs[iFont] != nullptr);

    return (FontObjs[iFont]->p16BPPPalette);
}

// GetFontObject
//	Returns the VOBJECT pointer of a font.

// FUNCTION: WIZ8 0x00406df0
HVOBJECT GetFontObject(INT32 iFont)
{
    Assert(iFont >= 0);
    Assert(iFont <= MAX_FONTS);
    Assert(FontObjs[iFont] != nullptr);

    return (FontObjs[iFont].get());
}

// FindFreeFont
//	Locates an empty slot in the font table.

INT32 FindFreeFont(void)
{
    int count;

    for (count = 0; count < MAX_FONTS; count++)
        if (FontObjs[count] == nullptr)
            return (count);

    return (-1);
}

// LoadFontFile
//	Loads a font from an ETRLE file, and inserts it into one of the font slots.
//  This function returns (-1) if it fails, and debug msgs for a reason.
//  Otherwise the font number is returned.

// FUNCTION: WIZ8 0x00406e00
INT32 LoadFontFile(std::string_view filename)
{
    VOBJECT_DESC vo_desc;
    INT32 LoadIndex;

    Assert(!filename.empty());

    if ((LoadIndex = FindFreeFont()) == (-1)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Out of font slots (%.*s)", static_cast<int>(filename.size()), filename.data());
        return (-1);
    }

    vo_desc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
    vo_desc.ImageFile = filename;

    FontObjs[LoadIndex].reset(CreateVideoObject(&vo_desc));
    if (!FontObjs[LoadIndex].get()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error creating VOBJECT (%.*s)", static_cast<int>(filename.size()), filename.data());
        return (-1);
    }

    if (FontDefault == (-1))
        FontDefault = LoadIndex;

    return (LoadIndex);
}

// UnloadFont - Delete the font structure
//	Deletes the video object of a particular font. Frees up the memory and
// resources allocated for it.

void UnloadFont(UINT32 FontIndex)
{
    Assert(FontIndex >= 0);
    Assert(FontIndex < MAX_FONTS);
    Assert(FontObjs[FontIndex] != nullptr);

    FontObjs[FontIndex].reset();
    if (FontDefault == static_cast<INT32>(FontIndex)) {
        const auto font = std::find_if(FontObjs.begin(), FontObjs.end(),
                                      [](const auto& candidate) { return bool(candidate); });
        FontDefault = font == FontObjs.end() ? -1 : static_cast<INT32>(font - FontObjs.begin());
    }
}

// GetWidth
//	Returns the width of a given character in the font.

UINT32 GetWidth(HVOBJECT hSrcVObject, INT16 ssIndex)
{
    ETRLEObject* pTrav;

    // Assertions
    Assert(hSrcVObject != nullptr);

    if (ssIndex < 0 || ssIndex > 92) {
    }

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[ssIndex]);
    return ((UINT32)(pTrav->usWidth + pTrav->sOffsetX));
}

// StringPixLengthArg
//		Returns the length of a string with a variable number of arguments, in
// pixels, using the current font. Maximum length in characters the string can
// evaluate to is 512.
//    'uiCharCount' specifies how many characters of the string are counted.

// FUNCTION: WIZ8 0x00406ea0
INT16 StringPixLengthArg(INT32 usUseFont, UINT32 uiCharCount, const CHAR16* pFontString, ...)
{
    va_list argptr;
    wchar_t string[512];

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    // make sure the character count is legal
    if (uiCharCount > wcslen(string)) {
        uiCharCount = wcslen(string);
    } else {
        if (uiCharCount < wcslen(string)) {
            // less than the full string, so whack off the end of it (it's temporary anyway)
            string[uiCharCount] = '\0';
        }
    }

    return (StringPixLength(string, usUseFont));
}
//  StringNPixLength
//  Return the length of the of the string or count characters in the
//  string, which ever comes first.
//  Returns INT16
//  Created by:     Gilles Beauparlant
//  Created on:     12/1/99

static inline CHAR16 ReadFontCharacter(const CHAR16* text)
{
    CHAR16 value;
    memcpy(&value, reinterpret_cast<const unsigned char*>(text), sizeof(value));
    return value;
}

// FUNCTION: WIZ8 0x00406f90
INT16 StringNPixLength(CHAR16* string, UINT32 uiMaxCount, INT32 UseFont)
{
    UINT32 Cur, uiCharCount;
    CHAR16 *curletter, transletter;

    Cur = 0;
    uiCharCount = 0;
    curletter = string;

    while (ReadFontCharacter(curletter) != L'\0' && uiCharCount < uiMaxCount) {
        transletter = GetIndex(ReadFontCharacter(curletter++));
        Cur += GetWidth(FontObjs[UseFont].get(), transletter);
        uiCharCount++;
    }
    return ((INT16)Cur);
}
// StringPixLength
//	Returns the length of a string in pixels, depending on the font given.

// FUNCTION: WIZ8 0x00407010
INT16 StringPixLength(const CHAR16* string, INT32 UseFont)
{
    UINT32 Cur;
    const CHAR16* curletter;
    CHAR16 transletter;

    if (string == nullptr) {
        return (0);
    }

    Cur = 0;
    curletter = string;

    while (ReadFontCharacter(curletter) != L'\0') {
        transletter = GetIndex(ReadFontCharacter(curletter++));
        Cur += GetWidth(FontObjs[UseFont].get(), transletter);
    }
    return ((INT16)Cur);
}
// SaveFontSettings
//	Saves the current font printing settings into temporary locations.

// FUNCTION: WIZ8 0x00407090
void SaveFontSettings(void)
{
    SaveFontDefault = FontDefault;
    SaveFontDestBuffer = FontDestBuffer;
    SaveFontDestPitch = FontDestPitch;
    SaveFontDestBPP = FontDestBPP;
    SaveFontDestRegion = FontDestRegion;
    SaveFontDestWrap = FontDestWrap;
    SaveFontForeground16 = FontForeground16;
    SaveFontShadow16 = FontShadow16;
    SaveFontBackground16 = FontBackground16;
    SaveFontForeground8 = FontForeground8;
    SaveFontBackground8 = FontBackground8;
}
// RestoreFontSettings
//	Restores the last saved font printing settings from the temporary lactions

// FUNCTION: WIZ8 0x00407140
void RestoreFontSettings(void)
{
    FontDefault = SaveFontDefault;
    FontDestBuffer = SaveFontDestBuffer;
    FontDestPitch = SaveFontDestPitch;
    FontDestBPP = SaveFontDestBPP;
    FontDestRegion = SaveFontDestRegion;
    FontDestWrap = SaveFontDestWrap;
    FontForeground16 = SaveFontForeground16;
    FontShadow16 = SaveFontShadow16;
    FontBackground16 = SaveFontBackground16;
    FontForeground8 = SaveFontForeground8;
    FontBackground8 = SaveFontBackground8;
}

// GetHeight
//	Returns the height of a given character in the font.

UINT32 GetHeight(HVOBJECT hSrcVObject, INT16 ssIndex)
{
    ETRLEObject* pTrav;

    // Assertions
    Assert(hSrcVObject != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[ssIndex]);
    return ((UINT32)(pTrav->usHeight + pTrav->sOffsetY));
}
// GetFontHeight
//	Returns the height of the first character in a font.

// FUNCTION: WIZ8 0x004071f0
UINT16 GetFontHeight(INT32 FontNum)
{
    Assert(FontNum >= 0);
    Assert(FontNum <= MAX_FONTS);
    Assert(FontObjs[FontNum] != nullptr);

    return ((UINT16)GetHeight(FontObjs[FontNum].get(), 0));
}

// GetIndex
//		Given a word-sized character, this function returns the index of the
//	cell in the font to print to the screen. The conversion table is built by
//	CreateEnglishTransTable()

INT16 GetIndex(UINT16 siChar)
{
    const auto glyph = std::find(g_font_translation.begin(), g_font_translation.end(), siChar);
    if (glyph != g_font_translation.end())
        return static_cast<INT16>(glyph - g_font_translation.begin());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Invalid character given %d", siChar);
    return 0;
}

// SetFont
//	Sets the current font number.

// FUNCTION: WIZ8 0x00407210
BOOLEAN SetFont(INT32 iFontIndex)
{
    Assert(iFontIndex >= 0);
    Assert(iFontIndex <= MAX_FONTS);
    Assert(FontObjs[iFontIndex] != nullptr);

    FontDefault = iFontIndex;
    return (TRUE);
}

// SetFontDestBuffer
//	Sets the destination buffer for printing to, the clipping rectangle, and
// sets the line wrap on/off. DestBuffer is a VOBJECT handle, not a pointer.

// FUNCTION: WIZ8 0x00407220
BOOLEAN SetFontDestBuffer(UINT32 DestBuffer, INT32 x1, INT32 y1, INT32 x2, INT32 y2, BOOLEAN wrap)
{
    Assert(x2 > x1);
    Assert(y2 > y1);

    FontDestBuffer = DestBuffer;

    FontDestRegion.iLeft = x1;
    FontDestRegion.iTop = y1;
    FontDestRegion.iRight = x2;
    FontDestRegion.iBottom = y2;
    FontDestWrap = wrap;

    return (TRUE);
}

// mprintf
//	Prints to the currently selected destination buffer, at the X/Y coordinates
// specified, using the currently selected font. Other than the X/Y coordinates,
// the parameters are identical to printf. The resulting string may be no longer
// than 512 word-characters. Uses monochrome font color settings

// FUNCTION: WIZ8 0x00407260
UINT32 mprintf(INT32 x, INT32 y, const CHAR16* pFontString, ...)
{
    INT32 destx, desty;
    CHAR16 *curletter, transletter;
    va_list argptr;
    wchar_t string[512];
    UINT32 uiDestPitchBYTES;
    UINT8* pDestBuf;

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    curletter = string;

    destx = x;
    desty = y;

    // Lock the dest buffer
    pDestBuf = LockVideoSurface(FontDestBuffer, &uiDestPitchBYTES);

    while (ReadFontCharacter(curletter) != 0) {
        transletter = GetIndex(ReadFontCharacter(curletter++));

        if (FontDestWrap &&
            BltIsClipped(FontObjs[FontDefault].get(), destx, desty, transletter, &FontDestRegion)) {
            destx = x;
            desty += GetHeight(FontObjs[FontDefault].get(), transletter);
        }

        // Blit directly
        if (gbPixelDepth == 8) {
            Blt8BPPDataTo8BPPBufferMonoShadowClip(pDestBuf, uiDestPitchBYTES, FontObjs[FontDefault].get(),
                                                  destx, desty, transletter, &FontDestRegion,
                                                  FontForeground8, FontBackground8);
        } else {
            Blt8BPPDataTo16BPPBufferMonoShadowClip(
                (UINT16*)pDestBuf, uiDestPitchBYTES, FontObjs[FontDefault].get(), destx, desty,
                transletter, &FontDestRegion, FontForeground16, FontBackground16, FontShadow16);
        }
        destx += GetWidth(FontObjs[FontDefault].get(), transletter);
    }

    // Unlock buffer
    UnLockVideoSurface(FontDestBuffer);

    return (0);
}

// FUNCTION: WIZ8 0x00407420
void VarFindFontRightCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight,
                                 INT32 iFontIndex, INT16* psNewX, INT16* psNewY,
                                 const CHAR16* pFontString, ...)
{
    wchar_t string[512];
    va_list argptr;

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    FindFontRightCoordinates(sLeft, sTop, sWidth, sHeight, string, iFontIndex, psNewX, psNewY);
}

// FUNCTION: WIZ8 0x00407530
void VarFindFontCenterCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight,
                                  INT32 iFontIndex, INT16* psNewX, INT16* psNewY,
                                  const CHAR16* pFontString, ...)
{
    wchar_t string[512];
    va_list argptr;

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    FindFontCenterCoordinates(sLeft, sTop, sWidth, sHeight, string, iFontIndex, psNewX, psNewY);
}

void FindFontRightCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, CHAR16* pStr,
                              INT32 iFontIndex, INT16* psNewX, INT16* psNewY)
{
    INT16 xp, yp;

    // Compute the coordinates to right justify the text
    xp = ((sWidth - StringPixLength(pStr, iFontIndex))) + sLeft;
    yp = ((sHeight - GetFontHeight(iFontIndex)) / 2) + sTop;

    *psNewX = xp;
    *psNewY = yp;
}

void FindFontCenterCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, CHAR16* pStr,
                               INT32 iFontIndex, INT16* psNewX, INT16* psNewY)
{
    INT16 xp, yp;

    // Compute the coordinates to center the text
    xp = ((sWidth - StringPixLength(pStr, iFontIndex) + 1) / 2) + sLeft;
    yp = ((sHeight - GetFontHeight(iFontIndex)) / 2) + sTop;

    *psNewX = xp;
    *psNewY = yp;
}

// gprintf
//	Prints to the currently selected destination buffer, at the X/Y coordinates
// specified, using the currently selected font. Other than the X/Y coordinates,
// the parameters are identical to printf. The resulting string may be no longer
// than 512 word-characters.

// FUNCTION: WIZ8 0x00407650
UINT32 gprintf(INT32 x, INT32 y, const CHAR16* pFontString, ...)
{
    INT32 destx, desty;
    CHAR16 *curletter, transletter;
    va_list argptr;
    wchar_t string[512];
    UINT32 uiDestPitchBYTES;
    UINT8* pDestBuf;

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    curletter = string;

    destx = x;
    desty = y;

    // Lock the dest buffer
    pDestBuf = LockVideoSurface(FontDestBuffer, &uiDestPitchBYTES);

    while (ReadFontCharacter(curletter) != 0) {
        transletter = GetIndex(ReadFontCharacter(curletter++));

        if (FontDestWrap &&
            BltIsClipped(FontObjs[FontDefault].get(), destx, desty, transletter, &FontDestRegion)) {
            destx = x;
            desty += GetHeight(FontObjs[FontDefault].get(), transletter);
        }

        // Blit directly
        if (gbPixelDepth == 8) {
            Blt8BPPDataTo8BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                   FontObjs[FontDefault].get(), destx, desty, transletter,
                                                   &FontDestRegion);
        } else {
            Blt8BPPDataTo16BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                    FontObjs[FontDefault].get(), destx, desty,
                                                    transletter, &FontDestRegion);
        }
        destx += GetWidth(FontObjs[FontDefault].get(), transletter);
    }

    // Unlock buffer
    UnLockVideoSurface(FontDestBuffer);

    return (0);
}

// FUNCTION: WIZ8 0x004077d0
UINT32 gprintfDirty(INT32 x, INT32 y, const CHAR16* pFontString, ...)
{
    INT32 destx, desty;
    CHAR16 *curletter, transletter;
    va_list argptr;
    wchar_t string[512];
    UINT32 uiDestPitchBYTES;
    UINT8* pDestBuf;

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    curletter = string;

    destx = x;
    desty = y;

    // Lock the dest buffer
    pDestBuf = LockVideoSurface(FontDestBuffer, &uiDestPitchBYTES);

    while (ReadFontCharacter(curletter) != 0) {
        transletter = GetIndex(ReadFontCharacter(curletter++));

        if (FontDestWrap &&
            BltIsClipped(FontObjs[FontDefault].get(), destx, desty, transletter, &FontDestRegion)) {
            destx = x;
            desty += GetHeight(FontObjs[FontDefault].get(), transletter);
        }

        // Blit directly
        if (gbPixelDepth == 8) {
            Blt8BPPDataTo8BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                   FontObjs[FontDefault].get(), destx, desty, transletter,
                                                   &FontDestRegion);
        } else {
            Blt8BPPDataTo16BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                    FontObjs[FontDefault].get(), destx, desty,
                                                    transletter, &FontDestRegion);
        }
        destx += GetWidth(FontObjs[FontDefault].get(), transletter);
    }

    // Unlock buffer
    UnLockVideoSurface(FontDestBuffer);

    InvalidateRegion(x, y, x + StringPixLength(string, FontDefault), y + GetFontHeight(FontDefault),
                     INVAL_SRC_TRANS);

    return (0);
}

// gprintf_buffer
//	Prints to the currently selected destination buffer, at the X/Y coordinates
// specified, using the currently selected font. Other than the X/Y coordinates,
// the parameters are identical to printf. The resulting string may be no longer
// than 512 word-characters.

// FUNCTION: WIZ8 0x00407a10
UINT32 gprintf_buffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, UINT32 FontType, INT32 x, INT32 y,
                      const CHAR16* pFontString, ...)
{
    INT32 destx, desty;
    CHAR16 *curletter, transletter;
    va_list argptr;
    wchar_t string[512];

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    curletter = string;

    destx = x;
    desty = y;

    while (ReadFontCharacter(curletter) != 0) {
        transletter = GetIndex(ReadFontCharacter(curletter++));

        if (FontDestWrap &&
            BltIsClipped(FontObjs[FontType].get(), destx, desty, transletter, &FontDestRegion)) {
            destx = x;
            desty += GetHeight(FontObjs[FontType].get(), transletter);
        }

        // Blit directly
        if (gbPixelDepth == 8) {
            Blt8BPPDataTo8BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                   FontObjs[FontDefault].get(), destx, desty, transletter,
                                                   &FontDestRegion);
        } else {
            Blt8BPPDataTo16BPPBufferTransparentClip((UINT16*)pDestBuf, uiDestPitchBYTES,
                                                    FontObjs[FontDefault].get(), destx, desty,
                                                    transletter, &FontDestRegion);
        }

        destx += GetWidth(FontObjs[FontType].get(), transletter);
    }

    return (0);
}

// FUNCTION: WIZ8 0x00407b80
UINT32 mprintf_buffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, UINT32 FontType, INT32 x, INT32 y,
                      const CHAR16* pFontString, ...)
{
    INT32 destx, desty;
    CHAR16 *curletter, transletter;
    va_list argptr;
    wchar_t string[512];

    Assert(pFontString != nullptr);

    va_start(argptr, pFontString);          // Set up variable argument pointer
    vswprintf(string, pFontString, argptr); // process gprintf string (get output str)
    va_end(argptr);

    curletter = string;

    destx = x;
    desty = y;

    while (ReadFontCharacter(curletter) != 0) {
        transletter = GetIndex(ReadFontCharacter(curletter++));

        if (FontDestWrap &&
            BltIsClipped(FontObjs[FontDefault].get(), destx, desty, transletter, &FontDestRegion)) {
            destx = x;
            desty += GetHeight(FontObjs[FontDefault].get(), transletter);
        }

        // Blit directly
        if (gbPixelDepth == 8) {
            Blt8BPPDataTo8BPPBufferMonoShadowClip(pDestBuf, uiDestPitchBYTES, FontObjs[FontDefault].get(),
                                                  destx, desty, transletter, &FontDestRegion,
                                                  FontForeground8, FontBackground8);
        } else {
            Blt8BPPDataTo16BPPBufferMonoShadowClip(
                (UINT16*)pDestBuf, uiDestPitchBYTES, FontObjs[FontDefault].get(), destx, desty,
                transletter, &FontDestRegion, FontForeground16, FontBackground16, FontShadow16);
        }
        destx += GetWidth(FontObjs[FontDefault].get(), transletter);
    }

    return (0);
}

// InitializeFontManager
//	Starts up the font manager system with the appropriate translation table.

// FUNCTION: WIZ8 0x00407d30
BOOLEAN InitializeFontManager(const std::vector<UINT16>& translation)
{
    UINT16 uiRight, uiBottom;
    UINT8 uiPixelDepth;

    FontDefault = (-1);
    FontDestBuffer = BACKBUFFER;
    FontDestPitch = 0;

    //	FontDestBPP=0;

    GetCurrentVideoSettings(&uiRight, &uiBottom, &uiPixelDepth);
    FontDestRegion.iLeft = 0;
    FontDestRegion.iTop = 0;
    FontDestRegion.iRight = (INT32)uiRight;
    FontDestRegion.iBottom = (INT32)uiBottom;
    FontDestBPP = (UINT32)uiPixelDepth;

    FontDestWrap = FALSE;

    ShutdownFontManager();
    g_font_translation = translation;

    return TRUE;
}

// ShutdownFontManager
//	Shuts down, and deallocates all fonts.

// FUNCTION: WIZ8 0x00407e30
void ShutdownFontManager(void)
{
    for (auto& font : FontObjs)
        font.reset();
    g_font_translation.clear();
    FontDefault = SaveFontDefault = -1;
}

// CreateEnglishTransTable
// Creates the English text->font map table.

// FUNCTION: WIZ8 0x00407ec0
std::vector<UINT16> CreateEnglishTransTable()
{
    return {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
        'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
        'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
        'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
        'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', '!', '@', '#', '$', '%', '^', '&', '*',
        '(', ')', '-', '_', '+', '=', '|', '\\', '{', '}',
        '[', ']', ':', ';', '"', '\'', '<', '>', ',', '.',
        '?', '/', ' ', 193, 192, 193, 196, 195, 197, 199,
        201, 200, 202, 203, 205, 204, 206, 207, 209, 211,
        210, 212, 214, 213, 216, 218, 217, 219, 220, 221,
        225, 224, 226, 228, 227, 229, 231, 233, 232, 234,
        235, 237, 236, 238, 239, 241, 243, 242, 244, 246,
        245, 248, 250, 249, 251, 252, 254, 255, 223, FONT_GLYPH_TARGET_POINT,
        FONT_GLYPH_TARGET_CONE, FONT_GLYPH_TARGET_SINGLE, FONT_GLYPH_TARGET_GROUP, FONT_GLYPH_TARGET_NONE, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        191, 161
    };
}
// LoadFontFile
// Parameter List : filename - File created by the utility tool to open
// Return Value  pointer to the base structure
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry
// GetMaxFontWidth - Gets the maximum font width
// Parameter List : pointer to the base structure
// Return Value  Maximum font width
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry
// ConvertToPaletteEntry
// Parameter List : Converts from RGB to SGPPaletteEntry
// Return Value  pointer to the SGPPaletteEntry
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry
// SetFontPalette - Sets the Palette
// Parameter List : pointer to the base structure
//                  new pixel depth
//                  new Palette size
//                  pointer to palette data
// Return Value  BOOLEAN
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry
// SetFont16BitData - Sets the font structure to hold 16 bit data
// Parameter List : pointer to the base structure
//                  pointer to new 16 bit data
// Return Value  BOOLEAN
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry
// Blt8Imageto16Dest
// Parameter List : Start offset
//                  End Offset
//                  Dest x, y
//                  Font Width
//                  Pointer to Base structure
//                  Pointer to destination buffer
//                  Destination Pitch
//                  Height of Each element
// Return Value  : BOOLEAN
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry
// Blt8Imageto8Dest
// Parameter List : Start offset
//                  End Offset
//                  Dest x, y
//                  Font Width
//                  Pointer to Base structure
//                  Pointer to destination buffer
//                  Destination Pitch
//                  Height of Each element
// Return Value  : BOOLEAN
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry
// Blt16Imageto16Dest
// Parameter List : Start offset
//                  End Offset
//                  Dest x, y
//                  Font Width
//                  Pointer to Base structure
//                  Pointer to destination buffer
//                  Destination Pitch
//                  Height of Each element
// Return Value  : BOOLEAN
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry
// GetOffset
// Parameter List : Given the index, gets the corresponding offset
// Return Value  : offset
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry
// GetOffLen
// Parameter List : Given the index, gets the corresponding offset
// length which is the number of compressed pixels
// Return Value  : offset
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry
// PrintFontString
// Parameter List : pointer to \0 (NULL) terminated font string
//                  x,y,TotalWidth, TotalHeight is the bounding rectangle where
//                  the font is to be printed
//                  Multiline if true will print on multiple lines otherwise on 1 line
//                  Pointer to base structure
// Return Value  : BOOLEAN
// Modification History :
// Nov 26th 1996 -> modified for use by Wizardry

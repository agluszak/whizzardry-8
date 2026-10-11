#include <utility>
#include <SDL3/SDL_rect.h>
#include <cstddef>
#include <string>
#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include "wiz8/unicode.h"
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
// font.c
#include "Types.h"
#include "Font.h"
#include "Video2.h"
#include "himage.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "vsurface.h"



static std::vector<UINT16> g_font_translation;
FontRenderState g_font_render_state;
static FontRenderState saved_font_state;

Font::Font(Font&& other) noexcept : glyphs(std::move(other.glyphs))
{
    other.clear();
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other) {
        clear();
        glyphs = std::move(other.glyphs);
        other.clear();
    }
    return *this;
}

Font::~Font()
{
    clear();
}

void Font::clear()
{
    if (g_font_render_state.font == this) g_font_render_state.font = nullptr;
    if (saved_font_state.font == this) saved_font_state.font = nullptr;
    glyphs = {};
}

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

    if (!g_font_render_state.font)
        return;

    g_font_render_state.foreground_index = ubForeground;

    uiRed = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubForeground].peRed;
    uiGreen = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubForeground].peGreen;
    uiBlue = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubForeground].peBlue;

    g_font_render_state.foreground = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}

// FUNCTION: WIZ8 0x00406c90
void SetFontShadow(UINT8 ubShadow)
{
    UINT32 uiRed, uiGreen, uiBlue;

    if (!g_font_render_state.font)
        return;

    //g_font_render_state.foreground_index=ubForeground;

    uiRed = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubShadow].peRed;
    uiGreen = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubShadow].peGreen;
    uiBlue = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubShadow].peBlue;

    g_font_render_state.shadow = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));

    if (ubShadow != 0) {
        if (g_font_render_state.shadow == 0) {
            g_font_render_state.shadow = 1;
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

    if (!g_font_render_state.font)
        return;

    g_font_render_state.background_index = ubBackground;

    uiRed = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubBackground].peRed;
    uiGreen = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubBackground].peGreen;
    uiBlue = (UINT32)g_font_render_state.font->glyphs.pPaletteEntry[ubBackground].peBlue;

    g_font_render_state.background = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}

// FUNCTION: WIZ8 0x00406d80
void SetRGBFontShadow(UINT32 uiRed, UINT32 uiGreen, UINT32 uiBlue)
{
    if (!g_font_render_state.font)
        return;
    g_font_render_state.shadow = Get16BPPColor(FROMRGB(uiRed, uiGreen, uiBlue));
}
//end Kris

// SetFontObjectPalette16BPP
//	Sets the palette of a font, using a 16 bit palette.

// FUNCTION: WIZ8 0x00406dc0
UINT16* SetFontObjectPalette16BPP(Font& iFont, UINT16* pPal16)
{

    iFont.glyphs.p16BPPPalette = pPal16;
    iFont.glyphs.pShadeCurrent = pPal16;

    return (pPal16);
}

// GetFontObjectPalette16BPP
//	Sets the palette of a font, using a 16 bit palette.

// FUNCTION: WIZ8 0x00406de0
UINT16* GetFontObjectPalette16BPP(Font& iFont)
{

    return (iFont.glyphs.p16BPPPalette);
}

// FUNCTION: WIZ8 0x00406e00
bool Font::load(std::string_view filename)
{
    VOBJECT_DESC desc;
    desc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
    desc.ImageFile = filename;
    auto object = CreateVideoObject(desc);
    if (!object) return false;
    clear();
    glyphs = std::move(*object);
    return true;
}

// GetWidth
//	Returns the width of a given character in the font.

UINT32 GetWidth(const SGPVObject& hSrcVObject, INT16 ssIndex)
{
    const SpriteFrame* pTrav;

    // Assertions

    if (ssIndex < 0 || ssIndex > 92) {
    }

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject.sprites[ssIndex]);
    return ((UINT32)(pTrav->usWidth + pTrav->sOffsetX));
}

// Measure a prefix counted in Unicode scalar values.

// FUNCTION: WIZ8 0x00406ea0
INT16 Font::codepointPrefixWidth(std::string_view string, UINT32 uiCharCount) const
{

    return textWidth(string.substr(0, wiz8::text::codepoint_prefix(string, uiCharCount)));
}
//  StringNPixLength
//  Return the length of the of the string or count characters in the
//  string, which ever comes first.
//  Returns INT16
//  Created by:     Gilles Beauparlant
//  Created on:     12/1/99

// FUNCTION: WIZ8 0x00406f90
INT16 Font::prefixWidth(std::string_view string, std::size_t byte_end) const
{
    if (glyphs.sprites.empty()) return 0;
    UINT32 width = 0;
    for (std::size_t offset = 0; offset < string.size() && offset < byte_end;) {
        const auto glyph = GetIndex(wiz8::text::next(string, offset));
        if (offset > byte_end) break;
        width += GetWidth(glyphs, glyph);
    }
    return static_cast<INT16>(width);
}

// FUNCTION: WIZ8 0x00407010
INT16 Font::textWidth(std::string_view string) const
{
    return prefixWidth(string, string.size());
}
// SaveFontSettings
//	Saves the current font printing settings into temporary locations.

// FUNCTION: WIZ8 0x00407090
void SaveFontSettings(void)
{
    saved_font_state = g_font_render_state;
}
// RestoreFontSettings
//	Restores the last saved font printing settings from the temporary lactions

// FUNCTION: WIZ8 0x00407140
void RestoreFontSettings(void)
{
    g_font_render_state = saved_font_state;
}

// GetHeight
//	Returns the height of a given character in the font.

UINT32 GetHeight(const SGPVObject& hSrcVObject, INT16 ssIndex)
{
    const SpriteFrame* pTrav;

    // Assertions

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject.sprites[ssIndex]);
    return ((UINT32)(pTrav->usHeight + pTrav->sOffsetY));
}
// GetFontHeight
//	Returns the height of the first character in a font.

// FUNCTION: WIZ8 0x004071f0
UINT16 Font::height() const
{
    if (glyphs.sprites.empty()) return 0;
    return ((UINT16)GetHeight(glyphs, 0));
}

// GetIndex
//		Given a word-sized character, this function returns the index of the
//	cell in the font to print to the screen. The conversion table is built by
//	CreateEnglishTransTable()

INT16 GetIndex(char32_t siChar)
{
    const auto glyph = std::find(g_font_translation.begin(), g_font_translation.end(), siChar);
    if (glyph != g_font_translation.end())
        return static_cast<INT16>(glyph - g_font_translation.begin());
    const auto fallback = std::find(g_font_translation.begin(), g_font_translation.end(), '?');
    return fallback == g_font_translation.end() ? 0 : static_cast<INT16>(fallback - g_font_translation.begin());
}

// SetFont
//	Selects a non-owning reference to the font resource.

// FUNCTION: WIZ8 0x00407210
BOOLEAN SetFont(Font& iFontIndex)
{

    g_font_render_state.font = &iFontIndex;
    return (TRUE);
}

// SetFontDestBuffer
//	Sets the destination buffer for printing to, the clipping rectangle, and
// sets the line wrap on/off. The destination is a non-owning surface reference.

// FUNCTION: WIZ8 0x00407220
BOOLEAN SetFontDestBuffer(SGPVSurface& DestBuffer, INT32 x1, INT32 y1, INT32 x2, INT32 y2, BOOLEAN wrap)
{
    Assert(x2 > x1);
    Assert(y2 > y1);

    g_font_render_state.destination = &DestBuffer;

    g_font_render_state.clip = {x1, y1, x2 - x1, y2 - y1};
    g_font_render_state.wrap = wrap;

    return (TRUE);
}

// One bounded UTF-8 traversal for surface and buffer rendering.
static void DrawTextToBuffer(UINT8* buffer, UINT32 pitch, Font& font_index, INT32 x, INT32 y,
                             std::string_view text, bool mono)
{
    auto& font = font_index.glyphs;
    if (!buffer) return;
    INT32 destx = x;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto glyph = GetIndex(wiz8::text::next(text, offset));
        if (g_font_render_state.wrap && BltIsClipped(font, destx, y, glyph, &g_font_render_state.clip)) {
            destx = x;
            y += GetHeight(font, glyph);
        }
        BltSpriteTo16BPPBuffer(reinterpret_cast<UINT16*>(buffer), pitch, font.sprites[glyph],
                              font.pShadeCurrent, destx, y, &g_font_render_state.clip, false,
                              mono ? SpriteDrawOp::MonoShadow : SpriteDrawOp::Transparent,
                              g_font_render_state.foreground, g_font_render_state.background, g_font_render_state.shadow);
        destx += GetWidth(font, glyph);
    }
}

static UINT32 DrawTextToSurface(INT32 x, INT32 y, std::string_view text, bool mono)
{
    if (!g_font_render_state.font || !g_font_render_state.destination) return 0;
    UINT32 pitch;
    auto* buffer = LockVideoSurfaceBuffer(*g_font_render_state.destination, &pitch);
    DrawTextToBuffer(buffer, pitch, *g_font_render_state.font, x, y, text, mono);
    UnLockVideoSurfaceBuffer(*g_font_render_state.destination);
    return 0;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407260
UINT32 DrawMonoFontText(INT32 x, INT32 y, std::string_view string)
{
    return DrawTextToSurface(x, y, string, true);
}

// FUNCTION: WIZ8 0x00407420
void FindFontRightCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, std::string_view pStr,
                              Font& iFontIndex, INT16* psNewX, INT16* psNewY)
{
    INT16 xp, yp;

    // Compute the coordinates to right justify the text
    xp = ((sWidth - iFontIndex.textWidth(pStr))) + sLeft;
    yp = ((sHeight - iFontIndex.height()) / 2) + sTop;

    *psNewX = xp;
    *psNewY = yp;
}

// FUNCTION: WIZ8 0x00407530
void FindFontCenterCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, std::string_view pStr,
                               Font& iFontIndex, INT16* psNewX, INT16* psNewY)
{
    INT16 xp, yp;

    // Compute the coordinates to center the text
    xp = ((sWidth - iFontIndex.textWidth(pStr) + 1) / 2) + sLeft;
    yp = ((sHeight - iFontIndex.height()) / 2) + sTop;

    *psNewX = xp;
    *psNewY = yp;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407650
UINT32 DrawFontText(INT32 x, INT32 y, std::string_view string)
{
    return DrawTextToSurface(x, y, string, false);
}

// FUNCTION: WIZ8 0x004077d0
UINT32 DrawFontTextDirty(INT32 x, INT32 y, std::string_view string)
{
    if (!g_font_render_state.font) return 0;
    DrawFontText(x, y, string);
    InvalidateRegion(x, y, x + g_font_render_state.font->textWidth(string), y + g_font_render_state.font->height(),
                     INVAL_SRC_TRANS);
    return 0;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407a10
UINT32 DrawFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                            INT32 y, std::string_view string)
{
    DrawTextToBuffer(pDestBuf, uiDestPitchBYTES, FontType, x, y, string, false);
    return 0;
}

// FUNCTION: WIZ8 0x00407b80
UINT32 DrawMonoFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                                INT32 y, std::string_view string)
{
    DrawTextToBuffer(pDestBuf, uiDestPitchBYTES, FontType, x, y, string, true);
    return 0;
}

// FUNCTION: WIZ8 0x00407d30
void ConfigureFontRendering(const std::vector<UINT16>& translation)
{
    ResetFontRendering();
    g_font_translation = translation;
    g_font_render_state.destination = &GetFrameBufferSurface();
    UINT16 width, height;
    UINT8 depth;
    GetCurrentVideoSettings(&width, &height, &depth);
    g_font_render_state.clip = {0, 0, width, height};
}

// FUNCTION: WIZ8 0x00407e30
void ResetFontRendering()
{
    g_font_render_state = {};
    saved_font_state = {};
    g_font_translation.clear();
}

void SetFontDestClip(INT32 x1, INT32 y1, INT32 x2, INT32 y2)
{
    SetFontDestBuffer(*g_font_render_state.destination, x1, y1, x2, y2, g_font_render_state.wrap);
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

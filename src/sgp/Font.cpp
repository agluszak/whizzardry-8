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
FontRenderScope* FontRenderScope::current = nullptr;

Font::Font(Font&& other) noexcept
    : glyphs(std::move(other.glyphs)), customPalette(std::move(other.customPalette))
{
    other.clear();
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other) {
        clear();
        glyphs = std::move(other.glyphs);
        customPalette = std::move(other.customPalette);
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
    for (auto* scope = FontRenderScope::current; scope; scope = scope->previous)
        if (scope->saved.font == this) scope->saved.font = nullptr;
    glyphs = {};
    customPalette.reset();
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
    if (!g_font_render_state.font)
        return;

    g_font_render_state.foreground_index = ubForeground;

    g_font_render_state.foreground =
        Get16BPPColor(g_font_render_state.font->glyphs.pPaletteEntry[ubForeground]);
}

// FUNCTION: WIZ8 0x00406c90
void SetFontShadow(UINT8 ubShadow)
{
    if (!g_font_render_state.font)
        return;

    //g_font_render_state.foreground_index=ubForeground;

    g_font_render_state.shadow =
        Get16BPPColor(g_font_render_state.font->glyphs.pPaletteEntry[ubShadow]);

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
    if (!g_font_render_state.font)
        return;

    g_font_render_state.background_index = ubBackground;

    g_font_render_state.background =
        Get16BPPColor(g_font_render_state.font->glyphs.pPaletteEntry[ubBackground]);
}

// FUNCTION: WIZ8 0x00406d80
void SetRGBFontShadow(UINT32 uiRed, UINT32 uiGreen, UINT32 uiBlue)
{
    if (!g_font_render_state.font)
        return;
    g_font_render_state.shadow = Get16BPPColor(SDL_Color{static_cast<UINT8>(uiRed),
        static_cast<UINT8>(uiGreen), static_cast<UINT8>(uiBlue), SDL_ALPHA_OPAQUE});
}
//end Kris

// FUNCTION: WIZ8 0x00406dc0
void Font::setPalette(const Palette16& colors)
{
    customPalette = colors;
    glyphs.currentShade = 4;
}

// FUNCTION: WIZ8 0x00406de0
const Palette16& Font::palette() const
{
    return customPalette ? *customPalette : glyphs.palette;
}

const Palette16& Font::currentPalette() const
{
    return glyphs.currentShade == 4 ? palette() : glyphs.currentPalette();
}

void Font::resetPalette()
{
    glyphs.currentShade = 4;
    customPalette.reset();
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

static UINT32 GetWidth(const SGPVObject& hSrcVObject, INT16 ssIndex)
{
    const auto& glyph = hSrcVObject.sprites[ssIndex];
    return glyph.usWidth + glyph.sOffsetX;
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
// FUNCTION: WIZ8 0x00407090
FontRenderScope::FontRenderScope() : saved(g_font_render_state), previous(current)
{
    current = this;
}

// FUNCTION: WIZ8 0x00407140
FontRenderScope::~FontRenderScope()
{
    Assert(current == this);
    g_font_render_state = saved;
    current = previous;
}

// GetHeight
//	Returns the height of a given character in the font.

static UINT32 GetHeight(const SGPVObject& hSrcVObject, INT16 ssIndex)
{
    const auto& glyph = hSrcVObject.sprites[ssIndex];
    return glyph.usHeight + glyph.sOffsetY;
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
void SetFont(Font& font)
{

    g_font_render_state.font = &font;
}

// SetFontDestBuffer
//	Sets the destination buffer for printing to, the clipping rectangle, and
// sets the line wrap on/off. The destination is a non-owning surface reference.

// FUNCTION: WIZ8 0x00407220
void SetFontDestBuffer(SGPVSurface& DestBuffer, INT32 x1, INT32 y1, INT32 x2, INT32 y2, bool wrap)
{
    Assert(x2 > x1);
    Assert(y2 > y1);

    g_font_render_state.destination = &DestBuffer;

    g_font_render_state.clip = {x1, y1, x2 - x1, y2 - y1};
    g_font_render_state.wrap = wrap;

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
                              font_index.currentPalette().data(), destx, y, &g_font_render_state.clip, false,
                              mono ? SpriteDrawOp::MonoShadow : SpriteDrawOp::Transparent,
                              g_font_render_state.foreground, g_font_render_state.background, g_font_render_state.shadow);
        destx += GetWidth(font, glyph);
    }
}

static void DrawTextToSurface(INT32 x, INT32 y, std::string_view text, bool mono)
{
    if (!g_font_render_state.font || !g_font_render_state.destination) return;
    const auto& storage = *g_font_render_state.destination->cpu().surface;
    DrawTextToBuffer(static_cast<BYTE*>(storage.pixels), storage.pitch, *g_font_render_state.font, x, y, text, mono);
    return;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407260
void DrawMonoFontText(INT32 x, INT32 y, std::string_view string)
{
    DrawTextToSurface(x, y, string, true);
}

// FUNCTION: WIZ8 0x00407420
SDL_Point Font::rightAlignedPosition(const SDL_Rect& bounds, std::string_view text) const
{
    return {bounds.x + bounds.w - textWidth(text), bounds.y + (bounds.h - height()) / 2};
}

// FUNCTION: WIZ8 0x00407530
SDL_Point Font::centeredPosition(const SDL_Rect& bounds, std::string_view text) const
{
    return {bounds.x + (bounds.w - textWidth(text) + 1) / 2,
            bounds.y + (bounds.h - height()) / 2};
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407650
void DrawFontText(INT32 x, INT32 y, std::string_view string)
{
    DrawTextToSurface(x, y, string, false);
}

// FUNCTION: WIZ8 0x004077d0
void DrawFontTextDirty(INT32 x, INT32 y, std::string_view string)
{
    if (!g_font_render_state.font) return;
    DrawFontText(x, y, string);
    InvalidateRegion(x, y, x + g_font_render_state.font->textWidth(string), y + g_font_render_state.font->height(),
                     INVAL_SRC_TRANS);
    return;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407a10
void DrawFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                            INT32 y, std::string_view string)
{
    DrawTextToBuffer(pDestBuf, uiDestPitchBYTES, FontType, x, y, string, false);
    return;
}

// FUNCTION: WIZ8 0x00407b80
void DrawMonoFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                                INT32 y, std::string_view string)
{
    DrawTextToBuffer(pDestBuf, uiDestPitchBYTES, FontType, x, y, string, true);
    return;
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
    for (auto* scope = FontRenderScope::current; scope; scope = scope->previous)
        scope->saved = {};
    g_font_translation.clear();
}

void SetFontDestClip(INT32 x1, INT32 y1, INT32 x2, INT32 y2)
{
    Assert(x2 > x1 && y2 > y1);
    g_font_render_state.clip = {x1, y1, x2 - x1, y2 - y1};
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

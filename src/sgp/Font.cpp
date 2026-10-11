#include <utility>
#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>
#include <SDL3/SDL_surface.h>

#include "wiz8/unicode.h"
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
// font.c
#include "Font.h"
#include "Video2.h"
#include "himage.h"
#include "image.h"
#include "image_assets.h"
#include "sprite.h"
#include "Types.h"

static std::vector<std::uint16_t> g_font_translation;
FontRenderState g_font_render_state;
FontRenderScope* FontRenderScope::current = nullptr;

Font::Font(Font&& other) noexcept
    : glyphs(std::move(other.glyphs)), customPalette(std::move(other.customPalette)),
      shades(std::move(other.shades)), currentShade(other.currentShade)
{
    other.clear();
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other) {
        clear();
        glyphs = std::move(other.glyphs);
        customPalette = std::move(other.customPalette);
        shades = std::move(other.shades);
        currentShade = other.currentShade;
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
    shades.clear();
    currentShade = kNormalShade;
}

// SetFontForeground
//	Sets the foreground color of the currently selected font. The parameter is
// the index into the 8-bit palette. In 8BPP mode, that index number is used
// for the pixel value to be drawn for nontransparent pixels. In 16BPP mode,
// the RGB values from the palette are used to create the pixel color. Note
// that if you change fonts, the selected foreground/background colors will
// stay at what they are currently set to.

// FUNCTION: WIZ8 0x00406c20
void SetFontForeground(std::uint8_t ubForeground)
{
    if (!g_font_render_state.font)
        return;

    g_font_render_state.foreground_index = ubForeground;

    g_font_render_state.foreground =
        Get16BPPColor(g_font_render_state.font->glyphs->palette[ubForeground]);
}

// FUNCTION: WIZ8 0x00406c90
void SetFontShadow(std::uint8_t ubShadow)
{
    if (!g_font_render_state.font)
        return;

    //g_font_render_state.foreground_index=ubForeground;

    g_font_render_state.shadow =
        Get16BPPColor(g_font_render_state.font->glyphs->palette[ubShadow]);

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
void SetFontBackground(std::uint8_t ubBackground)
{
    if (!g_font_render_state.font)
        return;

    g_font_render_state.background_index = ubBackground;

    g_font_render_state.background =
        Get16BPPColor(g_font_render_state.font->glyphs->palette[ubBackground]);
}

// FUNCTION: WIZ8 0x00406d80
void SetRGBFontShadow(SDL_Color color)
{
    if (!g_font_render_state.font)
        return;
    g_font_render_state.shadow = Get16BPPColor(color);
}
//end Kris

// FUNCTION: WIZ8 0x00406dc0
void Font::setPalette(const Palette16& colors)
{
    customPalette = colors;
    currentShade = kNormalShade;
}

// FUNCTION: WIZ8 0x00406de0
const Palette16& Font::palette() const
{
    static const Palette16 empty{};
    return customPalette ? *customPalette : glyphs ? glyphs->colors : empty;
}

const Palette16& Font::currentPalette() const
{
    return currentShade == kNormalShade ? palette() : shades.at(currentShade).value();
}

void Font::resetPalette()
{
    currentShade = kNormalShade;
    customPalette.reset();
}

// FUNCTION: WIZ8 0x00406460
std::uint16_t Font::createShades(std::uint32_t glow)
{
    if (!glyphs)
        return false;
    shades.clear();
    shades.resize(16);
    switch (glow) {
    case FONT_GLOW_GREEN:
        shades[0] = Create16BPPPaletteShaded(glyphs->palette, 0, 255, 0, true);
        break;
    case FONT_GLOW_BLUE:
        shades[0] = Create16BPPPaletteShaded(glyphs->palette, 0, 0, 255, true);
        break;
    case FONT_GLOW_YELLOW:
        shades[0] = Create16BPPPaletteShaded(glyphs->palette, 255, 255, 0, true);
        break;
    case FONT_GLOW_RED:
        shades[0] = Create16BPPPaletteShaded(glyphs->palette, 255, 0, 0, true);
        break;
    }

    shades[1] = Create16BPPPaletteShaded(glyphs->palette, 293, 293, 293, false);
    shades[2] = Create16BPPPaletteShaded(glyphs->palette, 281, 281, 281, false);
    shades[3] = Create16BPPPaletteShaded(glyphs->palette, 268, 268, 268, false);
    shades[5] = Create16BPPPaletteShaded(glyphs->palette, 195, 195, 195, false);
    shades[6] = Create16BPPPaletteShaded(glyphs->palette, 165, 165, 165, false);
    shades[7] = Create16BPPPaletteShaded(glyphs->palette, 135, 135, 135, false);
    shades[8] = Create16BPPPaletteShaded(glyphs->palette, 105, 105, 105, false);
    shades[9] = Create16BPPPaletteShaded(glyphs->palette, 75, 75, 75, false);
    shades[10] = Create16BPPPaletteShaded(glyphs->palette, 45, 45, 45, false);
    shades[11] = Create16BPPPaletteShaded(glyphs->palette, 36, 36, 36, false);
    shades[12] = Create16BPPPaletteShaded(glyphs->palette, 27, 27, 27, false);
    shades[13] = Create16BPPPaletteShaded(glyphs->palette, 18, 18, 18, false);
    shades[14] = Create16BPPPaletteShaded(glyphs->palette, 9, 9, 9, false);
    shades[15] = Create16BPPPaletteShaded(glyphs->palette, 0, 0, 0, false);
    currentShade = kNormalShade;
    return true;
}

// FUNCTION: WIZ8 0x004068e0
std::uint16_t Font::setShade(std::uint32_t shade)
{
    if (shade != kNormalShade && (shade >= shades.size() || !shades[shade]))
        return false;
    currentShade = shade;
    return true;
}

// FUNCTION: WIZ8 0x00406e00
bool Font::load(std::string_view filename)
{
    auto sheet = g_image_assets.sprites(filename);
    if (!sheet)
        return false;
    clear();
    glyphs = std::move(sheet);
    return true;
}

// GetWidth
//	Returns the width of a given character in the font.

static std::uint32_t GetWidth(const SpriteSheet& sprites, std::int16_t ssIndex)
{
    const auto& glyph = sprites.frames[ssIndex];
    return glyph.usWidth + glyph.sOffsetX;
}

// Measure a prefix counted in Unicode scalar values.

// FUNCTION: WIZ8 0x00406ea0
std::int16_t Font::codepointPrefixWidth(std::string_view string, std::uint32_t uiCharCount) const
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
std::int16_t Font::prefixWidth(std::string_view string, std::size_t byte_end) const
{
    if (!glyphs || glyphs->frames.empty()) return 0;
    std::uint32_t width = 0;
    for (std::size_t offset = 0; offset < string.size() && offset < byte_end;) {
        const auto glyph = GetIndex(wiz8::text::next(string, offset));
        if (offset > byte_end) break;
        width += GetWidth(*glyphs, glyph);
    }
    return static_cast<std::int16_t>(width);
}

// FUNCTION: WIZ8 0x00407010
std::int16_t Font::textWidth(std::string_view string) const
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

static std::uint32_t GetHeight(const SpriteSheet& sprites, std::int16_t ssIndex)
{
    const auto& glyph = sprites.frames[ssIndex];
    return glyph.usHeight + glyph.sOffsetY;
}
// GetFontHeight
//	Returns the height of the first character in a font.

// FUNCTION: WIZ8 0x004071f0
std::uint16_t Font::height() const
{
    if (!glyphs || glyphs->frames.empty()) return 0;
    return static_cast<std::uint16_t>(GetHeight(*glyphs, 0));
}

// GetIndex
//		Given a word-sized character, this function returns the index of the
//	cell in the font to print to the screen. The conversion table is built by
//	CreateEnglishTransTable()

std::int16_t GetIndex(char32_t siChar)
{
    const auto glyph = std::find(g_font_translation.begin(), g_font_translation.end(), siChar);
    if (glyph != g_font_translation.end())
        return static_cast<std::int16_t>(glyph - g_font_translation.begin());
    const auto fallback = std::find(g_font_translation.begin(), g_font_translation.end(), '?');
    return fallback == g_font_translation.end() ? 0 : static_cast<std::int16_t>(fallback - g_font_translation.begin());
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
void SetFontDestBuffer(SDL_Surface& destination, std::int32_t x1, std::int32_t y1,
                       std::int32_t x2, std::int32_t y2, bool wrap)
{
    Assert(x2 > x1);
    Assert(y2 > y1);

    g_font_render_state.destination = &destination;
    g_font_render_state.clip = {x1, y1, x2 - x1, y2 - y1};
    g_font_render_state.wrap = wrap;
}

static void DrawTextToSurface(SDL_Surface& target, Font& font, std::int32_t x, std::int32_t y,
                              std::string_view text, bool mono)
{
    if (!font.glyphs)
        return;
    ScopedClipRect clip(target, g_font_render_state.clip);
    const auto& sprites = *font.glyphs;
    std::int32_t destx = x;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto glyph = GetIndex(wiz8::text::next(text, offset));
        if (glyph < 0 || static_cast<std::size_t>(glyph) >= sprites.frames.size())
            continue;
        if (g_font_render_state.wrap &&
            SpriteIsClipped(sprites, glyph, destx, y, g_font_render_state.clip)) {
            destx = x;
            y += GetHeight(sprites, glyph);
        }
        DrawSprite(target, sprites, glyph, destx, y,
                   {.blend = mono ? SpriteBlend::Mono : SpriteBlend::Transparent,
                    .foreground = g_font_render_state.foreground,
                    .background = g_font_render_state.background,
                    .shadow = g_font_render_state.shadow,
                    .palette = &font.currentPalette()});
        destx += GetWidth(sprites, glyph);
    }
}

static void DrawTextToSurface(std::int32_t x, std::int32_t y, std::string_view text, bool mono)
{
    if (!g_font_render_state.font || !g_font_render_state.destination) return;
    DrawTextToSurface(*g_font_render_state.destination, *g_font_render_state.font, x, y, text, mono);
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407260
void DrawMonoFontText(std::int32_t x, std::int32_t y, std::string_view string)
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
void DrawFontText(std::int32_t x, std::int32_t y, std::string_view string)
{
    DrawTextToSurface(x, y, string, false);
}

// FUNCTION: WIZ8 0x004077d0
void DrawFontTextDirty(std::int32_t x, std::int32_t y, std::string_view string)
{
    if (!g_font_render_state.font) return;
    DrawFontText(x, y, string);
    InvalidateRegion(x, y, x + g_font_render_state.font->textWidth(string), y + g_font_render_state.font->height(),
                     INVAL_SRC_TRANS);
    return;
}

// Draw UTF-8 text using the current font and destination.

// FUNCTION: WIZ8 0x00407a10
void DrawFontTextToSurface(SDL_Surface& target, Font& font, std::int32_t x, std::int32_t y,
                           std::string_view string)
{
    DrawTextToSurface(target, font, x, y, string, false);
}

// FUNCTION: WIZ8 0x00407b80
void DrawMonoFontTextToSurface(SDL_Surface& target, Font& font, std::int32_t x, std::int32_t y,
                               std::string_view string)
{
    DrawTextToSurface(target, font, x, y, string, true);
}

// FUNCTION: WIZ8 0x00407d30
void ConfigureFontRendering(const std::vector<std::uint16_t>& translation)
{
    ResetFontRendering();
    g_font_translation = translation;
    g_font_render_state.destination = &GetFrameBufferSurface();
    std::uint16_t width, height;
    std::uint8_t depth;
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

void SetFontDestClip(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2)
{
    Assert(x2 > x1 && y2 > y1);
    g_font_render_state.clip = {x1, y1, x2 - x1, y2 - y1};
}

// CreateEnglishTransTable
// Creates the English text->font map table.

// FUNCTION: WIZ8 0x00407ec0
std::vector<std::uint16_t> CreateEnglishTransTable()
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

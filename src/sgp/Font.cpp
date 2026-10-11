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
#include "image_assets.h"
#include "sprite.h"

static std::vector<std::uint16_t> g_font_translation;

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
    glyphs = {};
    customPalette.reset();
    shades.clear();
    currentShade = kNormalShade;
}

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

// FUNCTION: WIZ8 0x00406c20
// FUNCTION: WIZ8 0x00406c90
// FUNCTION: WIZ8 0x00406d10
std::uint16_t Font::color(std::uint8_t index) const
{
    return glyphs ? Get16BPPColor(glyphs->palette[index]) : 0;
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

// FUNCTION: WIZ8 0x00407260
// FUNCTION: WIZ8 0x00407650
// FUNCTION: WIZ8 0x00407a10
// FUNCTION: WIZ8 0x00407b80
void DrawText(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
              TextOptions options)
{
    if (!font.glyphs)
        return;
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&target, &clip);
    const auto& sprites = *font.glyphs;
    int x = at.x;
    int y = at.y;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto glyph = GetIndex(wiz8::text::next(text, offset));
        if (glyph < 0 || static_cast<std::size_t>(glyph) >= sprites.frames.size())
            continue;
        if (options.wrap && SpriteIsClipped(sprites, glyph, x, y, clip)) {
            x = at.x;
            y += GetHeight(sprites, glyph);
        }
        DrawSprite(target, sprites, glyph, x, y,
                   {.blend = options.mono ? SpriteBlend::Mono : SpriteBlend::Transparent,
                    .foreground = options.colors.foreground,
                    .background = options.colors.background,
                    .shadow = options.colors.shadow,
                    .palette = &font.currentPalette()});
        x += GetWidth(sprites, glyph);
    }
}

// FUNCTION: WIZ8 0x004077d0
void DrawTextAndInvalidate(SDL_Surface& target, Font& font, SDL_Point at,
                           std::string_view text, TextOptions options)
{
    DrawText(target, font, at, text, options);
    InvalidateRegion(at.x, at.y, at.x + font.textWidth(text), at.y + font.height(),
                     INVAL_SRC_TRANS);
}

// FUNCTION: WIZ8 0x00407d30
void ConfigureFontRendering(const std::vector<std::uint16_t>& translation)
{
    ResetFontRendering();
    g_font_translation = translation;
}

// FUNCTION: WIZ8 0x00407e30
void ResetFontRendering()
{
    g_font_translation.clear();
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

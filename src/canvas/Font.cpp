#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/unicode.h"
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/canvas/sprite.h"

static constexpr auto font_translation = [] {
    std::array<std::uint16_t, 252> table{};
    for (unsigned index = 0; index < 26; ++index) {
        table[index] = 'A' + index;
        table[index + 26] = 'a' + index;
    }
    for (unsigned index = 0; index < 10; ++index) table[index + 52] = '0' + index;
    constexpr std::array<std::uint16_t, 92> extra{'!',
                                                  '@',
                                                  '#',
                                                  '$',
                                                  '%',
                                                  '^',
                                                  '&',
                                                  '*',
                                                  '(',
                                                  ')',
                                                  '-',
                                                  '_',
                                                  '+',
                                                  '=',
                                                  '|',
                                                  '\\',
                                                  '{',
                                                  '}',
                                                  '[',
                                                  ']',
                                                  ':',
                                                  ';',
                                                  '"',
                                                  '\'',
                                                  '<',
                                                  '>',
                                                  ',',
                                                  '.',
                                                  '?',
                                                  '/',
                                                  ' ',
                                                  193,
                                                  192,
                                                  193,
                                                  196,
                                                  195,
                                                  197,
                                                  199,
                                                  201,
                                                  200,
                                                  202,
                                                  203,
                                                  205,
                                                  204,
                                                  206,
                                                  207,
                                                  209,
                                                  211,
                                                  210,
                                                  212,
                                                  214,
                                                  213,
                                                  216,
                                                  218,
                                                  217,
                                                  219,
                                                  220,
                                                  221,
                                                  225,
                                                  224,
                                                  226,
                                                  228,
                                                  227,
                                                  229,
                                                  231,
                                                  233,
                                                  232,
                                                  234,
                                                  235,
                                                  237,
                                                  236,
                                                  238,
                                                  239,
                                                  241,
                                                  243,
                                                  242,
                                                  244,
                                                  246,
                                                  245,
                                                  248,
                                                  250,
                                                  249,
                                                  251,
                                                  252,
                                                  254,
                                                  255,
                                                  223,
                                                  FONT_GLYPH_TARGET_POINT,
                                                  FONT_GLYPH_TARGET_CONE,
                                                  FONT_GLYPH_TARGET_SINGLE,
                                                  FONT_GLYPH_TARGET_GROUP,
                                                  FONT_GLYPH_TARGET_NONE};
    std::copy(extra.begin(), extra.end(), table.begin() + 62);
    table[250] = 191;
    table[251] = 161;
    return table;
}();

Font::Font(Font&& other) noexcept
    : glyphs(std::move(other.glyphs)), customPalette(other.customPalette),
      shades(std::move(other.shades)), currentShade(other.currentShade)
{
    other.clear();
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other) {
        clear();
        glyphs = std::move(other.glyphs);
        customPalette = other.customPalette;
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

void Font::setPalette(const Palette16& colors)
{
    customPalette = colors;
    currentShade = kNormalShade;
}

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

std::uint16_t Font::createShades(std::uint32_t glow)
{
    if (!glyphs) return false;
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

std::uint16_t Font::setShade(std::uint32_t shade)
{
    if (shade != kNormalShade && (shade >= shades.size() || !shades[shade])) return false;
    currentShade = shade;
    return true;
}

std::uint16_t Font::color(std::uint8_t index) const
{
    return glyphs ? Get16BPPColor(glyphs->palette[index]) : 0;
}

bool Font::load(std::string_view filename)
{
    auto sheet = g_image_assets.sprites(filename);
    if (!sheet) return false;
    clear();
    glyphs = std::move(sheet);
    return true;
}

static std::uint32_t GetWidth(const SpriteSheet& sprites, std::int16_t ssIndex)
{
    if (ssIndex < 0 || static_cast<std::size_t>(ssIndex) >= sprites.frames.size()) return 0;
    const auto& glyph = sprites.frames[ssIndex];
    return glyph.width + glyph.offset.x;
}

// Measure a prefix counted in Unicode scalar values.

std::int16_t Font::codepointPrefixWidth(std::string_view string, std::uint32_t uiCharCount) const
{

    return textWidth(string.substr(0, wiz8::text::codepoint_prefix(string, uiCharCount)));
}

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

std::int16_t Font::textWidth(std::string_view string) const
{
    return prefixWidth(string, string.size());
}

static std::uint32_t GetHeight(const SpriteSheet& sprites, std::int16_t ssIndex)
{
    if (ssIndex < 0 || static_cast<std::size_t>(ssIndex) >= sprites.frames.size()) return 0;
    const auto& glyph = sprites.frames[ssIndex];
    return glyph.height + glyph.offset.y;
}

std::uint16_t Font::height() const
{
    if (!glyphs || glyphs->frames.empty()) return 0;
    return static_cast<std::uint16_t>(GetHeight(*glyphs, 0));
}

// First matching frame, including duplicate entries; unknown characters use the question mark.

std::int16_t GetIndex(char32_t siChar)
{
    const auto glyph = std::find(font_translation.begin(), font_translation.end(), siChar);
    if (glyph != font_translation.end())
        return static_cast<std::int16_t>(glyph - font_translation.begin());
    const auto fallback = std::find(font_translation.begin(), font_translation.end(), '?');
    return fallback == font_translation.end()
               ? 0
               : static_cast<std::int16_t>(fallback - font_translation.begin());
}

void DrawText(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
              TextOptions options)
{
    if (!font.glyphs) return;
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&target, &clip);
    const auto& sprites = *font.glyphs;
    int x = at.x;
    int y = at.y;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto glyph = GetIndex(wiz8::text::next(text, offset));
        if (glyph < 0 || static_cast<std::size_t>(glyph) >= sprites.frames.size()) continue;
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

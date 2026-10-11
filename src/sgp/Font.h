#ifndef __FONT_H_
#define __FONT_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <vector>
#include <string_view>
#include <cstddef>
#include <optional>
#include <memory>

#include "himage.h"

// font glyphs for spell targeting types
inline constexpr char32_t FONT_GLYPH_TARGET_POINT = 0xFFF0;
inline constexpr char32_t FONT_GLYPH_TARGET_CONE = 0xFFF1;
inline constexpr char32_t FONT_GLYPH_TARGET_SINGLE = 0xFFF2;
inline constexpr char32_t FONT_GLYPH_TARGET_GROUP = 0xFFF3;
inline constexpr char32_t FONT_GLYPH_TARGET_NONE = 0xFFF4;

inline constexpr std::uint32_t FONT_GLOW_GREEN = 0;
inline constexpr std::uint32_t FONT_GLOW_BLUE = 1;
inline constexpr std::uint32_t FONT_GLOW_YELLOW = 2;
inline constexpr std::uint32_t FONT_GLOW_RED = 3;

struct SDL_Surface;
struct SpriteSheet;
struct Font {
    static constexpr std::uint32_t kNormalShade = 4;

    std::shared_ptr<const SpriteSheet> glyphs;
    std::optional<Palette16> customPalette;
    std::vector<std::optional<Palette16>> shades;
    std::uint32_t currentShade = kNormalShade;
    Font() = default;
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;
    // Failed loads preserve the resource.
    bool load(std::string_view filename);
    void clear();
    void setPalette(const Palette16& palette);
    std::uint16_t color(std::uint8_t index) const;
    const Palette16& palette() const;
    const Palette16& currentPalette() const;
    void resetPalette();
    std::uint16_t createShades(std::uint32_t glow);
    std::uint16_t setShade(std::uint32_t shade);
    SDL_Point rightAlignedPosition(const SDL_Rect& bounds, std::string_view text) const;
    SDL_Point centeredPosition(const SDL_Rect& bounds, std::string_view text) const;
    std::uint16_t height() const;
    std::int16_t textWidth(std::string_view text) const;
    std::int16_t prefixWidth(std::string_view text, std::size_t byte_end) const;
    std::int16_t codepointPrefixWidth(std::string_view text, std::uint32_t count) const;
    ~Font();
};

struct TextColors {
    std::uint16_t foreground = 0;
    std::uint16_t background = 0;
    std::uint16_t shadow = 2;
};

struct TextOptions {
    bool mono = false;
    TextColors colors{};
    bool wrap = false;
};

void DrawText(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
              TextOptions options = {});
void DrawTextAndInvalidate(SDL_Surface& target, Font& font, SDL_Point at,
                           std::string_view text, TextOptions options = {});

void ConfigureFontRendering(const std::vector<std::uint16_t>& translation);
void ResetFontRendering();

std::vector<std::uint16_t> CreateEnglishTransTable();

std::int16_t GetIndex(char32_t siChar);

#endif

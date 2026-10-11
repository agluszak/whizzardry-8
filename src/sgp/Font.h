#ifndef __FONT_H_
#define __FONT_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <vector>
#include <string_view>
#include <cstddef>
#include <utility>
#include <optional>

#include "himage.h"

#include "vobject.h"

// font glyphs for spell targeting types
inline constexpr char32_t FONT_GLYPH_TARGET_POINT = 0xFFF0;
inline constexpr char32_t FONT_GLYPH_TARGET_CONE = 0xFFF1;
inline constexpr char32_t FONT_GLYPH_TARGET_SINGLE = 0xFFF2;
inline constexpr char32_t FONT_GLYPH_TARGET_GROUP = 0xFFF3;
inline constexpr char32_t FONT_GLYPH_TARGET_NONE = 0xFFF4;

struct SGPVSurface;
struct Font {
    SGPVObject glyphs{};
    std::optional<Palette16> customPalette;
    Font() = default;
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;
    // Failed loads preserve the resource and render selection. Successful
    // loads, clearing, and moves invalidate its saved and active selections.
    bool load(std::string_view filename);
    void clear();
    void setPalette(const Palette16& palette);
    const Palette16& palette() const;
    const Palette16& currentPalette() const;
    void resetPalette();
    SDL_Point rightAlignedPosition(const SDL_Rect& bounds, std::string_view text) const;
    SDL_Point centeredPosition(const SDL_Rect& bounds, std::string_view text) const;
    std::uint16_t height() const;
    std::int16_t textWidth(std::string_view text) const;
    std::int16_t prefixWidth(std::string_view text, std::size_t byte_end) const;
    std::int16_t codepointPrefixWidth(std::string_view text, std::uint32_t count) const;
    explicit Font(SGPVObject object) : glyphs(std::move(object)) {}
    ~Font();
};

struct FontRenderState {
    Font* font = nullptr;
    SGPVSurface* destination = nullptr;
    SDL_Rect clip = {0, 0, 640, 480};
    bool wrap = false;
    std::uint16_t foreground = 0;
    std::uint16_t background = 0;
    std::uint16_t shadow = 2;
    std::uint8_t foreground_index = 0;
    std::uint8_t background_index = 0;
};
extern FontRenderState g_font_render_state;
void SetFontDestClip(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2);

// functions

void SetFontForeground(std::uint8_t ubForeground);
void SetFontBackground(std::uint8_t ubBackground);
void SetFontShadow(std::uint8_t ubBackground);

void SetRGBFontShadow(std::uint32_t uiRed, std::uint32_t uiGreen, std::uint32_t uiBlue);

void DrawFontText(std::int32_t x, std::int32_t y, std::string_view text);
void DrawFontTextDirty(std::int32_t x, std::int32_t y, std::string_view text);
void DrawMonoFontText(std::int32_t x, std::int32_t y, std::string_view text);
void DrawFontTextToBuffer(std::uint8_t* pDestBuf, std::uint32_t uiDestPitchBYTES, Font& FontType, std::int32_t x,
                            std::int32_t y, std::string_view text);
void DrawMonoFontTextToBuffer(std::uint8_t* pDestBuf, std::uint32_t uiDestPitchBYTES, Font& FontType, std::int32_t x,
                                std::int32_t y, std::string_view text);

void SetFontDestBuffer(SGPVSurface& DestBuffer, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2,
                                 bool wrap);
void SetFont(Font& font);

void ConfigureFontRendering(const std::vector<std::uint16_t>& translation);
void ResetFontRendering();

std::vector<std::uint16_t> CreateEnglishTransTable();

std::int16_t GetIndex(char32_t siChar);

// Each scope owns its snapshot; nested UI drawing cannot overwrite an outer save.
class FontRenderScope {
public:
    FontRenderScope();
    ~FontRenderScope();
    FontRenderScope(const FontRenderScope&) = delete;
    FontRenderScope& operator=(const FontRenderScope&) = delete;
private:
    friend struct Font;
    friend void ResetFontRendering();
    FontRenderState saved;
    FontRenderScope* previous;
    static FontRenderScope* current;
};

#endif

#ifndef __FONT_H_
#define __FONT_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_rect.h>
#include <vector>
#include <string_view>
#include <cstddef>
#include <utility>
#include <optional>

#include "himage.h"

#include "Types.h"
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
    UINT16 height() const;
    INT16 textWidth(std::string_view text) const;
    INT16 prefixWidth(std::string_view text, std::size_t byte_end) const;
    INT16 codepointPrefixWidth(std::string_view text, UINT32 count) const;
    explicit Font(SGPVObject object) : glyphs(std::move(object)) {}
    ~Font();
};

struct FontRenderState {
    Font* font = nullptr;
    SGPVSurface* destination = nullptr;
    SDL_Rect clip = {0, 0, 640, 480};
    bool wrap = false;
    UINT16 foreground = 0;
    UINT16 background = 0;
    UINT16 shadow = 2;
    UINT8 foreground_index = 0;
    UINT8 background_index = 0;
};
extern FontRenderState g_font_render_state;
void SetFontDestClip(INT32 x1, INT32 y1, INT32 x2, INT32 y2);

// functions

void SetFontForeground(UINT8 ubForeground);
void SetFontBackground(UINT8 ubBackground);
void SetFontShadow(UINT8 ubBackground);

void SetRGBFontShadow(SDL_Color color);

void DrawFontText(INT32 x, INT32 y, std::string_view text);
void DrawFontTextDirty(INT32 x, INT32 y, std::string_view text);
void DrawMonoFontText(INT32 x, INT32 y, std::string_view text);
void DrawFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                            INT32 y, std::string_view text);
void DrawMonoFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                                INT32 y, std::string_view text);

void SetFontDestBuffer(SGPVSurface& DestBuffer, INT32 x1, INT32 y1, INT32 x2, INT32 y2,
                                 bool wrap);
void SetFont(Font& font);

void ConfigureFontRendering(const std::vector<UINT16>& translation);
void ResetFontRendering();

std::vector<UINT16> CreateEnglishTransTable();

INT16 GetIndex(char32_t siChar);

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

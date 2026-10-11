#ifndef __FONT_H_
#define __FONT_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_rect.h>
#include <vector>
#include <string_view>
#include <cstddef>
#include <utility>

#include "Types.h"
#include "vobject.h"

#define DEFAULT_SHADOW 2
#define MILITARY_SHADOW 67
#define NO_SHADOW 0

// font foreground color symbols
#define FONT_FCOLOR_WHITE 0x0000
#define FONT_FCOLOR_RED 0x0000
#define FONT_FCOLOR_BLUE 0x0000
#define FONT_FCOLOR_GREEN 0x0000
#define FONT_FCOLOR_YELLOW 0x0000
#define FONT_FCOLOR_BROWN 0x0000
#define FONT_FCOLOR_ORANGE 0x0000
#define FONT_FCOLOR_PURPLE 0x0000

// font background color symbols
#define FONT_BCOLOR_WHITE 0x0000
#define FONT_BCOLOR_RED 0x0000
#define FONT_BCOLOR_BLUE 0x0000
#define FONT_BCOLOR_GREEN 0x0000
#define FONT_BCOLOR_YELLOW 0x0000
#define FONT_BCOLOR_BROWN 0x0000
#define FONT_BCOLOR_ORANGE 0x0000
#define FONT_BCOLOR_PURPLE 0x0000

// font glyphs for spell targeting types
#define FONT_GLYPH_TARGET_POINT 0xFFF0
#define FONT_GLYPH_TARGET_CONE 0xFFF1
#define FONT_GLYPH_TARGET_SINGLE 0xFFF2
#define FONT_GLYPH_TARGET_GROUP 0xFFF3
#define FONT_GLYPH_TARGET_NONE 0xFFF4

// typedefs


struct SGPVSurface;
struct Font {
    SGPVObject glyphs{};
    Font() = default;
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;
    // Failed loads preserve the resource and render selection. Successful
    // loads, clearing, and moves invalidate its saved and active selections.
    bool load(std::string_view filename);
    void clear();
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
    BOOLEAN wrap = FALSE;
    UINT16 foreground = 0;
    UINT16 background = 0;
    UINT16 shadow = DEFAULT_SHADOW;
    UINT8 foreground_index = 0;
    UINT8 background_index = 0;
};
extern FontRenderState g_font_render_state;
void SetFontDestClip(INT32 x1, INT32 y1, INT32 x2, INT32 y2);

// functions

void SetFontForeground(UINT8 ubForeground);
void SetFontBackground(UINT8 ubBackground);
void SetFontShadow(UINT8 ubBackground);

//Kris:  added these
void SetRGBFontShadow(UINT32 uiRed, UINT32 uiGreen, UINT32 uiBlue);

UINT16* SetFontObjectPalette16BPP(Font& iFont, UINT16* pPal16);
UINT16* GetFontObjectPalette16BPP(Font& iFont);

UINT32 DrawFontText(INT32 x, INT32 y, std::string_view text);
UINT32 DrawFontTextDirty(INT32 x, INT32 y, std::string_view text);
UINT32 DrawMonoFontText(INT32 x, INT32 y, std::string_view text);
UINT32 DrawFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                            INT32 y, std::string_view text);
UINT32 DrawMonoFontTextToBuffer(UINT8* pDestBuf, UINT32 uiDestPitchBYTES, Font& FontType, INT32 x,
                                INT32 y, std::string_view text);

// Function for displaying coded test. Since it's slower to do this, it's separate from  the normal fuctions
#define FONT_CODE_BEGINCOLOR 180
#define FONT_CODE_RESETCOLOR 181

extern BOOLEAN SetFontDestBuffer(SGPVSurface& DestBuffer, INT32 x1, INT32 y1, INT32 x2, INT32 y2,
                                 BOOLEAN wrap);
extern BOOLEAN SetFont(Font& iFontIndex);

void ConfigureFontRendering(const std::vector<UINT16>& translation);
void ResetFontRendering();


extern std::vector<UINT16> CreateEnglishTransTable();

extern INT16 GetIndex(char32_t siChar);
extern UINT32 GetWidth(const SGPVObject& hSrcVObject, INT16 ssIndex);

extern void SaveFontSettings(void);
extern void RestoreFontSettings(void);

void FindFontRightCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, std::string_view pStr,
                              Font& iFontIndex, INT16* psNewX, INT16* psNewY);
void FindFontCenterCoordinates(INT16 sLeft, INT16 sTop, INT16 sWidth, INT16 sHeight, std::string_view pStr,
                               Font& iFontIndex, INT16* psNewX, INT16* psNewY);


#endif

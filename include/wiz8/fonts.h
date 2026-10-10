#pragma once

unsigned char InitializeMenuFonts(void);

#include "Font.h"
#include "Types.h"

/* Game-specific bitmap font roles and owned notice palettes. */
extern int ghTinyMonoFont;
extern int g_calligraphy_shadow_font;
extern int g_calligraphy_font;
extern int g_engraved_font;
extern int g_monster_damage_font;
extern int g_options_detail_font;
extern int g_wiz_text_mono_font;
extern int g_wiz_text_font;
extern int g_embossed_font;
extern int g_font12point1;
extern int g_wiz_dialog_font;
extern int g_profession_font;
extern int g_wiz_text_font_secondary;
extern int g_wiz_text_bold_font;
extern int g_font10arial;
extern int g_small_font_secondary;
extern int g_button_font;
extern int g_large_font;
extern int g_small_font;
extern int g_options_title_font;
extern int g_smfnt_font;
/* Frames of the notice palette catalog (Data\Fonts\Palette*.sti). */
enum W8FontPaletteIndex {
    W8_FONT_PALETTE_RED = 0,
    W8_FONT_PALETTE_GREEN = 1,
    W8_FONT_PALETTE_PURPLE = 2,
    W8_FONT_PALETTE_BLUE = 3,
    W8_FONT_PALETTE_ORANGE = 4,
    W8_FONT_PALETTE_YELLOW = 5,
    W8_FONT_PALETTE_PINK = 6,
    W8_FONT_PALETTE_BROWN = 7,
    W8_FONT_PALETTE_WHITE = 8,
    W8_FONT_PALETTE_RUST = 9,
    W8_FONT_PALETTE_BRONZE = 10,
    W8_FONT_PALETTE_GRAY = 11,
    W8_FONT_PALETTE_BEIGE = 12,
    W8_FONT_PALETTE_OPTIONS_GREEN = 13,
    W8_FONT_PALETTE_OPTIONS_WHITE = 14,
    W8_FONT_PALETTE_TEXT_BOX = 15,
    W8_FONT_PALETTE_COUNT = 16
};

extern std::array<std::unique_ptr<UINT16[]>, 15> g_font_state_palettes;

#pragma once

unsigned char InitializeMenuFonts(void);
void ReleaseMenuFonts();

#include "Font.h"

/* Game-specific bitmap font roles and owned notice palettes. */
extern Font ghTinyMonoFont;
extern Font g_calligraphy_shadow_font;
extern Font g_calligraphy_font;
extern Font g_engraved_font;
extern Font g_monster_damage_font;
extern Font g_options_detail_font;
extern Font g_wiz_text_mono_font;
extern Font g_wiz_text_font;
extern Font g_embossed_font;
extern Font g_font12point1;
extern Font g_wiz_dialog_font;
extern Font g_profession_font;
extern Font g_wiz_text_font_secondary;
extern Font g_wiz_text_bold_font;
extern Font g_font10arial;
extern Font g_small_font_secondary;
extern Font g_button_font;
extern Font g_large_font;
extern Font g_small_font;
extern Font g_options_title_font;
extern Font g_smfnt_font;
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

extern std::array<Palette16, 15> g_font_state_palettes;

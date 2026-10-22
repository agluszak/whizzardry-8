#pragma once

#include <array>

#include "wiz8/canvas/Font.h"
#include "wiz8/rules/events.h" // IWYU pragma: export - the notice palettes.

/* Load every game font below; false when one is missing. */
bool InitializeMenuFonts(void);
void ReleaseMenuFonts();
/* Drop any notice palette a screen left selected on a font. */
void ResetMenuFontPalettes();

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
/* Level-load captions and the party-death/ending overlay text. */
extern Font g_level_load_font;
extern Font g_journal_font;
extern std::array<Palette16, 15> g_font_state_palettes;

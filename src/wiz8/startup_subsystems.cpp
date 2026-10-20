#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/fonts.h"
#include "wiz8/render/frame.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

bool g_texture_cache_enabled;

Font g_calligraphy_shadow_font;
Font g_calligraphy_font;
Font g_engraved_font;
Font g_monster_damage_font;
Font g_options_detail_font;
Font g_wiz_text_mono_font;
Font g_wiz_text_font;
Font g_embossed_font;
Font g_font12point1;
Font g_wiz_dialog_font;
Font g_profession_font;
Font g_wiz_text_font_secondary;
Font g_wiz_text_bold_font;
Font g_font10arial;
Font g_small_font_secondary;
Font g_button_font;
Font g_large_font;
Font g_small_font;
Font g_options_title_font;
Font ghTinyMonoFont;
Font g_smfnt_font;
Font g_level_load_font;
Font g_journal_font;

std::array<Palette16, 15> g_font_state_palettes;

namespace {
constexpr std::array all_fonts = {
    &g_calligraphy_shadow_font,
    &g_calligraphy_font,
    &g_engraved_font,
    &g_monster_damage_font,
    &g_options_detail_font,
    &g_wiz_text_mono_font,
    &g_wiz_text_font,
    &g_embossed_font,
    &g_font12point1,
    &g_wiz_dialog_font,
    &g_profession_font,
    &g_wiz_text_font_secondary,
    &g_wiz_text_bold_font,
    &g_font10arial,
    &g_small_font_secondary,
    &g_button_font,
    &g_large_font,
    &g_small_font,
    &g_options_title_font,
    &ghTinyMonoFont,
    &g_smfnt_font,
    &g_level_load_font,
    &g_journal_font,
};
}

unsigned char InitializeMenuFonts(void)
{
    const std::array fonts{
        std::tuple{&g_large_font, "Data\\Fonts\\LargeFont.sti", FONT_GLOW_GREEN},
        std::tuple{&g_small_font, "Data\\Fonts\\SmallFont.sti", FONT_GLOW_GREEN},
        std::tuple{&g_small_font_secondary, "Data\\Fonts\\SmallFont.sti", FONT_GLOW_RED},
        std::tuple{&g_wiz_text_font, "Data\\Fonts\\Wiz_Text_Font.sti", FONT_GLOW_GREEN},
        std::tuple{&g_calligraphy_font, "Data\\Fonts\\CalligraphyFont.sti", FONT_GLOW_GREEN},
        std::tuple{&g_calligraphy_shadow_font, "Data\\Fonts\\CalligraphyFontFullShadow.sti",
                   FONT_GLOW_GREEN},
        std::tuple{&g_smfnt_font, "Data\\Fonts\\SmFnt.sti", FONT_GLOW_GREEN},
        std::tuple{&ghTinyMonoFont, "Data\\Fonts\\TinyMonoFont.sti", FONT_GLOW_GREEN},
        std::tuple{&g_button_font, "Data\\Fonts\\ButtonFont.sti", FONT_GLOW_GREEN},
        std::tuple{&g_engraved_font, "Data\\Fonts\\Engraved.sti", FONT_GLOW_GREEN},
        std::tuple{&g_embossed_font, "Data\\Fonts\\Embossed.sti", FONT_GLOW_GREEN},
        std::tuple{&g_wiz_text_font_secondary, "Data\\Fonts\\Wiz_Text_Font.sti", FONT_GLOW_GREEN},
        std::tuple{&g_wiz_text_bold_font, "Data\\Fonts\\Wiz_Text_Font_Bold.sti", FONT_GLOW_GREEN},
        std::tuple{&g_wiz_text_mono_font, "Data\\Fonts\\wiz_text_font_monopalette.sti",
                   std::uint32_t{0}},
        std::tuple{&g_options_title_font, "Data\\Fonts\\Opt_title_font.sti", FONT_GLOW_GREEN},
        std::tuple{&g_options_detail_font, "Data\\Fonts\\Opt_detail_font.sti", FONT_GLOW_GREEN},
        std::tuple{&g_profession_font, "Data\\Fonts\\Profession.sti", FONT_GLOW_YELLOW},
        std::tuple{&g_font10arial, "Data\\Fonts\\Font10Arial.sti", std::uint32_t{0}},
        std::tuple{&g_wiz_dialog_font, "Data\\Fonts\\dialog_font.sti", FONT_GLOW_GREEN},
        std::tuple{&g_monster_damage_font, "Data\\Fonts\\monsterdamage_font.sti", std::uint32_t{0}},
        std::tuple{&g_font12point1, "Data\\Fonts\\FONT12POINT1.sti", FONT_GLOW_GREEN},
        std::tuple{&g_level_load_font, "Data\\Level Load\\levelload_font.sti", std::uint32_t{0}},
        std::tuple{&g_journal_font, "Data\\Journal\\journal_font.sti", std::uint32_t{0}}};
    std::array<Font, fonts.size()> loaded;
    for (size_t index = 0; index < fonts.size(); ++index) {
        const auto& [role, filename, shade] = fonts[index];
        if (!loaded[index].load(filename)) {
            return 0;
        }
    }
    for (size_t index = 0; index < fonts.size(); ++index) {
        const auto shade = std::get<2>(fonts[index]);
        loaded[index].createShades(shade);
    }
    decltype(g_font_state_palettes) palettes;
    for (size_t index = 0; index < palettes.size(); ++index) {
        palettes[index] =
            CatalogArtwork::Load(0x1e5, static_cast<int>(index)).spriteSheet()->colors;
    }
    for (size_t index = 0; index < fonts.size(); ++index)
        *std::get<0>(fonts[index]) = std::move(loaded[index]);
    g_font_state_palettes.swap(palettes);
    return 1;
}

void ReleaseMenuFonts()
{
    for (Font* font : all_fonts) font->clear();
    g_font_state_palettes = {};
}

void ResetMenuFontPalettes()
{
    for (Font* font : all_fonts) font->resetPalette();
}

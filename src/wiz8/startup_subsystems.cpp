#include <utility>
#include <tuple>
#include <array>
#include <cstddef>
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include <memory>

#include "wiz8/fonts.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/engine_code/Video2.h"
#include "Font.h"
#include "vobject.h"
#include "Types.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// GLOBAL: WIZ8 0x0065beaf
bool g_texture_cache_enabled;

// GLOBAL: WIZ8 0x006835f4
Font g_calligraphy_shadow_font;
// GLOBAL: WIZ8 0x006835f8
Font g_calligraphy_font;
// GLOBAL: WIZ8 0x00683600
Font g_engraved_font;
// GLOBAL: WIZ8 0x00683608
Font g_monster_damage_font;
// GLOBAL: WIZ8 0x00683614
Font g_options_detail_font;
// GLOBAL: WIZ8 0x00683630
Font g_wiz_text_mono_font;
// GLOBAL: WIZ8 0x00683640
Font g_wiz_text_font;
// GLOBAL: WIZ8 0x00683644
Font g_embossed_font;
// GLOBAL: WIZ8 0x00683648
Font g_font12point1;
// GLOBAL: WIZ8 0x00683654
Font g_wiz_dialog_font;
// GLOBAL: WIZ8 0x00683658
Font g_profession_font;
// GLOBAL: WIZ8 0x00683660
Font g_wiz_text_font_secondary;
// GLOBAL: WIZ8 0x00683664
Font g_wiz_text_bold_font;
// GLOBAL: WIZ8 0x00683668
Font g_font10arial;
// GLOBAL: WIZ8 0x0068366C
Font g_small_font_secondary;
// GLOBAL: WIZ8 0x00683670
Font g_button_font;
// GLOBAL: WIZ8 0x00683674
Font g_large_font;
// GLOBAL: WIZ8 0x00683678
Font g_small_font;
// GLOBAL: WIZ8 0x0068368C
Font g_options_title_font;
// GLOBAL: WIZ8 0x00683690
Font ghTinyMonoFont;
// GLOBAL: WIZ8 0x00683694
Font g_smfnt_font;

// GLOBAL: WIZ8 0x0068EE1C
std::array<std::unique_ptr<UINT16[]>, 15> g_font_state_palettes;

// FUNCTION: WIZ8 0x004e27a0
unsigned char InitializeMenuFonts(void)
{
    const std::array fonts{
        std::tuple{&g_large_font, "Data\\Fonts\\LargeFont.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_small_font, "Data\\Fonts\\SmallFont.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_small_font_secondary, "Data\\Fonts\\SmallFont.sti", HVOBJECT_GLOW_RED},
        std::tuple{&g_wiz_text_font, "Data\\Fonts\\Wiz_Text_Font.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_calligraphy_font, "Data\\Fonts\\CalligraphyFont.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_calligraphy_shadow_font, "Data\\Fonts\\CalligraphyFontFullShadow.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_smfnt_font, "Data\\Fonts\\SmFnt.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&ghTinyMonoFont, "Data\\Fonts\\TinyMonoFont.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_button_font, "Data\\Fonts\\ButtonFont.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_engraved_font, "Data\\Fonts\\Engraved.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_embossed_font, "Data\\Fonts\\Embossed.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_wiz_text_font_secondary, "Data\\Fonts\\Wiz_Text_Font.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_wiz_text_bold_font, "Data\\Fonts\\Wiz_Text_Font_Bold.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_wiz_text_mono_font, "Data\\Fonts\\wiz_text_font_monopalette.sti", 0},
        std::tuple{&g_options_title_font, "Data\\Fonts\\Opt_title_font.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_options_detail_font, "Data\\Fonts\\Opt_detail_font.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_profession_font, "Data\\Fonts\\Profession.sti", HVOBJECT_GLOW_YELLOW},
        std::tuple{&g_font10arial, "Data\\Fonts\\Font10Arial.sti", 0},
        std::tuple{&g_wiz_dialog_font, "Data\\Fonts\\dialog_font.sti", HVOBJECT_GLOW_GREEN},
        std::tuple{&g_monster_damage_font, "Data\\Fonts\\monsterdamage_font.sti", 0},
        std::tuple{&g_font12point1, "Data\\Fonts\\FONT12POINT1.sti", HVOBJECT_GLOW_GREEN}
    };
    std::array<Font, fonts.size()> loaded;
    for (size_t index = 0; index < fonts.size(); ++index) {
        const auto& [role, filename, shade] = fonts[index];
        if (!loaded[index].load(filename)) {
            return 0;
        }
    }
    for (size_t index = 0; index < fonts.size(); ++index) {
        const auto shade = std::get<2>(fonts[index]);
        if (shade)
            CreateObjectPaletteTables(loaded[index].glyphs, shade);
    }
    decltype(g_font_state_palettes) palettes;
    for (size_t index = 0; index < palettes.size(); ++index) {
        palettes[index] = CopyCatalogImagePalette16BPP(0x1e5, index);
        if (!palettes[index]) {
            return 0;
        }
    }
    for (size_t index = 0; index < fonts.size(); ++index)
        *std::get<0>(fonts[index]) = std::move(loaded[index]);
    g_font_state_palettes.swap(palettes);
    SetFont(g_large_font);
    ConfigureDialogFont(g_wiz_dialog_font, 1, 0xff, 0);
    return 1;
}

void ReleaseMenuFonts()
{
    g_level_load_font.clear();
    g_dialog_interface_font = nullptr;
    ghTinyMonoFont.clear();
    g_calligraphy_shadow_font.clear();
    g_calligraphy_font.clear();
    g_engraved_font.clear();
    g_monster_damage_font.clear();
    g_options_detail_font.clear();
    g_wiz_text_mono_font.clear();
    g_wiz_text_font.clear();
    g_embossed_font.clear();
    g_font12point1.clear();
    g_wiz_dialog_font.clear();
    g_profession_font.clear();
    g_wiz_text_font_secondary.clear();
    g_wiz_text_bold_font.clear();
    g_font10arial.clear();
    g_small_font_secondary.clear();
    g_button_font.clear();
    g_large_font.clear();
    g_small_font.clear();
    g_options_title_font.clear();
    g_smfnt_font.clear();
    for (auto& palette : g_font_state_palettes) palette.reset();
}

#include <cstdint>
#include "Font.h"
#pragma once

#include <optional>
#include <string_view>

#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "compat/compiler.h"

enum { W8_DIALOG_ATTRIBUTE_COUNT = 7 };

struct W8AttributeMinimums {
    std::int32_t values[W8_DIALOG_ATTRIBUTE_COUNT];
};

/* One row supplies the two string ids and the catalog image index copied into
   a profession or race information dialog. */
struct W8ProfRaceInfoRow {
    std::uint32_t name_id;
    std::uint32_t detail_id;
    std::uint32_t image_id;
};

extern W8AttributeMinimums g_profession_attribute_minimums[];
extern W8AttributeMinimums g_race_attribute_minimums[];
extern W8ProfRaceInfoRow g_profession_info_rows[W8_PROFESSION_COUNT];
extern W8ProfRaceInfoRow g_race_info_rows[W8_RACE_COUNT];

/* Shared shell of the profession and race information dialogs: the base fixes
   the popup frame and the derived dialogs fill the string ids, the attribute
   minimums and the header icon. */
// VTABLE: WIZ8 0x005efbc0
class W8ProfRaceInfoDialogBase : public W8DialogBase {
public:
    W8ProfRaceInfoDialogBase();
    virtual std::int32_t CreateControls() override;
    virtual void Draw() override;
    virtual void OnRightButtonUp() override;
    virtual void OnMouseWheel(std::int32_t delta) override;

protected:
    virtual bool PopulateText();

private:
    static void ScrollCallback(W8DialogScrollBar* scroll_bar, std::int32_t first_visible_entry);
    /* Renders one text line at a dialog-relative rectangle through a scratch
       W8TextBuffer. */
    void DrawTextLine(std::uint32_t layout_mode, std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height,
                      std::string_view text, Font& font);

protected:
    std::optional<CatalogArtwork> m_uiArtwork;
    std::uint32_t m_uiSummaryImage;
    std::uint32_t m_uiNameId;    /* g_localized_strings index */
    std::uint32_t m_uiHeadingId; /* g_localized_strings index */
    std::uint32_t m_uiDetailId;  /* g_localized_strings index */
    std::int32_t m_minimums[W8_DIALOG_ATTRIBUTE_COUNT];
    W8DialogScrollBar m_scroll_bar;
    W8ButtonWidget m_button;
    W8DialogTextArea m_text_area;
};

// VTABLE: WIZ8 0x005efbfc
class W8ProfessionInfoDialog : public W8ProfRaceInfoDialogBase {
public:
    W8ProfessionInfoDialog(W8Profession uiIndex);

private:
    virtual bool PopulateText() override;

    W8Profession m_uiIndex;
};

// VTABLE: WIZ8 0x005efc38
class W8RaceInfoDialog : public W8ProfRaceInfoDialogBase {
public:
    W8RaceInfoDialog(W8Race uiIndex);

private:
    virtual bool PopulateText() override;

    W8Race m_uiIndex;
};

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/character.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

static W8CharacterStatsRecord g_character_profession_records[15] = {
    {0x0000010b, 0x0000000a, 0x0000000b, 0x02a4, 0, 0},
    {0x0000010b, 0x00000018, 0x00000019, 0x02a5, 0, 0},
    {0x0000010b, 0x00000008, 0x00000009, 0x02a6, 0, 0},
    {0x0000010b, 0x00000002, 0x00000003, 0x02a7, 0, 0},
    {0x0000010b, 0x00000010, 0x00000011, 0x02a8, 0, 0},
    {0x0000010b, 0x00000014, 0x00000015, 0x02a9, 0, 0},
    {0x0000010b, 0x0000000c, 0x0000000d, 0x02aa, 0, 0},
    {0x0000010b, 0x0000001c, 0x0000001d, 0x02ab, 0, 0},
    {0x0000010b, 0x00000006, 0x00000007, 0x02ac, 0, 0},
    {0x0000010b, 0x00000000, 0x00000001, 0x02ad, 0, 0},
    {0x0000010b, 0x00000012, 0x00000013, 0x02ae, 0, 0},
    {0x0000010b, 0x00000016, 0x00000017, 0x02af, 0, 0},
    {0x0000010b, 0x00000004, 0x00000005, 0x02b0, 0, 0},
    {0x0000010b, 0x0000000e, 0x0000000f, 0x02b1, 0, 0},
    {0x0000010b, 0x0000001a, 0x0000001b, 0x02b2, 0, 0},
};
static W8CharacterStatsRecord g_character_race_records[W8_RACE_PLAYABLE_COUNT] = {
    {0x0000010c, 0x00000008, 0x00000009, 0x0284, 1, 0},
    {0x0000010c, 0x00000004, 0x00000005, 0x0285, 1, 0},
    {0x0000010c, 0x0000000c, 0x0000000d, 0x0286, 1, 0},
    {0x0000010c, 0x00000010, 0x00000011, 0x0287, 1, 0},
    {0x0000010c, 0x0000000e, 0x0000000f, 0x0288, 1, 0},
    {0x0000010c, 0x00000014, 0x00000015, 0x0289, 1, 0},
    {0x0000010c, 0x00000000, 0x00000001, 0x028a, 1, 0},
    {0x0000010c, 0x0000000a, 0x0000000b, 0x028b, 1, 0},
    {0x0000010c, 0x00000002, 0x00000003, 0x028c, 1, 0},
    {0x0000010c, 0x00000012, 0x00000013, 0x028d, 1, 0},
    {0x0000010c, 0x00000006, 0x00000007, 0x028e, 1, 0},
};
static W8CharacterStatsRecord g_character_gender_records[2] = {
    {0x0000010d, 0x00000000, 0x00000001, 0x02d1, 0, 0},
    {0x0000010d, 0x00000002, 0x00000003, 0x02d2, 0, 0},
};
static W8CharacterStatsRecord g_character_profession_default_record = {
    0x0000010e, 0x00000002, 0x00000003, 0x008f, 1, 0,
};
static W8CharacterStatsRecord g_character_race_default_record = {
    0x0000010e, 0x00000000, 0x00000001, 0x0090, 1, 0,
};
static W8CharacterStatsRecord g_character_gender_default_record = {
    0x0000010e, 0x00000004, 0x00000005, 0x0091, 1, 0,
};

/* The six resistance icons drawn beside the stats page's resistance values. */
int g_character_resistance_images[6] = {
    0x193, 0x194, 0x195, 0x196, 0x197, 0x198,
};
unsigned short g_character_trait_name_ids[0x20] = {
    0x328, 0x329, 0x32a, 0x32b, 0x32c, 0x32d, 0x32e, 0x32f, 0x330, 0x331, 0x332,
    0x333, 0x334, 0x335, 0x336, 0x337, 0x338, 0x339, 0x33a, 0x33b, 0x33c, 0x33d,
    0x33e, 0x33f, 0x340, 0x341, 0x342, 0x343, 0x344, 0x345, 0x346, 0x347,
};
char g_dash[] = "-";
static char g_zero_slash_zero[] = "0/0";

/* Skill name message ids indexed by skill id are declared with the stats
   page's shared tables in CharacterScreen.h. */

/* The stats page's columns are laid out against these two origin-relative
   frames; the profession bonus block's line height depends on how many traits
   the character has. */

W8CharacterStatsRow::W8CharacterStatsRow(W8CharacterStatsPage& owner, int x, int y, int count,
                                         const W8CharacterStatsRecord* table,
                                         const W8CharacterStatsRecord* default_record,
                                         int help_first, int help_second, int help_value)
    : m_count(count), m_x(0xc3 + x), m_y(0x2b + y), m_table(table),
      m_default_record(default_record), m_owner(owner)
{
    const auto arrow = [this](int x, int frame, int help, int direction) {
        auto button = std::make_unique<W8ButtonWidget>(m_owner.input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0x10a, 0).spriteSheet(), frame + 3, frame,
                                 frame + 1, frame + 2, frame + 4),
            {.on_click = [this, direction](
                             auto*) { AfterUiDispatch([this, direction] { Step(direction); }); },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = help,
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(x, m_y + 4);
        return button;
    };
    m_decrement = arrow(m_x + 1, 0, help_first, -1);
    m_increment = arrow(m_x + 0x9f, 5, help_value, 1);
    m_value = &owner.input.add(
        SDL_Rect{m_x + 0x1c, m_y + 4, 127, 23}, [this](const UiPointerEvent& event) {
            m_dirty = true;
            if ((m_enabled) && (event.action != UiPointerAction::Up || event.captured_buttons))
                PlayButtonSound(event.action);
            if (event.action == UiPointerAction::Up && event.inside && event.captured_buttons) {
                if (event.button == UiPointerButton::Right && m_index >= 0)
                    AfterUiDispatch([this] { m_owner.OnRowInfoRequested(this, m_index); });
                else if (event.button == UiPointerButton::Left && m_enabled)
                    AfterUiDispatch([this] {
                        if (m_choices)
                            Collapse();
                        else
                            BuildSubpanel();
                    });
            }
            return UiResult::Handled;
        });
    m_value->setTooltip([help_second] { return std::string(g_localized_strings[help_second]); });
}

W8CharacterStatsRow::~W8CharacterStatsRow()
{
    m_owner.input.remove(*m_value);
}

void W8CharacterStatsRow::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    m_decrement->SetEnabled(enabled);
    m_increment->SetEnabled(enabled);
    m_dirty = true;
}

void W8CharacterStatsRow::SetValue(int index)
{
    const int previous = m_index;
    m_index = index;
    m_dirty = true;
    if (previous != index) m_owner.OnRowValueChanged(this, index);
}

void W8CharacterStatsRow::Step(int direction)
{
    for (int step = 1; step <= m_count; ++step) {
        const int index = m_index < 0 ? (direction > 0 ? step - 1 : m_count - step)
                                      : (m_index + direction * step + m_count * 2) % m_count;
        if (m_table[index].enabled) {
            SetValue(index);
            return;
        }
    }
}

void W8CharacterStatsRow::BuildSubpanel()
{
    m_choices = std::make_unique<UiLayer>(UiLayerOrder::CharacterChoices);
    m_choice_targets.clear();
    for (int index = 0; index < m_count; ++index) {
        const int top = index == 0 ? 0 : index * 22 + 2;
        const int height = index == 0 || index == m_count - 1 ? 24 : 22;
        auto& choice = m_choices->add(
            SDL_Rect{m_x + 0x9e, m_y + 1 + top, 127, height + 1},
            [this, index](const UiPointerEvent& event) {
                m_dirty = true;
                if ((m_table[index].enabled) &&
                    (event.action != UiPointerAction::Up || event.captured_buttons))
                    PlayButtonSound(event.action);
                if (event.action == UiPointerAction::Up && event.inside && event.captured_buttons) {
                    if (event.button == UiPointerButton::Right)
                        AfterUiDispatch([this, index] { m_owner.OnRowInfoRequested(this, index); });
                    else if (event.button == UiPointerButton::Left && m_table[index].enabled)
                        AfterUiDispatch([this, index] {
                            SetValue(index);
                            Collapse();
                        });
                }
                return UiResult::Handled;
            });
        m_choice_targets.push_back(&choice);
    }
    m_dirty = true;
    m_owner.OnRowExpanded(this);
}

void W8CharacterStatsRow::Collapse()
{
    m_choices.reset();
    m_choice_targets.clear();
    m_dirty = true;
    m_owner.OnRowCollapsed(this);
}

void W8CharacterStatsRow::CollapseIfNotHovered()
{
    if (m_choices && !m_value->hovered() &&
        std::ranges::none_of(m_choice_targets,
                             [](const auto* target) { return target->hovered(); }))
        Collapse();
}

static void DrawStatRecord(SDL_Surface& target, const W8CharacterStatsRecord& record, int x, int y,
                           bool enabled, bool hovered)
{
    W8TextBuffer text;
    text.SetLayoutBounds(x + 0x19, y, x + 0x7e, y + 0x16);
    text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    text.SetFontStateIndex(enabled ? (hovered ? 0xd : -1) : 0);
    text.SetText(g_localized_strings[record.name_id], g_options_detail_font);
    text.RenderToTarget(0, true, target);
    CatalogArtwork::Load(record.object, 0)
        .drawAndInvalidate(target, enabled ? record.image_enabled : record.image_disabled, x + 2,
                           y + 1);
}

void W8CharacterStatsRow::Draw(SDL_Surface& target, bool force)
{
    m_dirty |= force;
    if (force || m_dirty) {
        CatalogArtwork::Load(0x104, 0).drawAndInvalidate(target,
                                                         !m_enabled           ? 3
                                                         : m_value->hovered() ? 2
                                                                              : 1,
                                                         m_x + 0x1c, m_y + 4);
        DrawStatRecord(target, m_index < 0 ? *m_default_record : m_table[m_index], m_x + 0x1c,
                       m_y + 4, m_enabled, false);
    }
    m_decrement->m_dirty |= force;
    m_increment->m_dirty |= force;
    m_decrement->Refresh();
    m_increment->Refresh();
    if (!m_choices) m_dirty = false;
}

void W8CharacterStatsRow::DrawChoices(SDL_Surface& target)
{
    if (!m_choices || !m_dirty) return;
    for (int index = 0; index < m_count; ++index) {
        const int top = index == 0 ? 0 : index * 22 + 2;
        const int frame = index == 0 ? 4 : index == m_count - 1 ? 6 : 5;
        CatalogArtwork::Load(0x104, 0).drawAndInvalidate(target, frame, m_x + 0x9e, m_y + 1 + top);
        DrawStatRecord(target, m_table[index], m_x + 0x9e, m_y + 1 + top + (index == 0 ? 2 : 0),
                       m_table[index].enabled, m_choice_targets[index]->hovered());
    }
    m_dirty = false;
}

/* Send every row control through its enabled state, redraw the values, and
   refresh the character-wide figures the rows depend on. */
void W8CharacterStatsPage::Activate()
{
    input.setEnabled(true);
    Refresh();
    for (int index = 0; index < 5; ++index) {
        m_attribute_controls[index]->setEnabled(true);
    }
    m_contentDirty = true;
    m_prepared = true;
}

/* The skills page's Deactivate is the same one-call body; the linker folded
   both onto this address, so only this definition carries the marker. */
void W8CharacterStatsPage::Deactivate()
{
    input.setEnabled(false);
}

/* Rebuild the three row displays from the character's current profession,
   race and sex. In creation mode the profession list is opened up
   entirely; in level-up mode only the eligible professions stay enabled. */
void W8CharacterStatsPage::UpdateRowValues()
{
    unsigned char eligible[15];

    if (m_mode == W8_CHARACTER_CREATE) {
        if (m_character->iProfession == W8_PROFESSION_VALKYRIE) {
            g_character_gender_records[0].enabled = 0;
        } else {
            g_character_gender_records[0].enabled = 1;
            g_character_gender_records[1].enabled = 1;
        }
    } else if (m_mode == W8_CHARACTER_LEVEL_UP) {
        DetermineEligibleProfessions(m_character, m_creation_state, eligible);
    }
    for (int index = 0; index < 15; ++index) {
        if (m_mode == W8_CHARACTER_CREATE) {
            g_character_profession_records[index].enabled = 1;
        } else if (m_mode == W8_CHARACTER_LEVEL_UP) {
            g_character_profession_records[index].enabled = eligible[index];
        }
    }

    W8Profession profession = m_character->iProfession;
    W8CharacterStatsRow* row = m_profession_row.get();
    row->SetValue(profession);
    m_profession_row->Invalidate();

    W8Race race = m_character->iRace;
    row = m_race_row.get();
    row->SetValue(race);
    m_race_row->Invalidate();

    W8Gender gender = m_character->gender;
    row = m_gender_row.get();
    row->SetValue(gender);
    m_gender_row->Invalidate();
}

/* The entries only need enabling the first time the rows become usable. */
void W8CharacterStatsPage::Refresh()
{
    UpdateRowValues();
    if (m_character->iRace != W8_RACE_NONE || m_character->iProfession != W8_PROFESSION_NONE) {
        for (const auto& entry : m_entries) {
            if (!m_rows_initialized) {
                entry->SetEnabled(true);
            }
            entry->UpdateButtons();
        }
        m_rows_initialized = true;
    }
}

/* Accept the page: refund everything the editing state still owes, then let
   the screen recompute its navigation buttons. */
void W8CharacterStatsPage::Accept()
{
    RefundAllocatedAttributes(m_character, m_creation_state);
    InvalidateAll();
    m_contentDirty = true;
    m_screen->UpdateNavigation(this);
    for (const auto& entry : m_entries) {
        entry->UpdateButtons();
    }
}

/* The next button needs a complete attribute allocation; exit is available
   once any points are committed. */
void W8CharacterStatsPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    if (!m_creation_state->attributes_complete || m_character->iProfession == W8_PROFESSION_NONE ||
        m_character->iRace == W8_RACE_NONE || m_character->gender == W8_GENDER_UNSET) {
        *next_enabled = false;
    } else {
        *next_enabled = true;
    }
    *exit_enabled =
        m_creation_state->attribute_points_remaining < m_creation_state->attribute_points_total;
    if (*next_enabled != nav_next_state) {
        for (const auto& entry : m_entries) {
            entry->SetIncrementAllowed(!*next_enabled);
        }
        nav_next_state = *next_enabled;
    }
}

/* A click anywhere closes an expanded row's record list. */
void W8CharacterStatsPage::HandleInput(const InputEvent* input)
{
    if (input->type != InputEventType::LeftButtonDown &&
        input->type != InputEventType::RightButtonDown) {
        return;
    }
    m_profession_row->CollapseIfNotHovered();
    m_race_row->CollapseIfNotHovered();
    m_gender_row->CollapseIfNotHovered();
}

/* One attribute entry was adjusted: apply the point through the shared
   creation-state helper, then show every skill the maxed attribute gates.
   Only skills 0x22..0x27 are walked: retail's end bound is the attribute
   field of g_skill_attributes[0x28], so the last skill is never offered. */
void W8CharacterStatsPage::AdjustEntry(W8CharacterPageEntry* entry, int delta)
{
    entry->MarkDirty();
    m_contentDirty = true;
    AdjustAllocatedAttribute(m_character, m_creation_state, static_cast<W8Attribute>(entry->m_id),
                             delta);
    InvalidateAll();
    m_screen->UpdateNavigation(this);
    if (m_character->attributes[entry->m_id].base >= 100) {
        for (int skill = 0x22; skill < 0x28; ++skill) {
            if (g_skill_attributes[skill].attribute_1 == static_cast<int>(entry->m_id) &&
                m_character->skills[skill].active) {
                m_screen->ShowDescription(entry->m_id, skill);
            }
        }
    }
}

void W8CharacterStatsPage::ShowEntryInfo(W8CharacterPageEntry* entry)
{
    m_screen->ShowPrimaryAttributeInfo(static_cast<W8Attribute>(entry->m_id));
}

/* A value row moved: rerun the whole creation rebuild for the new
   profession, race or sex, then refresh the row controls. */
void W8CharacterStatsPage::OnRowValueChanged(W8CharacterStatsRow* row, int value)
{
    if (row == m_profession_row.get()) {
        RebuildLevelUpPoolsForProfession(m_character, m_creation_state,
                                         static_cast<W8Profession>(value));
    } else if (row == m_race_row.get()) {
        SetCharacterRace(m_character, m_creation_state, static_cast<W8Race>(value));
    } else {
        SetCharacterGender(m_character, m_creation_state, static_cast<W8Gender>(value));
    }
    m_contentDirty = true;
    m_screen->UpdateNavigation(this);
    Refresh();
}

void W8CharacterStatsPage::SetRowControlsActive(W8CharacterStatsRow* row, bool active)
{
    if (row == m_profession_row.get()) {
        m_profession_row->m_increment->SetVisible(active);
        m_race_row->m_increment->SetVisible(active);
        m_gender_row->m_increment->SetVisible(active);
    } else if (row == m_race_row.get()) {
        m_race_row->m_increment->SetVisible(active);
        m_gender_row->m_increment->SetVisible(active);
    } else {
        m_gender_row->m_increment->SetVisible(active);
    }
    for (const auto& entry : m_entries) {
        entry->SetHelpActive(active);
    }
    for (int control_index = 0; control_index < 5; ++control_index) {
        m_attribute_controls[control_index]->setEnabled(active);
    }
}

/* A row opened its record list: park the row and attribute controls so the
   list owns the input. */
void W8CharacterStatsPage::OnRowExpanded(W8CharacterStatsRow* row)
{
    SetRowControlsActive(row, false);
}

/* The record list closed: restore the row and attribute controls. */
void W8CharacterStatsPage::OnRowCollapsed(W8CharacterStatsRow* row)
{
    SetRowControlsActive(row, true);
    InvalidateAll();
}

/* Right-clicking a row's value asks the screen for that entry's info. */
void W8CharacterStatsPage::OnRowInfoRequested(W8CharacterStatsRow* row, int value)
{
    if (row == m_profession_row.get()) {
        m_screen->ShowProfessionInfo(static_cast<W8Profession>(value));
        return;
    }
    if (row == m_race_row.get()) {
        m_screen->ShowRaceInfo(static_cast<W8Race>(value));
    }
}

void W8CharacterStatsPage::Prepare()
{
    W8CharacterPage::Prepare();
    m_profession_row->Invalidate();
    m_race_row->Invalidate();
    m_gender_row->Invalidate();
}

/* The whole page: three value rows, seven attribute entries, five attribute
   corner controls, then the profession/race/sex list state. */
void W8CharacterStatsPage::SetCharacter(W8Character* character,
                                        W8CharacterCreationState* creation_state,
                                        W8CharacterScreenMode mode)
{
    W8CharacterPage::SetCharacter(character, creation_state, mode);
    m_profession_row = std::make_unique<W8CharacterStatsRow>(
        *this, 0x16, 10, 15, g_character_profession_records, &g_character_profession_default_record,
        0xf7, 0xf6, 0xf8);
    m_race_row =
        std::make_unique<W8CharacterStatsRow>(*this, 0x16, 0x3d, 11, g_character_race_records,
                                              &g_character_race_default_record, 0xfa, 0xf9, 0xfb);
    m_gender_row =
        std::make_unique<W8CharacterStatsRow>(*this, 0x16, 0x70, 2, g_character_gender_records,
                                              &g_character_gender_default_record, 0xfd, 0xfc, 0xfe);

    if (mode == W8_CHARACTER_CREATE) {
        m_profession_row->SetEnabled(true);
        m_race_row->SetEnabled(true);
        m_gender_row->SetEnabled(true);
    } else if (mode == W8_CHARACTER_EDIT) {
        m_profession_row->SetEnabled(false);
        m_race_row->SetEnabled(false);
        m_gender_row->SetEnabled(false);
    } else if (mode == W8_CHARACTER_LEVEL_UP) {
        if (character->iRace == W8_RACE_ANDROID) {
            m_profession_row->SetEnabled(false);
        } else {
            m_profession_row->SetEnabled(true);
        }
        m_race_row->SetEnabled(false);
        m_gender_row->SetEnabled(false);
    }

    for (int attribute_index = 0; attribute_index < 7; ++attribute_index) {
        W8CharacterPageEntry* entry =
            new W8CharacterPageEntry(input, 0xe5, 0x1b + attribute_index * 0xe, false);
        AddEntry(entry);
        entry->on_adjust = [this, entry](int delta) { AdjustEntry(entry, delta); };
        entry->on_info = [this, entry] { ShowEntryInfo(entry); };
        entry->SetContent(attribute_index,
                          g_localized_strings[g_character_description_first_ids[attribute_index]],
                          &character->attributes[attribute_index].base,
                          &creation_state->attribute_values[attribute_index],
                          &creation_state->attribute_limits[attribute_index], 0x101);
        entry->SetEnabled(false);
    }
    nav_next_state = false;
    m_rows_initialized = false;

    for (int index = 0; index < 5; ++index) {
        auto& target = input.add(
            SDL_Rect{0xc3 + 0xf3, 0x2b + 0xba + index * 14, 0xa3, 13},
            [this, index](const UiPointerEvent& event) {
                if (event.action == UiPointerAction::Up && event.inside &&
                    event.button == UiPointerButton::Right && event.captured_buttons)
                    AfterUiDispatch([this, index] { m_screen->ShowSecondaryAttributeInfo(index); });
                return UiResult::Handled;
            });
        target.setTooltip([] { return std::string(g_localized_strings[0x101]); });
        target.setEnabled(false);
        m_attribute_controls[index] = &target;
    }
}

/* The page redraw: header figures, the seven attribute entries' section,
   the three value rows' labels, then the resistance and trait blocks. */
void W8CharacterStatsPage::Redraw(SDL_Surface& target, bool full_redraw)
{
    bool redraw = static_cast<unsigned char>(m_active && m_dirty);
    W8TextBuffer text;
    int left = W8CharacterPage::bounds.x;
    int top = W8CharacterPage::bounds.y;
    W8CharacterPage::Redraw(target, full_redraw);
    m_profession_row->Draw(target, redraw || full_redraw);
    m_race_row->Draw(target, redraw || full_redraw);
    m_gender_row->Draw(target, redraw || full_redraw);
    if (redraw) {
        W8ControlsRect bounds;
        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        bounds.left = left + 0xe5;
        bounds.right = left + 0x1a8;
        bounds.top = top + 0xe;
        bounds.bottom = top + 0x1a;
        text.RenderString(&bounds, g_localized_strings[0x96], g_wiz_text_font_secondary, false,
                          target);

        if (m_character->iRace != W8_RACE_NONE) {
            bounds.top = top + 0x7e;
            bounds.bottom = top + 0x8a;
            bounds.left = left + 0xf9;
            bounds.right = left + 0x10f;
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            text.RenderString(fmt::format("{:d}", m_creation_state->attribute_points_remaining),
                              g_wiz_text_font_secondary, false, target);
        }

        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        bounds.right = left + 0x1a8;
        bounds.top = top + 0x7e;
        bounds.bottom = top + 0x8a;
        bounds.left = left + 0x116;
        int saved_right = bounds.right;
        text.RenderString(&bounds, g_localized_strings[0x87], g_wiz_text_font_secondary, false,
                          target);

        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        bounds.left = left + 0x1a;
        bounds.right = left + 0xe4;
        bounds.top = top + 0xac;
        bounds.bottom = top + 0xb8;
        text.RenderString(&bounds, g_localized_strings[0xb1], g_wiz_text_font_secondary, false,
                          target);

        text.SetLayoutMode(g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft);
        bounds.left = left + 0x1d;
        bounds.bottom = top + 0x133;
        bounds.top = top + 0xbb;
        bounds.right = left + 0xe6;
        if (m_character->iProfession != W8_PROFESSION_NONE || m_character->iRace != W8_RACE_NONE) {
            unsigned char available[0x20];
            unsigned int available_count = 0;
            for (int trait = 0; trait < 0x20; ++trait) {
                if (CharacterHasTrait(m_character, static_cast<W8Trait>(trait))) {
                    available[trait] = 1;
                    ++available_count;
                } else {
                    available[trait] = 0;
                }
            }
            int line_height = (available_count < 8) + 0xd;
            if (m_character->iProfession != W8_PROFESSION_NONE) {
                text.SetClipBounds(bounds);
                text.SetLayoutBounds(&bounds, true);
                text.RenderString(
                    fmt::format("{} {}",
                                g_localized_strings
                                    [g_character_skill_name_ids
                                         [g_profession_bonus_skills[m_character->iProfession]]],
                                g_localized_strings[0xb2]),
                    g_wiz_text_font_secondary, false, target);
                bounds.top += line_height;
            }
            for (int trait_index = 0; trait_index < 0x20; ++trait_index) {
                if (available[trait_index] != 0) {
                    text.RenderString(&bounds,
                                      g_localized_strings[g_character_trait_name_ids[trait_index]],
                                      g_wiz_text_font_secondary, false, target);
                    bounds.top += line_height;
                }
            }
        }

        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        bounds.left = left + 0xf3;
        bounds.right = left + 0x1a7;
        bounds.top = top + 0xac;
        bounds.bottom = top + 0xb8;
        text.RenderString(&bounds, g_localized_strings[0xa6], g_wiz_text_font_secondary, false,
                          target);

        bounds.left = left + 0xf8;
        bounds.top = top + 0xba;
        bounds.bottom = top + 0xc6;
        bounds.right = left + 0x195;
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text.RenderString(&bounds, g_localized_strings[0xa7], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(fmt::format("{:d}", m_character->uiHPMax), g_wiz_text_font_secondary,
                              false, target);
        }

        bounds.top += 0xe;
        bounds.bottom += 0xe;
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text.RenderString(&bounds, g_localized_strings[0xa9], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(fmt::format("{:d}", m_character->uiStaminaMax),
                              g_wiz_text_font_secondary, false, target);
        }

        bounds.bottom += 0xe;
        bounds.top += 0xe;
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text.RenderString(&bounds, g_localized_strings[0xed], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(
                fmt::format("{:d}", ComputeLevelUpSpellPointAward(m_character, m_creation_state)),
                g_wiz_text_font_secondary, false, target);
        }

        bounds.top += 0xe;
        bounds.bottom += 0xe;
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text.RenderString(&bounds, g_localized_strings[0xad], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(fmt::format("{:d}", m_character->armor_class_average),
                              g_wiz_text_font_secondary, false, target);
        }

        bounds.top += 0xe;
        bounds.bottom += 0xe;
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text.RenderString(&bounds, g_localized_strings[0xaf], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(fmt::format("{:d}", m_character->carrying_capacity / 10),
                              g_wiz_text_font_secondary, false, target);
        }

        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        bounds.left = left + 0x1a;
        bounds.right = left + 0xe4;
        bounds.top = top + 0x140;
        bounds.bottom = top + 0x14c;
        text.RenderString(&bounds, g_localized_strings[0xb9], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iRace != W8_RACE_NONE || m_character->iProfession != W8_PROFESSION_NONE) {
            for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
                bool first_column = (realm & 1) == 0;
                bounds.left = (realm / 2) * 0x3f + 0x39 + left;
                bounds.top = (first_column ? 0x157 : 0x171) + top;
                bounds.bottom = bounds.top + 0xe;
                bounds.right = bounds.left + 0x21;
                text.SetClipBounds(bounds);
                text.SetLayoutBounds(&bounds, true);
                int value = m_character->resistances[realm].total - 0x19;
                std::string value_text =
                    value == 0 ? std::string(g_dash) : fmt::format("{:+d}", value);
                text.RenderString(value_text, g_wiz_text_font_secondary, false, target);
            }
        }
        for (int realm_index = 0; realm_index < 6; ++realm_index) {
            bool first_column = (realm_index & 1) == 0;
            int frame_top = (first_column ? 0x155 : 0x16f) + top;
            CatalogArtwork::Load(g_character_resistance_images[realm_index], 0)
                .drawAndInvalidate(target, g_spell_realm_animations[realm_index].initial_frame,
                                   (realm_index / 2) * 0x3f + 0x23 + left, frame_top);
        }

        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        bounds.left = left + 0xf3;
        bounds.right = left + 0x1a7;
        bounds.top = top + 0x120;
        bounds.bottom = top + 0x12c;
        text.RenderString(&bounds, g_localized_strings[0xb3], g_wiz_text_font_secondary, false,
                          target);
        if (m_character->iProfession != W8_PROFESSION_NONE) {
            text.SetLayoutMode(g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft);
            bounds.top = top + 0x12f;
            bounds.bottom = top + 0x13b;
            bounds.left = left + 0xf6;
            bounds.right = left + 0x1a9;
            text.RenderString(
                &bounds,
                g_localized_strings[g_character_skill_name_ids
                                        [g_profession_bonus_skills[m_character->iProfession]]],
                g_wiz_text_font_secondary, false, target);
            for (int index = 0; index < 4; ++index) {
                if (g_profession_skills[m_character->iProfession][index] != -1) {
                    bounds.top += 0xe;
                    bounds.bottom += 0xe;
                    text.RenderString(
                        &bounds,
                        g_localized_strings
                            [g_character_skill_name_ids
                                 [g_profession_skills[m_character->iProfession][index]]],
                        g_wiz_text_font_secondary, false, target);
                }
            }
            text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            bounds.left = left + 0x106;
            bounds.right = left + 0x11c;
            bounds.top = top + 0x179;
            bounds.bottom = top + 0x185;
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            text.RenderString(fmt::format("{:d}", m_creation_state->skill_points_total),
                              g_wiz_text_font_secondary, false, target);
        }
        text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        bounds.top = top + 0x179;
        bounds.left = left + 0x123;
        bounds.bottom = top + 0x185;
        bounds.right = saved_right;
        text.RenderString(&bounds, g_localized_strings[0x87], g_wiz_text_font_secondary, false,
                          target);
    }

    text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (m_prepared) {
        W8ControlsRect bounds = {4, 0xec, 0xc2, 0x162};
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        int text_id;
        if (m_mode == W8_CHARACTER_CREATE) {
            text_id = 0xe5;
        } else if (m_character->iRace == W8_RACE_ANDROID) {
            text_id = 0xe7;
        } else {
            text_id = 0xe6;
        }
        text.RenderString(g_localized_strings[text_id], g_wiz_text_font_secondary, true, target);
        bounds.top = 0x162;
        bounds.bottom = 0x179;
        bounds.right = 0x8f;
        text.RenderString(&bounds, g_localized_strings[0xe2], g_wiz_text_font_secondary, true,
                          target);
        bounds.top = 0x184;
        bounds.bottom = 0x19b;
        text.RenderString(&bounds, g_localized_strings[0xe4], g_wiz_text_font_secondary, true,
                          target);
        m_prepared = false;
    }

    if (m_contentDirty) {
        W8ControlsRect bounds = {0x8f, 0x162, 0xbf, 0x179};
        CatalogArtwork::Load(0x107, 0).draw(target, 5, 0x8f, 0x162);
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(fmt::format("{:d}", m_creation_state->attribute_step_limit),
                          g_options_detail_font, true, target);
        bounds.top = 0x184;
        bounds.bottom = 0x19b;
        CatalogArtwork::Load(0x107, 0).draw(target, 5, 0x8f, 0x184);
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        int total = m_creation_state->attribute_points_total;
        std::string points_text =
            total < 1
                ? g_zero_slash_zero
                : fmt::format("{:d}/{:d}", m_creation_state->attribute_points_remaining, total);
        text.RenderString(points_text, g_options_detail_font, true, target);
        m_contentDirty = false;
    }

    m_profession_row->DrawChoices(target);
    m_race_row->DrawChoices(target);
    m_gender_row->DrawChoices(target);
}

/* Factory for the stats page; the render target comes from the base
   constructor. */
W8CharacterStatsPage* CreateCharacterStatsPage()
{
    return new W8CharacterStatsPage;
}

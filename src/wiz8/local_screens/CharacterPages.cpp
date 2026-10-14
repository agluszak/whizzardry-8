#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/button_interaction.h"
#include "wiz8/engine_code/Video2.h"
#include <string>
#include <SDL3/SDL_rect.h>
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/ui_layer.h"
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <string_view>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/layouts/character.h"
#include "wiz8/local_code/ControlsRect.h"
#include "input.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/fonts.h"

struct W8CharacterCreationState;

namespace {
template<class T> T ReadStatValue(const void* address) {
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
}

// GLOBAL: WIZ8 0x006483d0
W8PortraitDescriptor g_portrait_descriptors[80] = {
    {0, 0, 0, 1},  {0, 0, 1, 0},  {0, 0, 2, 2},  {0, 0, 3, 0},  {0, 0, 3, 1},  {0, 0, 3, 2},
    {0, 1, 0, 2},  {0, 1, 1, 0},  {0, 1, 2, 0},  {0, 1, 3, 1},  {0, 1, 3, 0},  {0, 1, 3, 0},
    {1, 0, 0, 1},  {1, 0, 1, 1},  {1, 0, 2, 2},  {1, 1, 0, 1},  {1, 1, 1, 1},  {1, 1, 2, 0},
    {2, 0, 0, 1},  {2, 0, 1, 2},  {2, 0, 2, 1},  {2, 1, 0, 1},  {2, 1, 1, 1},  {2, 1, 2, 2},
    {3, 0, 1, 1},  {3, 0, 2, 2},  {3, 1, 0, 0},  {3, 1, 1, 1},  {4, 0, 0, 1},  {4, 0, 1, 1},
    {4, 1, 0, 1},  {4, 1, 1, 0},  {5, 0, 0, 0},  {5, 0, 2, 1},  {5, 1, 0, 1},  {5, 1, 1, 0},
    {6, 0, 0, 1},  {6, 0, 1, 2},  {6, 1, 0, 0},  {6, 1, 1, 2},  {7, 0, 0, 0},  {7, 0, 1, 2},
    {7, 1, 0, 2},  {7, 1, 1, 2},  {8, 0, 0, 0},  {8, 0, 1, 2},  {8, 1, 0, 2},  {8, 1, 1, 1},
    {9, 0, 1, 2},  {9, 0, 2, 1},  {9, 1, 0, 1},  {9, 1, 1, 1},  {10, 0, 0, 2}, {10, 0, 1, 1},
    {10, 1, 0, 2}, {10, 1, 1, 1}, {11, 0, 4, 0}, {11, 1, 4, 2}, {10, 0, 3, 2}, {11, 0, 1, 1},
    {11, 0, 0, 0}, {11, 0, 1, 0}, {12, 0, 3, 2}, {13, 0, 3, 1}, {13, 0, 3, 1}, {13, 0, 3, 2},
    {13, 0, 3, 2}, {13, 0, 3, 1}, {13, 0, 3, 0}, {13, 0, 3, 1}, {13, 0, 3, 1}, {13, 0, 3, 2},
    {13, 0, 3, 0}, {13, 0, 3, 1}, {13, 0, 3, 0}, {13, 0, 3, 0}, {0, 0, 3, 0},  {0, 1, 3, 2},
    {1, 0, 3, 1},  {1, 1, 3, 0},
};
// VTABLE: WIZ8 0x005ef1d8 W8CharacterPageEntry
// class W8CharacterPageEntry

// FUNCTION: WIZ8 0x005af690
W8CharacterPageEntry::W8CharacterPageEntry(UiLayer& input, int x, int y, bool compact)
    : m_input(input), m_first(0), m_second(0), m_third(0), m_id(-1), m_draw_background(compact),
      m_dirty(1), m_enabled(0), m_increment_allowed(1)
{
    m_x = 0xc3 + x;
    m_y = 0x2b + y;
    int split = m_x + (compact ? 0x6d : 0x78);
    W8ControlsRect bounds = {m_x + 5, m_y + 1, split, m_y + 0xd};
    m_label.SetLayoutBounds(&bounds, true, true);
    m_label.SetText({}, g_wiz_text_font_secondary);
    m_label.SetLayoutMode(g_W8TextBufferAlignLeft);
    bounds.left = split;
    bounds.right = split + 0x17;
    m_first_text.SetLayoutBounds(&bounds, true, true);
    m_first_text.SetText({}, g_wiz_text_font_secondary);
    m_first_text.SetLayoutMode(g_W8TextBufferAlignRight);
    bounds.left = split + 0x2a;
    bounds.right = split + 0x39;
    m_second_text.SetLayoutBounds(&bounds, true, true);
    m_second_text.SetText({}, g_wiz_text_font_secondary);
    m_second_text.SetLayoutMode(g_W8TextBufferAlignRight);

    const auto button = [this](int x, int frame, int direction) {
        auto result = std::make_unique<W8ButtonWidget>(m_input);
        const auto adjust = [this, direction](int amount) {
            if (on_adjust) on_adjust(direction * amount);
            UpdateButtons();
            MarkDirty();
        };
        result->on_right_click = [adjust](auto*) { AfterUiDispatch([adjust] { adjust(5); }); };
        result->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0x10a, 0).spriteSheet(), frame + 3, frame,
                                 frame + 1, frame + 2, frame + 4),
            [adjust](auto*) { AfterUiDispatch([adjust] { adjust(1); }); }, 0);
        result->policy.input_rule = ButtonInputRule::PrimaryPressedState;
        result->menu_artwork = true;
        result->policy.repeat = true;
        result->SetPosition(x, m_y + 1);
        result->SetVisible(false);
        return result;
    };
    m_decrement = button(split + 0x1b, 0x19, -1);
    m_decrement->SetEnabled(false);
    m_increment = button(split + 0x3d, 0x1e, 1);
    m_help = &m_input.add(SDL_Rect{m_x, m_y, split - m_x + 1, 13},
        [this](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Up && event.inside &&
                event.button == UiPointerButton::Right && event.captured_buttons && on_info)
                AfterUiDispatch(on_info);
            return UiResult::Handled;
        });
    m_help->setEnabled(false);
}

W8CharacterPageEntry::~W8CharacterPageEntry()
{
    m_input.remove(*m_help);
}

// FUNCTION: WIZ8 0x005af9e0
void W8CharacterPageEntry::SetContent(unsigned int id, std::string_view label, unsigned int* first,
                                      int* second, int* third, int help_id)
{
    m_id = id;
    m_first = first;
    m_second = second;
    m_third = third;
    m_label.SetText(label, g_wiz_text_font_secondary);
    if (help_id == -1)
        m_help->setTooltip({});
    else
        m_help->setTooltip([help_id] { return std::string(g_localized_strings[help_id]); });
    SetEnabled(true);
    UpdateButtons();
    MarkDirty();
}

// FUNCTION: WIZ8 0x005afc20
void W8CharacterPageEntry::SetIncrementAllowed(bool allowed)
{
    m_increment_allowed = allowed;
    UpdateButtons();
    MarkDirty();
}

// FUNCTION: WIZ8 0x005afa90
void W8CharacterPageEntry::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    m_increment->SetVisible(enabled);
    m_decrement->SetVisible(enabled);
    m_help->setEnabled(enabled);
    MarkDirty();
}

// FUNCTION: WIZ8 0x005afae0
void W8CharacterPageEntry::SetHelpActive(bool active)
{
    m_help->setEnabled(active);
}

// FUNCTION: WIZ8 0x005afaf0
void W8CharacterPageEntry::Redraw(SDL_Surface& target)
{
    m_dirty |= m_increment->m_dirty || m_decrement->m_dirty;
    if (m_enabled && m_dirty) {
        m_increment->m_dirty = m_decrement->m_dirty = true;
        if (m_draw_background) {
            CatalogArtwork::Load(0x108, 0).drawAndInvalidate(target, 2, m_x, m_y);
        }
        m_first_text.SetText(fmt::format("{:d}", ReadStatValue<unsigned int>(m_first)), g_wiz_text_font_secondary);
        m_second_text.SetText(fmt::format("{:d}", ReadStatValue<int>(m_second)), g_wiz_text_font_secondary);
        m_first_text.FillBounds(target, 0x8000);
        m_second_text.FillBounds(target, 0x8000);
        m_label.RenderToTarget(0, true, target);
        m_first_text.RenderToTarget(0, true, target);
        m_second_text.RenderToTarget(0, true, target);
        m_dirty = false;
    } else if (!m_draw_background && m_dirty) {
        m_label.RenderToTarget(0, true, target);
        m_dirty = false;
    }
    m_increment->DrawAndInvalidate(target);
    m_decrement->DrawAndInvalidate(target);
    m_increment->m_dirty = m_decrement->m_dirty = false;
}

// FUNCTION: WIZ8 0x005afbf0
void W8CharacterPageEntry::SetLabelFontState(int state)
{
    m_label.SetFontStateIndex(state);
}

// FUNCTION: WIZ8 0x005afc00
void W8CharacterPageEntry::MarkDirty()
{
    m_decrement->m_dirty = true;
    m_increment->m_dirty = true;
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005afd10
void W8CharacterPageEntry::UpdateButtons()
{
    if (m_enabled) {
        bool enabled = ReadStatValue<int>(m_second) > 0;
        if (m_decrement->IsEnabled() != enabled) {
            m_decrement->SetEnabled(enabled);
            m_decrement->m_dirty = true;
        }
        enabled = m_increment_allowed && ReadStatValue<int>(m_second) < ReadStatValue<int>(m_third);
        if (m_increment->IsEnabled() != enabled) {
            m_increment->SetEnabled(enabled);
            m_increment->m_dirty = true;
        }
    }
}

// VTABLE: WIZ8 0x005ef1e4 W8CharacterPage
// class W8CharacterPage

// FUNCTION: WIZ8 0x005afd90
W8CharacterPage::W8CharacterPage(CatalogArtwork background)
    : background(std::move(background)), m_screen(nullptr)
{
}

// FUNCTION: WIZ8 0x005afe40
W8CharacterPage::~W8CharacterPage() = default;

void W8CharacterPage::SetActive(bool active)
{
    m_active = active;
    input.setEnabled(active);
}

// FUNCTION: WIZ8 0x005aff00
void W8CharacterPage::SetCharacter(W8Character* character, W8CharacterCreationState* creation_state,
                                   int mode)
{
    m_character = character;
    m_creation_state = creation_state;
    m_mode = mode;
}

// FUNCTION: WIZ8 0x005aff20
void W8CharacterPage::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    if (m_dirty || full_redraw) {
        background.drawAndInvalidate(target, 0, bounds.x, bounds.y);
        for (const auto& entry : m_entries) entry->MarkDirty();
    }
    m_dirty = false;
    for (int index = 0; index < static_cast<int>(m_entries.size()); ++index) {
        m_entries[index]->Redraw(target);
    }
}

// FUNCTION: WIZ8 0x005aff50
void W8CharacterPage::InvalidateAll()
{
    m_dirty = true;
    for (const auto& entry : m_entries) {
        entry->MarkDirty();
    }
}

// FUNCTION: WIZ8 0x005affa0
void W8CharacterPage::Prepare()
{
    InvalidateAll();
    m_contentDirty = true;
    m_prepared = true;
}

// FUNCTION: WIZ8 0x005affc0
void W8CharacterPage::AddEntry(W8CharacterPageEntry* entry) { m_entries.emplace_back(entry); }

void W8CharacterPage::HandleInput(const InputEvent*) {}
void W8CharacterPage::Refresh() {}

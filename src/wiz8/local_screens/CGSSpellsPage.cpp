#include <array>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "local_code/CharGeneration.h"
#include "local_code/ControlsRect.h"
#include "local_code/HealthStaminaMana.h"
#include "local_code/TextBuffer.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/fonts.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/screen.h"
#include "wiz8/ui_layer.h"

W8SpellRealmAnimation g_spell_realm_animations[6] = {{486, 22, 3}, {487, 18, 8},  {488, 14, 8},
                                                     {489, 21, 1}, {490, 16, 14}, {491, 24, 11}};

class W8CharacterSpellList {
public:
    W8CharacterSpellList(W8CharacterSpellsPage& owner, int x, int y,
                         W8CharacterSpellEntry* entries);
    ~W8CharacterSpellList() { m_owner.input.remove(*m_target); }
    W8CharacterSpellList(const W8CharacterSpellList&) = delete;
    void Redraw(SDL_Surface& target, bool force);
    void SetEntryCount(int count);
    W8CharacterSpellsPage& m_owner;
    HitTarget* m_target;
    SDL_Rect m_bounds;
    bool m_dirty = true;
    W8RangeControl m_range;
    W8CharacterSpellEntry* m_entries;
    int m_first_entry = 0;
    int m_entry_count = 0;
    int m_hovered_entry = -1;
    int m_scroll_offset = 0;
};

W8CharacterSpellList::W8CharacterSpellList(W8CharacterSpellsPage& owner, int x, int y,
                                           W8CharacterSpellEntry* entries)
    : m_owner(owner), m_bounds{0xc3 + x + 0x1a, 0x2b + y + 0x19, 0x9a, 0x60},
      m_range(owner.input, 0xc3 + x + 0xb8, 0x2b + y + 0x1a, 0xc3 + x + 0xca, 0x2b + y + 0x78),
      m_entries(entries)
{

    m_range.on_change = [this](auto* range) {
        m_scroll_offset = range->m_value;
        m_dirty = true;
    };
    m_target = &owner.input.add(
        SDL_Rect{m_bounds.x, m_bounds.y, m_bounds.w + 1, m_bounds.h + 1},
        [this](const UiPointerEvent& event) {
            const int entry =
                (event.position.y - m_bounds.y) / 13 + m_scroll_offset + m_first_entry;
            const bool valid = entry >= m_first_entry && entry < m_first_entry + m_entry_count;
            if ((valid) && (event.action != UiPointerAction::Enter &&
                            (event.action != UiPointerAction::Up || event.captured_buttons)))
                PlayButtonSound(event.action);
            if (event.action == UiPointerAction::Leave) {
                m_hovered_entry = -1;
                m_dirty = true;
            } else if (event.action == UiPointerAction::Move ||
                       event.action == UiPointerAction::Enter) {
                const int hovered = valid ? entry : -1;
                if (hovered != m_hovered_entry) {
                    m_hovered_entry = hovered;
                    m_dirty = true;
                }
            } else if (event.action == UiPointerAction::Wheel) {
                m_range.AdjustValue(event.wheel);
            } else if (event.action == UiPointerAction::Up && event.inside &&
                       event.captured_buttons && valid) {
                if (event.button == UiPointerButton::Right)
                    AfterUiDispatch([this, entry] { m_owner.ShowSpellInfo(entry); });
                else if (m_entries[entry].fSelectable) {
                    m_dirty = true;
                    AfterUiDispatch([this, entry] { m_owner.SelectSpell(entry); });
                }
            }
            return UiResult::Handled;
        });
}

W8CharacterSpellsPage::W8CharacterSpellsPage()
    : W8CharacterPage(CatalogArtwork::Load(0x109)), anim_timer(0.05f, TimerClock::Presentation)
{
}
W8CharacterSpellsPage::~W8CharacterSpellsPage() = default;

void W8CharacterSpellList::SetEntryCount(int count)
{
    m_range.InvalidateAll();
    m_range.SetActive(true);
    m_entry_count = count;
    m_dirty = true;
    if (count > 7) {
        m_range.SetRange(0, count - 7);
        m_range.SetEnabled(true);
    } else {
        m_range.SetEnabled(false);
    }
}

void W8CharacterSpellList::Redraw(SDL_Surface& target, bool force)
{
    if (m_dirty || force) {
        const auto [left, top, width, height] = m_bounds;
        InvalidateRegion(left, top, left + width, top + height);
        CatalogArtwork::Load(0x1b6, 0).drawRect(target, SDL_Rect{0, 0, width, height}, left, top);
        int y = top + 1;
        g_wiz_text_font_secondary.resetPalette();
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        for (int index = m_first_entry + m_scroll_offset;
             index < m_first_entry + m_scroll_offset + 7; ++index) {
            if (index >= m_first_entry + m_entry_count) break;
            if (!m_entries[index].fSelectable) {
                g_wiz_text_font_secondary.setShade(6);
            } else if (m_entries[index].selected) {
                g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_BLUE]);
            } else if (index == m_hovered_entry) {
                g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
            }
            DrawText(target, g_wiz_text_font_secondary, {left + 2, y},
                     Data().spells[m_entries[index].spell].display_name);
            std::string cost;
            cost = fmt::format("{:d}", Data().spells[m_entries[index].spell].spell_point_cost);
            DrawText(target, g_wiz_text_font_secondary,
                     {left + width - g_wiz_text_font_secondary.textWidth(cost) - 2, y}, cost);
            y += 13;
            g_wiz_text_font_secondary.resetPalette();
            g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        }
        m_range.InvalidateAll();
        m_dirty = false;
    }
}

void W8CharacterSpellsPage::SetCharacter(W8Character* character,
                                         W8CharacterCreationState* creation_state,
                                         W8CharacterScreenMode mode)
{
    W8CharacterPage::SetCharacter(character, creation_state, mode);
    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        m_realms[realm] = std::make_unique<W8CharacterSpellList>(
            *this, (realm % 2) * 215 + 13, (realm / 2) * 130 + 8, m_SpellData);
    }
}

void W8CharacterSpellsPage::Activate()
{
    input.setEnabled(true);
    UpdateSpellLists();
    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        m_realms[realm]->m_range.SetActive(true);
    }
    m_contentDirty = true;
    m_prepared = true;
}

void W8CharacterSpellsPage::Deactivate()
{
    input.setEnabled(false);
    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        m_realms[realm]->m_range.SetActive(false);
    }
}

/* Drop every pending pick: clear the per-entry selected flags, return the
   creation-state spell selections, and repaint. */
void W8CharacterSpellsPage::Accept()
{
    for (auto& index : m_SpellData) {
        index.selected = false;
    }
    ResetSpellSelections(m_character, m_creation_state);
    InvalidateAll();
    m_contentDirty = true;
    m_screen->UpdateNavigation(this);
}

/* Rebuilds each realm's list from the character's spell_learned states: pass
   one collects the known (-1) and pending (2) spells as selectable entries,
   pass two appends the learnable (1) spells greyed out. */
/* Repaint the spell-point instructions and pool, the six realm headings and
   values, then the animated realm icons and their range controls. */
void W8CharacterSpellsPage::Redraw(SDL_Surface& target, bool full_redraw)
{
    bool redraw = m_active && m_dirty;
    W8TextBuffer text;
    W8ControlsRect bounds;

    W8CharacterPage::Redraw(target, full_redraw);
    text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (m_prepared) {
        bounds.left = 4;
        bounds.top = 0xec;
        bounds.right = 0xc2;
        bounds.bottom = 0x173;
        text.RenderString(&bounds,
                          Data().strings[m_creation_state->spell_points_total == 0 ? 0xeb : 0xea],
                          g_wiz_text_font_secondary, true, target);

        bounds.top = 0x173;
        bounds.bottom = 0x18a;
        bounds.right = 0x8f;
        text.RenderString(&bounds, Data().strings[0xf4], g_wiz_text_font_secondary, true, target);
        m_prepared = false;
    }

    if (m_contentDirty) {
        bounds.left = 0x8f;
        bounds.top = 0x173;
        bounds.right = 0xbf;
        bounds.bottom = 0x18a;
        CatalogArtwork::Load(0x107, 0).draw(target, 5, 0x8f, 0x173);
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(fmt::format("{:d}/{:d}", m_creation_state->spell_points_remaining,
                                      m_creation_state->spell_points_total),
                          g_options_detail_font, true, target);
        m_contentDirty = false;
    }

    if (redraw) {
        for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
            if (m_realms[realm]->m_entry_count == 0) {
                continue;
            }
            text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
            bounds.left = W8CharacterPage::bounds.x + 0x29 + (realm % 2) * 0xd7;
            bounds.top = W8CharacterPage::bounds.y + 0x0f + (realm / 2) * 0x82;
            bounds.right = bounds.left + 0x3c;
            bounds.bottom = bounds.top + 0x0e;
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            text.SetFontStateIndex(1);
            text.RenderString(fmt::format("{}:", Data().strings[0xf2]), g_wiz_text_font_secondary,
                              false, target);
            text.SetFontStateIndex(-1);
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(
                fmt::format("{:d}", m_character->skills[W8_SKILL_FIRE_MAGIC + realm].points),
                g_wiz_text_font_secondary, false, target);

            text.SetLayoutMode(g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
            bounds.left += 0x58;
            bounds.right = bounds.left + 0x53;
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            text.SetFontStateIndex(1);
            text.RenderString(fmt::format("{}:", Data().strings[0xf3]), g_wiz_text_font_secondary,
                              false, target);
            text.SetFontStateIndex(-1);
            text.SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
            text.RenderString(fmt::format("{:d}/{:d}",
                                          GetCharacterRealmSpellPoints(
                                              m_character, static_cast<W8SpellRealm>(realm)),
                                          m_character->sp_max[realm]),
                              g_wiz_text_font_secondary, false, target);
        }
    }

    int elapsed = static_cast<int>(anim_timer.GetProgress());
    if (elapsed > 0 || redraw) {
        for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
            const W8SpellRealmAnimation& animation = g_spell_realm_animations[realm];
            if (m_realms[realm]->m_entry_count != 0 && elapsed != 0) {
                m_animation_frames[realm] =
                    (m_animation_frames[realm] + elapsed) % animation.frame_count;
            }
            if (!m_screen->HasDialog() || realm < 2) {
                CatalogArtwork::Load(animation.image, 0)
                    .drawAndInvalidate(target, m_animation_frames[realm],
                                       W8CharacterPage::bounds.x + 0x0f + (realm % 2) * 0xd7,
                                       W8CharacterPage::bounds.y + 0x0a + (realm / 2) * 0x82);
            }
        }
    }
    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        m_realms[realm]->Redraw(target, redraw || full_redraw);
        m_realms[realm]->m_range.Redraw(target, false);
    }
}

void W8CharacterSpellsPage::Refresh()
{
    UpdateSpellLists();
}

void W8CharacterSpellsPage::UpdateSpellLists()
{
    int entry = 0;
    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        m_animation_frames[realm] = g_spell_realm_animations[realm].initial_frame;
        int first = entry;
        for (signed char pass = 0; pass < 2; ++pass) {
            for (unsigned int spell = 0; spell < 0x72; ++spell) {
                if (Data().spells[spell].realm == realm) {
                    switch (m_character->spell_learned[spell]) {
                    case -1:
                    case 2:
                        if (pass == 0) {
                            m_SpellData[entry].realm = static_cast<W8SpellRealm>(realm);
                            m_SpellData[entry].spell = spell;
                            m_SpellData[entry].fSelectable = true;
                            m_SpellData[entry].selected = m_character->spell_learned[spell] == 2;
                            ++entry;
                        }
                        break;
                    case 1:
                        if (pass == 1) {
                            m_SpellData[entry].realm = static_cast<W8SpellRealm>(realm);
                            m_SpellData[entry].spell = spell;
                            m_SpellData[entry].fSelectable = false;
                            m_SpellData[entry].selected = false;
                            ++entry;
                        }
                        break;
                    }
                }
            }
        }
        m_realms[realm]->m_first_entry = first;
        m_realms[realm]->SetEntryCount(entry - first);
    }
}

void W8CharacterSpellsPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    *next_enabled = 1;
    *exit_enabled = m_creation_state->spell_points_remaining < m_creation_state->spell_points_total;
}

/* Retail emits this with the W8CharacterSpellListListener-adjusted `this`
   (page + 0x70): the slot is only ever dispatched through that base. */
void W8CharacterSpellsPage::SelectSpell(unsigned int uiSelected)
{
    assert(m_SpellData[uiSelected].fSelectable);
    if (m_SpellData[uiSelected].selected) {
        m_SpellData[uiSelected].selected = false;
        DeselectCreationSpell(m_character, m_creation_state, m_SpellData[uiSelected].spell);
    } else {
        if (m_creation_state->spell_points_remaining == 0) {
            for (unsigned int index = 0; index < 0x72; ++index) {
                if (m_SpellData[index].selected &&
                    (index != m_last_selected || m_creation_state->spell_points_total == 1)) {
                    m_SpellData[index].selected = false;
                    DeselectCreationSpell(m_character, m_creation_state, m_SpellData[index].spell);
                    break;
                }
            }
        }
        m_SpellData[uiSelected].selected = true;
        SelectCreationSpell(m_character, m_creation_state, m_SpellData[uiSelected].spell);
        m_last_selected = uiSelected;
    }
    UpdateSpellLists();
    m_contentDirty = true;
    m_screen->UpdateNavigation(this);
    InvalidateAll();
}

void W8CharacterSpellsPage::ShowSpellInfo(unsigned int entry)
{
    m_screen->ShowSpellInfo(m_SpellData[entry].spell);
}

W8CharacterSpellsPage* CreateCharacterSpellsPage()
{
    return new W8CharacterSpellsPage();
}

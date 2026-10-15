#include "wiz8/dialog_code/NpcDialog.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "Font.h"
#include "input.h"
#include "local_code/ControlsRect.h"
#include "npc_script_file.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

/* Dialog Code\NpcDialog.cpp. The NPC dialogue popup; see the header for the
   request-buffer layout. The option-list strings and the two price-check
   strings live in the g_localized_strings message table. */

// FUNCTION: WIZ8 0x005DA6B0
W8NpcDialog::W8NpcDialog(W8NpcQuoteEntry* message, int aux_data,
                         std::function<void(W8NpcDialog&)> closed)
    : m_closed(std::move(closed))
{
    short height = 0x46;
    short max_width = -1;
    short width = 0;
    int index;
    std::string line;

    m_input_group = std::make_unique<TextFieldGroup>(m_input, g_wiz_text_mono_font);
    m_message = message;
    m_aux_data = aux_data;

    char opcode = message->kind;
    if (opcode == W8_NPC_ENTRY_OPTIONS) {
        if (message->sub_entry_count == 2) {
            m_compact_options = false;
        } else {
            m_compact_options = true;
        }
        for (index = 0; index < message->sub_entry_count; ++index) {
            line = message->sub_entries[index].text;
            short length = g_wiz_text_mono_font.textWidth(line);
            if (max_width < length) {
                max_width = length;
            }
        }
        m_text_width = max_width + 6;
        width = (m_text_width + 0xa) * message->sub_entry_count + 0x1e;
    } else if (opcode == W8_NPC_ENTRY_PRICE_CHECK || opcode == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) {
        m_compact_options = false;
        short length = g_wiz_text_mono_font.textWidth(g_localized_strings[0x7df]);
        height = 0x50;
        m_text_width = length + 6;
        width = m_text_width * 2 + 0x32;
    } else if (opcode == W8_NPC_ENTRY_KEYWORD_INPUT || !m_compact_options) {
        width = 200;
    }
    /* A computed width under 200 only sticks when the compact flag is set,
       so a two-option list keeps the narrow width. */
    if (width < 200 && !m_compact_options) {
        width = 200;
    }
    m_bounds = {(640 - width) / 2, 184, width, height};
}

// FUNCTION: WIZ8 0x005DA860
W8NpcDialog::~W8NpcDialog() = default;

// FUNCTION: WIZ8 0x005DA8B0
int W8NpcDialog::CreateControls()
{
    int index;
    std::string line;
    W8ControlsRect bounds;

    m_background.generic_image = LoadGenericButtonImages(
        {}, "Data\\Dialogs\\DialogBorder.STI", {}, "Data\\Dialogs\\DialogBorder.STI", {},
        "Data\\Dialogs\\DialogBackground.sti", 0, 0, 0);
    if (!m_background.generic_image) return 4;
    m_input.add(SDL_Rect{m_bounds.x, m_bounds.y, m_bounds.w + 1, m_bounds.h + 1}, {},
                UI_PRIORITY_BACKGROUND);
    if (m_message->kind == W8_NPC_ENTRY_OPTIONS) {
        int x = (m_bounds.w -
                 static_cast<short>((m_text_width + 10) * m_message->sub_entry_count - 10)) /
                2;
        for (index = 0; index < m_message->sub_entry_count; ++index) {
            line = m_message->sub_entries[index].text;
            m_buttons[index] = std::make_unique<W8ButtonWidget>(m_input);
            m_buttons[index]->ConfigureTextButton(line, g_wiz_text_mono_font, 4, 5,
                                                  {static_cast<short>(m_bounds.x + x),
                                                   static_cast<short>(m_bounds.y + 0x23),
                                                   m_text_width, 0x14},
                                                  {.on_click = [this, index](W8ButtonWidget*) {
                                                      m_selected_option = index;
                                                      m_keep_open = false;
                                                  }});
            x += m_text_width + 10;
        }
        bounds.left = m_bounds.x + 10;
        bounds.top = m_bounds.y + 10;
        bounds.right = m_bounds.x + m_bounds.w - 10;
        bounds.bottom = m_bounds.y + 0x19;
        m_text_buffers[0] = std::make_unique<W8TextBuffer>(
            &bounds, g_localized_strings[0x7e4], g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    } else if (m_message->kind == W8_NPC_ENTRY_PRICE_CHECK ||
               m_message->kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) {
        short x = static_cast<short>((m_bounds.w - static_cast<short>((m_text_width + 5) * 2)) / 2);
        m_buttons[0] = std::make_unique<W8ButtonWidget>(m_input);
        m_buttons[0]->ConfigureTextButton(g_localized_strings[0x7df], g_wiz_text_mono_font, 4, 5,
                                          {static_cast<short>(m_bounds.x + x),
                                           static_cast<short>(m_bounds.y + 0x1e), m_text_width,
                                           0x14},
                                          {.on_click = [this](W8ButtonWidget*) {
                                              m_selected_option = 0;
                                              m_keep_open = false;
                                          }});
        m_buttons[1] = std::make_unique<W8ButtonWidget>(m_input);
        m_buttons[1]->ConfigureTextButton(g_localized_strings[0x7e0], g_wiz_text_mono_font, 4, 5,
                                          {static_cast<short>(m_bounds.x + x + m_text_width + 10),
                                           static_cast<short>(m_bounds.y + 0x1e), m_text_width,
                                           0x14},
                                          {.on_click = [this](W8ButtonWidget*) {
                                              m_selected_option = 1;
                                              m_keep_open = false;
                                          }});
        line = fmt::sprintf(g_localized_strings[0x7e5], g_npc_dialogue.price_check()->price);
        bounds.left = m_bounds.x + 10;
        bounds.top = m_bounds.y + 10;
        bounds.right = m_bounds.x + m_bounds.w - 10;
        bounds.bottom = m_bounds.y + 0x19;
        m_text_buffers[0] = std::make_unique<W8TextBuffer>(
            &bounds, line, g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        line = fmt::format("{} {:d}g", g_localized_strings[0x7e6], g_status.party_gold);
        bounds.left = m_bounds.x + 10;
        bounds.top = m_bounds.y + 0x37;
        bounds.right = m_bounds.x + m_bounds.w - 10;
        bounds.bottom = m_bounds.y + 0x46;
        m_text_buffers[1] = std::make_unique<W8TextBuffer>(
            &bounds, line, g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    } else if (m_message->kind == W8_NPC_ENTRY_KEYWORD_INPUT) {
        m_input_group->SetScheme(1);
        m_input_field = m_input_group->Add(m_bounds.x + (m_bounds.w - 0x8c) / 2, m_bounds.y + 0x23,
                                           0x8c, 0x10, 0x7f, "", 0x28, 0xf, true);
        m_input_group->Focus(m_input_field);
        bounds.left = m_bounds.x + 10;
        bounds.top = m_bounds.y + 10;
        bounds.right = m_bounds.x + m_bounds.w - 10;
        bounds.bottom = m_bounds.y + 0x19;
        m_text_buffers[0] = std::make_unique<W8TextBuffer>(
            &bounds, g_localized_strings[0x7e4], g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    }
    m_initialized = true;
    return 0;
}

// FUNCTION: WIZ8 0x005DAE00
void W8NpcDialog::DrawModal(SDL_Surface& target)
{
    int index;
    if (m_dirty) {
        if (!m_initialized && CreateControls() != 0) {
            m_cancelled = true;
            m_keep_open = false;
            return;
        }
        for (index = 0; index < 2; ++index) {
            if (m_text_buffers[index] != 0) {
                m_text_buffers[index]->SetGeometryDirty();
            }
        }
        for (index = 0; index < 3; ++index) {
            if (m_buttons[index] != 0) {
                m_buttons[index]->m_dirty = true;
            }
        }
        DrawButtonAppearance(target, m_background, m_bounds, {});
        InvalidateRegion(m_bounds.x, m_bounds.y, m_bounds.x + m_bounds.w, m_bounds.y + m_bounds.h);
        m_dirty = false;
    }
    for (index = 0; index < 3; ++index) {
        if (m_buttons[index] != 0) {
            m_buttons[index]->Draw(target);
        }
    }
    for (index = 0; index < 2; ++index) {
        if (m_text_buffers[index] != 0) {
            m_text_buffers[index]->RenderToTarget(0, false, target);
        }
    }
}

// FUNCTION: WIZ8 0x005DAE90
bool W8NpcDialog::ProcessInput()
{
    m_input.setModal(true);
    if (m_keep_open)
        PumpUiInput({
            .before =
                [this](const InputEvent& input) {
                    if (IsTypingEvent(input) && m_input_group &&
                        m_input_group->HandleInput(input)) {
                        return InputRoute::Handled;
                    }
                    if (input.type == InputEventType::KeyDown &&
                        (input.key == SDLK_RETURN || input.key == SDLK_ESCAPE) &&
                        m_message->kind == W8_NPC_ENTRY_KEYWORD_INPUT) {
                        m_cancelled = input.key == SDLK_ESCAPE;
                        if (!m_cancelled) m_input_text = m_input_group->Text(m_input_field);
                        m_keep_open = false;
                        return InputRoute::Stop;
                    }
                    return InputRoute::Pass;
                },
            // A chosen option closes the reply; later events belong to its successor.
            .consumed =
                [this](const InputEvent&) {
                    return m_keep_open ? InputRoute::Handled : InputRoute::Stop;
                },
        });
    if (!m_keep_open) {
        // Restore the previous editor before delivering the reply. Destruction
        // during screen teardown must not submit an unfinished reply.
        m_input_group.reset();
        m_input.setModal(false);
        if (auto closed = std::exchange(m_closed, {})) closed(*this);
    }
    return m_keep_open;
}

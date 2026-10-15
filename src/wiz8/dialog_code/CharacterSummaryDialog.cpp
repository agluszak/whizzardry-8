#include "wiz8/dialog_code/CharacterSummaryDialog.h"

#include <cstring>
#include <memory>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_surface.h>

#include "character_event_queue.h"
#include "compat/native.h"
#include "dialog_code/DialogBase.h"
#include "input.h"
#include "layouts/character.h"
#include "layouts/combat_state.h"
#include "layouts/game_status.h"
#include "local_code/ControlsRect.h"
#include "local_code/MonsterManager.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/fonts.h"
#include "wiz8/integer_constants.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

/* The bodies from 0x005E0320 through 0x005E0C30 form one dialog lifecycle
   cluster immediately after StatInfoDialogs.cpp.  Retail does not expose its
   original translation-unit or class spelling. */

// GLOBAL: WIZ8 0x00650250
static W8ControlsRect g_character_summary_quote_bounds[1] = {
    {121, 24, 345, 72},
};

// FUNCTION: WIZ8 0x005e0320
W8CharacterSummaryDialog::W8CharacterSummaryDialog(W8Character* character)
    : m_voice_started(0), m_quote_text(nullptr), m_field_070(0), m_character(character),
      m_use_original_character(0), m_portrait_clock_started(0)
{
    SetExtent(0x171, 0x60);
    SetBackground("Data\\Dialogs\\popup_quote.sti", 0);
    SetOrigin(0x87, 0xc0);
    CreateQuoteText();

    m_field_070 = 0;
    m_use_original_character = 0;
    m_saved_character = *g_status.buffers.Char;
    m_saved_monster_entry = gXStatus.monster_manager_entries[0];
    memcpy(&m_saved_party_row, g_status.buffers.XChar, sizeof(m_saved_party_row));
    *g_status.buffers.Char = *m_character;
    ResetPartySlotRow(0);
    ResetGameplaySlot(0);
    g_status.buffers.Char[0].fInParty = true;
    g_status.buffers.XChar[0].npc_index = -1;
}

// FUNCTION: WIZ8 0x005e0410
W8CharacterSummaryDialog::~W8CharacterSummaryDialog()
{
    gXStatus.character_event_queue->CompleteAllActiveEvents();
    if (!m_use_original_character) {
        *g_status.buffers.Char = m_saved_character;
        gXStatus.monster_manager_entries[0] = m_saved_monster_entry;
        memcpy(g_status.buffers.XChar, &m_saved_party_row, sizeof(m_saved_party_row));
    }
}

// FUNCTION: WIZ8 0x005e0600
bool W8CharacterSummaryDialog::CreateQuoteText()
{
    const auto& offset = g_character_summary_quote_bounds[0];
    W8ControlsRect bounds{offset.left + m_x, offset.top + m_y, offset.right + m_x,
                          offset.bottom + m_y};
    W8CharacterEvent event(m_character, g_effect0, 0, g_character_event_no_flags,
                           g_character_event_full_volume);
    m_quote_text =
        std::make_unique<W8TextBuffer>(&bounds, event.GetQuoteText(), g_wiz_text_font_secondary,
                                       g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    return true;
}

// FUNCTION: WIZ8 0x005e07b0
void W8CharacterSummaryDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    m_quote_text->Paint(target);
    RenderPartyPortrait(target, m_character->portrait_index, m_x + 11, m_y + 11, {}, 1, 0);
}

// FUNCTION: WIZ8 0x005e0830
void W8CharacterSummaryDialog::DrawPortraitAnimationFrame(SDL_Surface& target)
{
    BlitPartyPortraitAnimation(target, m_character->portrait_index, m_x + 11, m_y + 11, {}, 0,
                               false);
}

// FUNCTION: WIZ8 0x005e0860
// FUNCTION: WIZ8 0x005e0880
bool W8CharacterSummaryDialog::HandleInputEvent(const InputEvent* input)
{
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        switch (input->key) {
        case SDLK_BACKSPACE:
        case SDLK_RETURN:
        case SDLK_ESCAPE:
        case SDLK_SPACE:
            m_keep_open = false;
            break;
        }
    }
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005e0920
bool W8CharacterSummaryDialog::ProcessInput()
{
    if (!m_portrait_clock_started) {
        m_portrait_clock_started = true;
        m_portrait_clock = w8_get_ticks();
    }
    BeginModalInput();
    if (!m_voice_started && m_portrait_clock + 750 < w8_get_ticks()) {
        m_voice_started = true;
        W8Character* character = m_use_original_character ? m_character : g_status.buffers.Char;
        QueueCharacterEvent(character, g_effect0, 0, g_character_event_no_flags,
                            g_character_event_full_volume);
    }
    bool keep_open = m_keep_open;
    PumpUiInput({
        .before =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::LeftButtonDown ||
                    input.type == InputEventType::LeftButtonRepeat ||
                    input.type == InputEventType::RightButtonDown) {
                    m_keep_open = false;
                }
                return InputRoute::Pass;
            },
        .consumed =
            [this, &keep_open](const InputEvent&) {
                keep_open = m_keep_open;
                return InputRoute::Handled;
            },
        .unhandled =
            [this, &keep_open](const InputEvent& input) {
                keep_open = HandleInputEvent(&input);
                if (!keep_open) EndModalInput();
                return InputRoute::Stop;
            },
    });
    if (!keep_open) EndModalInput();
    return keep_open;
}

// FUNCTION: WIZ8 0x005e0c30

#include <cctype>
#include <cstring>

#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/integer_constants.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/xstatus.h"
#include "input.h"
#include "character_event_queue.h"
#include "compat/native.h"
#include "dialog_code/DialogBase.h"
#include "layouts/character.h"
#include "layouts/combat_state.h"
#include "layouts/game_status.h"
#include "local_code/ControlsRect.h"
#include "local_code/MonsterManager.h"
#include "wiz8/regions.h"

/* The bodies from 0x005E0320 through 0x005E0C30 form one dialog lifecycle
   cluster immediately after StatInfoDialogs.cpp.  Retail does not expose its
   original translation-unit or class spelling. */

// GLOBAL: WIZ8 0x00650250
static W8ControlsRect g_character_summary_quote_bounds[1] = {
    {121, 24, 345, 72},
};

// FUNCTION: WIZ8 0x005e0320
W8CharacterSummaryDialog::W8CharacterSummaryDialog(W8Character* character)
    : m_voice_started(0), m_quote_text(0), m_numeric_input(0), m_remaining(0),
      m_taken(0), m_total(0), m_field_070(0), m_character(character), m_use_original_character(0),
      m_portrait_clock_started(0)
{
    SetExtent(0x171, 0x60);
    SetBackground("Data\\Dialogs\\popup_quote.sti", 0);
}

// FUNCTION: WIZ8 0x005e0410
W8CharacterSummaryDialog::~W8CharacterSummaryDialog()
{
    DestroyControls();
}

// FUNCTION: WIZ8 0x005e04e0
int W8CharacterSummaryDialog::CreateControls()
{
    W8DialogBase::CreateControls();
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
    if (!CreateQuoteText()) {
        m_error = 7;
        return 7;
    }
    return 0;
}

// FUNCTION: WIZ8 0x005e0590
void W8CharacterSummaryDialog::DestroyControls()
{
    W8DialogBase::DestroyControls();
    delete m_quote_text;
    m_quote_text = 0;
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
    W8TextBuffer** buffers[] = {&m_quote_text};

    for (int index = 0; index < 1; ++index) {
        W8ControlsRect absolute = {g_character_summary_quote_bounds[index].left + m_x,
                                   g_character_summary_quote_bounds[index].top + m_y,
                                   g_character_summary_quote_bounds[index].right + m_x,
                                   g_character_summary_quote_bounds[index].bottom + m_y};
        W8Character* character = m_use_original_character ? m_character : g_status.buffers.Char;
        W8CharacterEvent event(character, g_effect0, 0, g_character_event_no_flags,
                               g_character_event_full_volume);
        *buffers[index] =
            new W8TextBuffer(&absolute, event.GetQuoteText(), g_wiz_text_font_secondary,
                             g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        if (*buffers[index] == 0) {
            delete m_quote_text;
            m_quote_text = 0;
            return false;
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x005e07b0
void W8CharacterSummaryDialog::Draw()
{
    if (m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) {
        if (!m_initialized) {
            CreateControls();
        }
        m_quote_text->SetGeometryDirty();
        W8DialogBase::Draw();
    }
    if (m_quote_text != 0) {
        m_quote_text->RenderToTarget(0, false, GetFrameBufferSurface());
    }
    RenderPartyPortrait(m_character->portrait_index, m_x + 11, m_y + 11, {}, 1, 0);
    if (!m_portrait_clock_started) {
        m_portrait_clock_started = true;
        m_portrait_clock = w8_get_ticks();
    }
}

// FUNCTION: WIZ8 0x005e0830
void W8CharacterSummaryDialog::DrawPortraitAnimationFrame()
{
    BlitPartyPortraitAnimation(m_character->portrait_index, m_x + 11, m_y + 11, {}, 0, false);
}

// FUNCTION: WIZ8 0x005e0860
void W8CharacterSummaryDialog::OnNumericInputChanged(int value)
{
    if (value == 0) {
        m_taken = m_numeric_input->m_value;
        m_remaining = m_total - m_taken;
    }
}

// FUNCTION: WIZ8 0x005e0880
bool W8CharacterSummaryDialog::HandleInputEvent(const InputEvent* input)
{
    W8DialogNumericInput* inputs[] = {m_numeric_input};
    for (int index = 0; index < 1; ++index) {
        if (inputs[index] != 0 && inputs[index]->m_active) {
            if (inputs[index]->HandleInput(input)) {
                return true;
            }
            break;
        }
    }
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        switch (toupper(input->key)) {
        case VK_BACK:
        case VK_RETURN:
        case VK_ESCAPE:
        case VK_SPACE:
            m_keep_open = false;
            break;
        }
    }
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005e0920
bool W8CharacterSummaryDialog::ProcessInput()
{
    BeginModalInput();
    if (!m_voice_started && m_portrait_clock + 750 < w8_get_ticks()) {
        m_voice_started = true;
        W8Character* character = m_use_original_character ? m_character : g_status.buffers.Char;
        QueueCharacterEvent(character, g_effect0, 0, g_character_event_no_flags,
                            g_character_event_full_volume);
    }
    bool keep_open = m_keep_open;
    DrainModalInput([this, &keep_open](const InputEvent& input) {
        if (input.type == InputEventType::LeftButtonDown || input.type == InputEventType::LeftButtonRepeat ||
            input.type == InputEventType::RightButtonDown) {
            m_keep_open = false;
        }
        if (!DispatchRegionInput(&input)) {
            keep_open = HandleInputEvent(&input);
            if (!keep_open) EndModalInput();
            return false;
        }
        keep_open = m_keep_open;
        return true;
    });
    if (!keep_open) EndModalInput();
    return keep_open;
}

// FUNCTION: WIZ8 0x005e0c30
W8DialogKind W8CharacterSummaryDialog::GetDialogType()
{
    return W8_DIALOG_CHARACTER_SUMMARY;
}

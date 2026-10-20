#include "wiz8/dialog_code/CharacterSummaryDialog.h"

#include <chrono>
#include <memory>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_surface.h>

#include "dialog_code/DialogBase.h"
#include "local_code/ControlsRect.h"
#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/npc_speech.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/portrait_animation.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/ui_input.h"

/* The bodies from 0x005E0320 through 0x005E0C30 form one dialog lifecycle
   cluster immediately after StatInfoDialogs.cpp.  Retail does not expose its
   original translation-unit or class spelling. */

static W8ControlsRect g_character_summary_quote_bounds[1] = {
    {121, 24, 345, 72},
};

W8CharacterSummaryDialog::W8CharacterSummaryDialog(W8Character* character)
    : m_voice_started(0), m_quote_text(nullptr), m_character(character), m_portrait_clock_started(0)
{
    SetExtent(0x171, 0x60);
    SetBackground("Data\\Dialogs\\popup_quote.sti", 0);
    SetOrigin(0x87, 0xc0);
    CreateQuoteText();

    m_portrait.pose = m_portrait.target_pose =
        m_character->highest_condition >= W8_CONDITION_ASLEEP ? 2 : 1;
    m_portrait.idle_clock = MonotonicMilliseconds() + PresentationRng().below(5000) + 5000;
}

W8CharacterSummaryDialog::~W8CharacterSummaryDialog()
{
    wiz8::sound::stop(m_voice);
}

void W8CharacterSummaryDialog::CreateQuoteText()
{
    const auto& offset = g_character_summary_quote_bounds[0];
    W8ControlsRect bounds{offset.left + m_x, offset.top + m_y, offset.right + m_x,
                          offset.bottom + m_y};
    m_quote_text = std::make_unique<W8TextBuffer>(
        &bounds, GetCharacterQuoteText(*m_character, 0), g_wiz_text_font_secondary,
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
}

void W8CharacterSummaryDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    m_quote_text->Paint(target);
    CatalogArtwork::Load(0x12, m_character->portrait_index).draw(target, 0, m_x + 11, m_y + 11);
    BlitPortraitAnimation(target, m_character->portrait_index, m_x + 11, m_y + 11, {}, m_portrait,
                          true,
                          {.missing = m_character->highest_condition == W8_CONDITION_MISSING,
                           .zero_hp = m_character->hp_current == 0});
}

void W8CharacterSummaryDialog::RefreshModal()
{
    W8DialogBase::RefreshModal();
    BlitPortraitAnimation(GetFrameBufferSurface(), m_character->portrait_index, m_x + 11, m_y + 11,
                          {}, m_portrait, false,
                          {.missing = m_character->highest_condition == W8_CONDITION_MISSING,
                           .zero_hp = m_character->hp_current == 0});
}

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

bool W8CharacterSummaryDialog::ProcessInput()
{
    if (!m_portrait_clock_started) {
        m_portrait_clock_started = true;
        m_portrait_clock = MonotonicMilliseconds();
    }
    BeginModalInput();
    if (!m_voice_started && m_portrait_clock + 750 < MonotonicMilliseconds()) {
        m_voice_started = true;
        const auto path = GetCharacterVoicePath(*m_character, 0);
        m_voice = wiz8::sound::play(path, {.volume = g_settings.voice_volume & 0xffu});
        m_mouth_gap = LoadMouthGapTrack(path);
        m_portrait.voice_remaining_ms =
            wiz8::sound::playback_time(m_voice)
                .value_or(wiz8::sound::PlaybackTime{
                    static_cast<unsigned int>(
                        ComputePortraitMessageDuration(GetCharacterQuoteText(*m_character, 0))),
                    0})
                .total_ms;
        m_portrait.frame = 7;
        m_portrait.frame_dirty = true;
        m_portrait.frame_due = std::chrono::milliseconds(MonotonicMilliseconds() + 120);
        m_portrait.target_pose = m_character->highest_condition >= W8_CONDITION_ASLEEP ? 2 : 1;
    }
    if (m_voice_started) {
        UpdateMouthGapTrack(m_voice, m_mouth_gap);
        AdvancePortraitSpeech(m_portrait, std::chrono::milliseconds(MonotonicMilliseconds()),
                              m_mouth_gap.mouth_open, false);
        AdvancePortraitPose(m_portrait);
        if (m_voice ? !wiz8::sound::is_playing(m_voice) : m_portrait.voice_remaining_ms == 0)
            m_keep_open = false;
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
                if (!keep_open) {
                    wiz8::sound::stop(m_voice);
                    EndModalInput();
                }
                return InputRoute::Stop;
            },
    });
    if (!keep_open) {
        wiz8::sound::stop(m_voice);
        EndModalInput();
    }
    return keep_open;
}

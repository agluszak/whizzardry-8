#pragma once

#include <cstdint>
#include <memory>

#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio_resources.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/platform/input.h"
#include "wiz8/portrait_animation.h"

class W8TextBuffer;
struct W8Character;

/* A character preview owns its voice and portrait animation. */
class W8CharacterSummaryDialog : public W8DialogBase {
public:
    explicit W8CharacterSummaryDialog(W8Character* character);
    virtual ~W8CharacterSummaryDialog() override;
    virtual void Draw(SDL_Surface& target) override;

    virtual bool ProcessInput() override;

    void RefreshModal() override;

private:
    void CreateQuoteText();
    bool HandleInputEvent(const InputEvent* input);

    bool m_voice_started;
    std::unique_ptr<W8TextBuffer> m_quote_text;
    W8Character* m_character;
    wiz8::sound::Playback m_voice;
    W8MouthGapTrack m_mouth_gap;
    W8PortraitAnimation m_portrait;
    bool m_portrait_clock_started;
    uint64_t m_portrait_clock;
};

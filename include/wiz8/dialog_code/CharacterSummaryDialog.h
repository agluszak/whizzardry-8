#pragma once

#include <cstdint>
#include <memory>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/MonsterManager.h"

class W8TextBuffer;

/* The character-summary popup temporarily installs the character as party
   slot zero so the ordinary portrait and quote machinery can render it. */
// VTABLE: WIZ8 0x005efdc0
class W8CharacterSummaryDialog : public W8DialogBase {
public:
    explicit W8CharacterSummaryDialog(W8Character* character);
    virtual ~W8CharacterSummaryDialog() override;
    virtual void Draw(SDL_Surface& target) override;

    virtual bool ProcessInput() override;

    void DrawPortraitAnimationFrame(SDL_Surface& target);

private:
    void CreateQuoteText();
    bool HandleInputEvent(const InputEvent* input);

    bool m_voice_started;
    std::unique_ptr<W8TextBuffer> m_quote_text;
    int m_field_070;
    W8Character* m_character;
    W8Character m_saved_character;
    W8MonsterManagerEntry m_saved_monster_entry;
    W8PartySlotRow m_saved_party_row;
    /* Select the original character instead of the temporary party-slot copy. */
    unsigned char m_use_original_character;
    bool m_portrait_clock_started;
    uint64_t m_portrait_clock;
};

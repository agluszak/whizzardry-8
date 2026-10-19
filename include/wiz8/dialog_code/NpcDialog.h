#pragma once

class TextFieldGroup;

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "compat/compiler.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/modal_content.h"
#include "wiz8/ui_layer.h"

class W8TextBuffer;
class W8ButtonWidget;
struct W8NpcQuoteEntry;

/* The dialog receives the script entry itself. Its sub-entry array is the
   option list for kind 5; operand0 is the price for kinds 0x12/0x1e. */

/* NPC replies: options, a price confirmation, or keyword entry. The dialog
   owns its editor; closing restores the previous editor before reporting the
   reply to the NPC controller. */
// VTABLE: WIZ8 0x005efa4c
class W8NpcDialog final : public ModalContent {
    friend void OnNpcDialogClosed(W8NpcDialog& dialog);

public:
    W8NpcDialog(W8NpcQuoteEntry* message, int aux_data, std::function<void(W8NpcDialog&)> closed);
    virtual ~W8NpcDialog() override;
    void DrawModal(SDL_Surface& target) override;
    void RefreshModal() override;
    void RequestModalRedraw() override { m_dirty = true; }
    virtual bool ProcessInput() override;

private:
    bool Configure();
    UiLayer m_input;
    SDL_Rect m_bounds{};
    ButtonAppearance m_background;
    bool m_dirty = true;
    bool m_keep_open = true;
    std::function<void(W8NpcDialog&)> m_closed;
    std::array<std::unique_ptr<W8TextBuffer>, 2> m_text_buffers;
    std::array<std::unique_ptr<W8ButtonWidget>, 3> m_buttons;
    bool m_cancelled = false;
    W8NpcQuoteEntry* m_message;
    bool m_compact_options = false; /* option list may size under 200 wide */
    unsigned short m_text_width; /* widest option text + 6 */
    std::optional<int> m_input_field;
    std::unique_ptr<TextFieldGroup> m_input_group;
    std::string m_input_text; /* fills the 0x270-byte allocation */
};

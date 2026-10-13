#include "Font.h"
#pragma once

#include <SDL3/SDL_rect.h>
#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/ui_input.h"

/* Numeric split dialogs release the active field before forwarding button-up,
   and forward repeat events unchanged. The field reference remains live across
   callbacks; keyboard handling stays with the concrete dialog. */
template <class Dialog, class Field>
void ProcessNumericDialogInputEvents(Dialog* dialog, Field*& active_field,
                                     bool (Dialog::*handle_event)(const InputEvent*))
{
    dialog->BeginModalInput();
    PumpUiInput({
        .before =
            [&](const InputEvent& input) {
                if (input.type == InputEventType::LeftButtonUp && active_field != 0) {
                    active_field->SetActive(false);
                }
                return InputRoute::Pass;
            },
        .unhandled =
            [&](const InputEvent& input) {
                if (input.type != InputEventType::LeftButtonRepeat &&
                    input.type != InputEventType::RightButtonRepeat) {
                    (dialog->*handle_event)(&input);
                }
                return InputRoute::Handled;
            },
    });
}

class W8MessageDialogBase;
struct W8Character;

/* ConfigureDialogFont writes these four; every dialog draw/text path reads
   them. Declared here because this unit owns their definitions. */
extern Font* g_dialog_interface_font;
extern bool g_dialog_font_enabled;
extern unsigned char g_dialog_font_foreground;
extern unsigned char g_dialog_font_background;
void ConfigureDialogFont(Font& font, bool enabled, unsigned char foreground,
                         unsigned char background);
W8DialogBase* CreateCharacterSummaryDialog(W8Character* character);
W8MessageDialogBase* CreateMessageDialog();
bool GetDialogResult(W8DialogBase* dialog);
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text);

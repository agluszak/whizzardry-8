#include "Font.h"
#pragma once

#include <string_view>

#include <SDL3/SDL_rect.h>

#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/ui_input.h"

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
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text);

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

W8DialogBase* CreateCharacterSummaryDialog(W8Character* character);
W8MessageDialogBase* CreateMessageDialog();
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text);

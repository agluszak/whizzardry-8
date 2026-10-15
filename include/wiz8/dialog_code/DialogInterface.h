#pragma once

#include <string_view>

class W8DialogBase;
class W8MessageDialogBase;
struct W8Character;

W8DialogBase* CreateCharacterSummaryDialog(W8Character* character);
W8MessageDialogBase* CreateMessageDialog();
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text);

#pragma once

#include <memory>
#include <string_view>

class W8DialogBase;
class W8MessageDialogBase;
struct W8Character;

std::unique_ptr<W8DialogBase> CreateCharacterSummaryDialog(W8Character* character);
std::unique_ptr<W8MessageDialogBase> CreateMessageDialog();
void SetDialogPrompt(W8MessageDialogBase& dialog, std::string_view text);

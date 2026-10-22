#pragma once

#include <memory>

class W8DialogBase;
class W8MessageDialogBase;
struct W8Character;

std::unique_ptr<W8DialogBase> CreateCharacterSummaryDialog(W8Character* character);
std::unique_ptr<W8MessageDialogBase> CreateMessageDialog();

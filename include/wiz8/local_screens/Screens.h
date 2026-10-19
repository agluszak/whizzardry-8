#pragma once

#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/layouts/screen_state.h"

void RefreshPartySlotDisplay(unsigned int party_slot);
void InitializeMainGameLevelBlock(void);
void SetTargetCursor(int cursor);
void ApplyCurrentCursor(void);
void RequestPartySlotRedraw(int bit);
unsigned char GetPartyOrderTextColor(signed char party_order);
W8ScreenId GetPendingScreenState(void);
void RequestScreenTransition(void);

bool IsScreenTransitionPending(void);
void RequestExitScreen(void);
bool ExitScreenEnter(void);
void ExitScreenFrame(void);
/* Dispatch one already-built line to the active screen. */
void ShowNoticeLine(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel);

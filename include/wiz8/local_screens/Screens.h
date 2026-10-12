#include <cstdint>
#include <string_view>
#pragma once
#include "wiz8/layouts/screen_state.h"
#include "wiz8/dialog_code/DialogBase.h"

void RefreshPartySlotDisplay(std::uint32_t party_slot);
void InitializeMainGameLevelBlock(void);
void SetTargetCursor(std::int32_t cursor);
void ApplyCurrentCursor(void);
void RequestPartySlotRedraw(std::int32_t bit);
std::uint8_t GetPartyOrderTextColor(std::int8_t party_order);
std::uint8_t ScreenLifecycleSuccess(void); // bool-byte-ok: screen-table unsigned char (*)() slot
void NoOp(void);
W8ScreenId GetPendingScreenState(void);
void SetPendingScreenState(W8ScreenId value);
void RequestScreenTransition(void);
// bool-byte-ok: screen-table unsigned char (*)() slot
bool IsScreenTransitionPending(void);
void RequestExitScreen(void);
std::uint8_t ExitScreenEnter(void);
void ExitScreenFrame(void);
/* Dispatch one already-built line to the active screen. */
void ShowNoticeLine(std::string_view text, W8DialogDestroyCallback callback, bool confirmation,
                    bool cancel);

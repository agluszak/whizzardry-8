#pragma once

#include <memory>

#include "wiz8/screen.h"

enum W8PartySelectionMode {
    W8_PARTY_SELECT_CHARACTERS = 0,
    W8_PARTY_SELECT_IMPORT = 1,
    W8_PARTY_SELECT_OPTIONS = 2,
    W8_PARTY_SELECT_CREATION_NOTICE = 3,
    W8_PARTY_SELECT_SAVE_NAME = 4
};

enum W8PartyCreationPage {
    W8_PARTY_CREATION_OPTIONS = 0,
    W8_PARTY_CREATION_NOTICE = 1,
    W8_PARTY_CREATION_SAVE_NAME = 2
};

enum W8PartyConfirmationAction {
    W8_PARTY_CONFIRM_NONE = 0,
    W8_PARTY_CONFIRM_DELETE_CHARACTER = 1,
    W8_PARTY_CONFIRM_START_WITH_SAVE_NAME = 2,
    W8_PARTY_CONFIRM_PROCEED_TO_OPTIONS = 3,
    W8_PARTY_CONFIRM_IMPORT = 4,
    W8_PARTY_CONFIRM_LEAVE = 5
};

/* One byte per portrait, nonzero for the portraits that carry
   animation frames. */
extern unsigned char g_portrait_frame_flags[0x50];

/* Refresh one party-selection list portrait after a slot change. */
void RefreshPartySelectionPortrait(unsigned int party_slot);
/* Nonzero while the selector is reviewing an existing character. */
bool PartySelectionInReviewMode(void);
/* Assembles the starting party from saved characters or an imported party. */
std::unique_ptr<Screen> CreatePartySelectionScreen();
/* Starts a new game: leaves for the race intro film chosen by the party. */
std::unique_ptr<Screen> CreateGameStartRouterScreen();

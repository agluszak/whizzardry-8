#pragma once

#include <optional>
#include <string>

#include "wiz8/layouts/save_screenshot.h"

enum W8ScreenId {
    W8_SCREEN_NONE = -1,
    W8_SCREEN_INTRO = 0,
    W8_SCREEN_MAIN_MENU = 1,
    W8_SCREEN_GAME_START_ROUTER = 2,
    W8_SCREEN_CHARACTER = 3,
    W8_SCREEN_PLEASE_WAIT = 4,
    W8_SCREEN_PARTY_SELECTION = 5,
    W8_SCREEN_CAMP = 6,
    W8_SCREEN_MAIN_GAME = 7,
    W8_SCREEN_AUTOMAP = 8,
    W8_SCREEN_CREDITS = 9,
    W8_SCREEN_OPTIONS = 10,
    W8_SCREEN_JOURNAL = 11,
    W8_SCREEN_EXIT = 12,
    W8_SCREEN_COUNT = 13
};

/* The shared screen mode field carries different domains for each destination.
   These four values belong specifically to the Please Wait screen. */
enum W8PleaseWaitMode {
    W8_PLEASE_WAIT_NEW_GAME = 0,
    W8_PLEASE_WAIT_LOAD_GAME = 1,
    W8_PLEASE_WAIT_SAVE_GAME = 2,
    W8_PLEASE_WAIT_CHANGE_LEVEL = 3
};

/* The destination-specific values carried by W8ScreenStateRuntime::mode. */
enum W8OptionsMode {
    W8_OPTIONS_DEFAULT = 0,
    W8_OPTIONS_LOAD = 1,
    W8_OPTIONS_SAVE = 2,
    W8_OPTIONS_IN_GAME = 3
};

enum W8CharacterScreenMode {
    W8_CHARACTER_CREATE = 0,
    W8_CHARACTER_EDIT = 1,
    W8_CHARACTER_LEVEL_UP = 2,
    W8_CHARACTER_LEVEL_UP_NOTICE = 3
};

/* A screen's lifecycle hooks. Initialize, enter and leave report failure,
   which stops the program; absent optional hooks have nothing to do. */
struct W8ScreenStateHandlers {
    bool (*initialize)() = nullptr;
    bool (*enter)() = nullptr;
    void (*frame)() = nullptr;
    bool (*leave)(int leaving) = nullptr; /* 0 = suspend, 1 = discard */
    void (*finalize)() = nullptr;
};

/* The current and pending screen records begin at the two globals whose first
   dwords the reviewed setters address directly. One storage object preserves
   that identity instead of mirroring the id into a synthetic runtime record. */
/* The five leading dwords and the name are what lifecycle record 4's entry
   handler reads out of this record to fill its own descriptor; the four bytes
   before the name and everything past it stay positional, and the name's bound
   is the record's end rather than a proved one. */
struct W8ScreenStateRuntime {
    W8ScreenId id;     /* 0x00 */
    int mode;          /* 0x04 */
    int parameter;     /* 0x08 */
    int parameter_2;   /* 0x0c */
    /* The character the camp and review-character transitions hand to the
       character screen. */
    struct W8Character* character = nullptr;
    /* The screenshot the Please Wait screen's save mode hands to SaveGame. */
    std::optional<W8SaveScreenshot> save_screenshot;
    /* 0x14: identifying PC for camp entry (gpIdentifyingPC); null when
       camp opens without an identify/use-item handoff. Not the entry mode —
       that lives on W8CampScreenState::item_action. */
    struct W8Character* parameter_4;
    std::string name;
    /* Previous user save slot, removed only after a successful renamed save. */
    std::string previous_save_name;
};

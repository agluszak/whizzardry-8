#pragma once

#include <optional>
#include <string>
#include <variant>

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

using W8ScreenMode = std::variant<std::monostate, W8PleaseWaitMode, W8OptionsMode,
                                  W8CharacterScreenMode>;

/* A screen's lifecycle hooks. Initialize, enter and leave report failure,
   which stops the program; absent optional hooks have nothing to do. */
struct W8ScreenStateHandlers {
    bool (*initialize)() = nullptr;
    bool (*enter)() = nullptr;
    void (*frame)() = nullptr;
    bool (*leave)(int leaving) = nullptr; /* 0 = suspend, 1 = discard */
    void (*finalize)() = nullptr;
};

/* Runtime transition state. The mode variant carries only the domain selected
   by id, so consumers cannot interpret one screen's mode as another's. */
struct W8ScreenStateRuntime {
    W8ScreenId id;
    W8ScreenMode mode;
    int parameter;
    int parameter_2;
    /* The character the camp and review-character transitions hand to the
       character screen. */
    struct W8Character* character = nullptr;
    /* The screenshot the Please Wait screen's save mode hands to SaveGame. */
    std::optional<W8SaveScreenshot> save_screenshot;
    /* Identifying PC for camp entry (gpIdentifyingPC); null when camp opens
       without an identify/use-item handoff. Not the entry mode — that lives on
       W8CampScreenState::item_action. */
    struct W8Character* parameter_4;
    std::string name;
    /* Previous user save slot, removed only after a successful renamed save. */
    std::string previous_save_name;
};

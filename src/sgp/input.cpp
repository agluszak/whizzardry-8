#include <SDL3/SDL_mouse.h>
#include <cstdint>
#include <utf8proc.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>
#include <string_view>
#include <algorithm>
#include <queue>
#include <iterator>
#include <cstddef>

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "native/input_events.h"
#include "input.h"
#include "english.h"
#include "Video2.h"
#include "compat/native.h"

// Make sure to refer to the translation table which is within one of the following files (depending
// on the language used). ENGLISH.C, JAPANESE.C, FRENCH.C, GERMAN.C, SPANISH.C, etc...

// The gfKeyState table is used to track which of the keys is up or down at any one time. This is used while polling
// the interface.

// GLOBAL: WIZ8 0x006f0520
bool gfKeyState[256]; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x00650db8
bool fCursorWasClipped = false;
SDL_Rect gCursorClipRect;

// The gsKeyTranslationTables basically translates scan codes to our own key value table. Please note that the table is 2 bytes
// wide per entry. This will be used since we will use 2 byte characters for translation purposes.

// GLOBAL: WIZ8 0x006f04ea
std::uint16_t gfShiftState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f051c
std::uint16_t gfAltState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f0508
std::uint16_t gfCtrlState; // TRUE = Pressed, FALSE = Not Pressed

// These data structure are used to track the mouse while polling

// GLOBAL: WIZ8 0x006f0518
uint64_t guiLeftButtonRepeatTimer;
// GLOBAL: WIZ8 0x006f04f0
uint64_t guiRightButtonRepeatTimer;

// GLOBAL: WIZ8 0x006f04ec
bool gfTrackMousePos; // TRUE = queue mouse movement events, FALSE = don't
// GLOBAL: WIZ8 0x006f04ed
bool gfLeftButtonState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f04e8
bool gfRightButtonState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f050a
std::uint16_t gusMouseXPos; // X position of the mouse on screen
// GLOBAL: WIZ8 0x006f04f8
std::uint16_t gusMouseYPos; // y position of the mouse on screen

// GLOBAL: WIZ8 0x006ef4e0
static std::queue<InputAtom> gEventQueue;
static constexpr std::size_t max_input_events = 256;

// ATE: Added to signal if we have had input this frame - cleared by the SGP main loop
// GLOBAL: WIZ8 0x00650db9
bool gfSGPInputReceived = false;

void HandleButtonRepeats(void);

// These are the hook functions for both keyboard and mouse

// FUNCTION: WIZ8 0x00401ea0
bool InitializeInputManager(void)
{
    std::fill(std::begin(gfKeyState), std::end(gfKeyState), false);
    // Initialize the Event Queue
    gEventQueue = {};
    // By default, we will not queue mousemove events
    gfTrackMousePos = false;
    // Initialize other variables
    gfShiftState = false;
    gfAltState = false;
    gfCtrlState = false;
    // Initialize variables pertaining to the button states
    gfLeftButtonState = false;
    gfRightButtonState = false;
    // Initialize variables pertaining to the repeat mechanism
    guiLeftButtonRepeatTimer = 0;
    guiRightButtonRepeatTimer = 0;
    // Set the mouse to the center of the screen
    gusMouseXPos = 320;
    gusMouseYPos = 240;
    // Activate the hook functions for both keyboard and Mouse
    return true;
}

// FUNCTION: WIZ8 0x00401f90
void QueueEvent(std::uint16_t ubInputEvent, std::uint32_t usParam, std::uint32_t uiParam, std::uint8_t clicks)
{
    if (gEventQueue.size() == max_input_events)
        return;
    const uint64_t uiTimer = w8_get_ticks();
    const std::uint16_t usKeyState = gfShiftState | gfCtrlState | gfAltState;
    switch (ubInputEvent) {
    case LEFT_BUTTON_DOWN:
        guiLeftButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIMEOUT;
        break;
    case RIGHT_BUTTON_DOWN:
        guiRightButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIMEOUT;
        break;
    case LEFT_BUTTON_UP:
        guiLeftButtonRepeatTimer = 0;
        break;
    case RIGHT_BUTTON_UP:
        guiRightButtonRepeatTimer = 0;
        break;
    }
    const SDL_MouseButtonFlags buttons = (gfLeftButtonState ? SDL_BUTTON_LMASK : 0) |
                                         (gfRightButtonState ? SDL_BUTTON_RMASK : 0);
    gEventQueue.push({uiTimer, usKeyState, ubInputEvent, usParam, uiParam, buttons, clicks});
}

// FUNCTION: WIZ8 0x00402140
bool DequeueEvent(InputAtom* Event)
{
    HandleButtonRepeats();
    if (gEventQueue.empty())
        return false;
    *Event = gEventQueue.front();
    gEventQueue.pop();
    return true;
}

// GLOBAL: WIZ8 0x005ff51c
unsigned short g_key_remap_5ff51c[14] = {0x0069, 0x0063, 0x0061, 0x0067, 0x0064, 0x0068, 0x0066,
                                         0x0062, 0x0000, 0x0000, 0x0000, 0x0000, 0x0060, 0x006e};

// FUNCTION: WIZ8 0x00402270
void KeyChange(std::uint32_t key, std::uint32_t flags, std::uint8_t pressed)
{
    SDL_Point point;
    unsigned int packed;
    unsigned int code;

    if (key == 0x0c) {
        key = 0x65;
    } else if (key < 0x2f && key > 0x20 && (flags & 0x1000000) == 0) {
        key = g_key_remap_5ff51c[key - 0x21];
    } else if (key == 0x0d && (flags & 0x1000000) != 0) {
        key = 0x6c;
    }
    GetGameMousePosition(&point);
    packed = ((unsigned int)point.y << 0x10) | ((unsigned int)point.x & 0xffff);
    code = key & 0xffff;
    if (code >= std::size(gfKeyState))
        return;
    if (pressed == 1) {
        QueueEvent(gfKeyState[code] ? KEY_REPEAT : KEY_DOWN, code, packed);
        gfKeyState[code] = true;
        return;
    }
    if (gfKeyState[code] == 1) {
        gfKeyState[code] = 0;
        QueueEvent(2, code, packed);
        return;
    }
    if ((short)key == 9 && gfAltState != 0) {
        SDL_MinimizeWindow(reinterpret_cast<SDL_Window*>(ghWindow));
        gfKeyState[0x12] = 0;
        gfAltState = 0;
    }
}

void KeyDown(std::uint32_t usParam, std::uint32_t uiParam)
{                        // Are we PRESSING down one of SHIFT, ALT or CTRL ???
    if (usParam == 16) { // SHIFT key is PRESSED
        gfShiftState = SHIFT_DOWN;
        gfKeyState[16] = true;
    } else {
        if (usParam == 17) { // CTRL key is PRESSED
            gfCtrlState = CTRL_DOWN;
            gfKeyState[17] = true;
        } else {
            if (usParam == 18) { // ALT key is pressed
                gfAltState = ALT_DOWN;
                gfKeyState[18] = true;
            } else {
                if (usParam == SNAPSHOT) {
                    //PrintScreen();
                    // DB Done in the KeyUp function
                    // this used to be keyed to SCRL_LOCK
                    // which I believe Luis gave the wrong value
                } else {
                    // No special keys have been pressed
                    // Call KeyChange() and pass TRUE to indicate key has been PRESSED and not RELEASED
                    KeyChange(usParam, uiParam, true);
                }
            }
        }
    }
}

void KeyUp(std::uint32_t usParam, std::uint32_t uiParam)
{                        // Are we RELEASING one of SHIFT, ALT or CTRL ???
    if (usParam == 16) { // SHIFT key is RELEASED
        gfShiftState = false;
        gfKeyState[16] = false;
    } else {
        if (usParam == 17) { // CTRL key is RELEASED
            gfCtrlState = false;
            gfKeyState[17] = false;
        } else {
            if (usParam == 18) { // ALT key is RELEASED
                gfAltState = false;
                gfKeyState[18] = false;
            } else {
                if (usParam == SNAPSHOT) {
                    // DB this used to be keyed to SCRL_LOCK
                    // which I believe Luis gave the wrong value
                    //#ifndef JA2
                    if (_KeyDown(CTRL))
                        VideoCaptureToggle();
                    else
                        //#endif
                        PrintScreen();
                } else {
                    // No special keys have been pressed
                    // Call KeyChange() and pass FALSE to indicate key has been PRESSED and not RELEASED
                    KeyChange(usParam, uiParam, false);
                }
            }
        }
    }
}

//
// Miscellaneous input-related utility functions:
//

// FUNCTION: WIZ8 0x00402750
void FreeMouseCursor(void)
{
    SetGameCursorRect(nullptr);
    fCursorWasClipped = false;
}

void HandleButtonRepeats(void)
{
    uint64_t uiTimer;

    uiTimer = w8_get_ticks();

    // Is there a LEFT mouse button repeat
    if (gfLeftButtonState) {
        if ((guiLeftButtonRepeatTimer > 0) && (guiLeftButtonRepeatTimer <= uiTimer)) {
            std::uint32_t uiTmpLParam;
            SDL_Point MousePos;

            GetGameMousePosition(&MousePos);
            uiTmpLParam = ((MousePos.y << 16) & 0xffff0000) | (MousePos.x & 0x0000ffff);
            QueueEvent(LEFT_BUTTON_REPEAT, 0, uiTmpLParam);
            guiLeftButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIME;
        }
    } else {
        guiLeftButtonRepeatTimer = 0;
    }

    // Is there a RIGHT mouse button repeat
    if (gfRightButtonState) {
        if ((guiRightButtonRepeatTimer > 0) && (guiRightButtonRepeatTimer <= uiTimer)) {
            std::uint32_t uiTmpLParam;
            SDL_Point MousePos;

            GetGameMousePosition(&MousePos);
            uiTmpLParam = ((MousePos.y << 16) & 0xffff0000) | (MousePos.x & 0x0000ffff);
            QueueEvent(RIGHT_BUTTON_REPEAT, 0, uiTmpLParam);
            guiRightButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIME;
        }
    } else {
        guiRightButtonRepeatTimer = 0;
    }
}

// FUNCTION: WIZ8 0x00402760
std::int16_t GetMouseWheelDeltaValue(std::uint32_t wParam)
{
    std::int16_t sDelta = HIWORD(wParam);

    return (sDelta / 120);
}

// FUNCTION: WIZ8 0x00402780
unsigned short TranslateKeyToCharacter(unsigned short key, unsigned char modifiers)
{
    if ((modifiers & (CTRL_DOWN | ALT_DOWN)) != 0)
        return 0;
    if ((modifiers & SHIFT_DOWN) != 0)
        return gsKeyTranslationTable[key + 256];
    return gsKeyTranslationTable[key];
}

// FUNCTION: WIZ8 0x004027C0
unsigned short TranslateCharacterToKey(unsigned short character)
{
    std::uint16_t key;
    for (key = 0; key < 0x200; ++key) {
        if (gsKeyTranslationTable[key] == character) {
            return key % 256;
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x00402800
bool IsUppercaseCharacter(std::uint32_t character)
{
    return utf8proc_category(character) == UTF8PROC_CATEGORY_LU;
}

// FUNCTION: WIZ8 0x00402820
bool IsLowercaseCharacter(std::uint32_t character)
{
    const auto category = utf8proc_category(character); return category == UTF8PROC_CATEGORY_LL || category == UTF8PROC_CATEGORY_LO || category == UTF8PROC_CATEGORY_LT || category == UTF8PROC_CATEGORY_LM || category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_MC || category == UTF8PROC_CATEGORY_ME;
}

// FUNCTION: WIZ8 0x00402840
bool IsPunctuationCharacter(std::uint32_t character)
{
    const auto category = utf8proc_category(character); return (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_SO) || category == UTF8PROC_CATEGORY_CF;
}

// FUNCTION: WIZ8 0x00402880
int ToUppercaseCharacter(int character)
{
    return utf8proc_toupper(character);
}

// FUNCTION: WIZ8 0x004028A0
int ToLowercaseCharacter(int character)
{
    return utf8proc_tolower(character);
}

// FUNCTION: WIZ8 0x00402920
int CompareTextIgnoreAsciiCase(std::string_view first, std::string_view second)
{
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i) {
        const int difference = lower(first[i]) - lower(second[i]);
        if (difference) return difference;
    }
    return (first.size() > second.size()) - (first.size() < second.size());
}

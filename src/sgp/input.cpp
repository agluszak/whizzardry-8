/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "Types.h"
#include "native/input_events.h"
#include <algorithm>
#include <queue>
#include <iterator>
#include "input.h"
#include "english.h"
#include "Video2.h"

// Make sure to refer to the translation table which is within one of the following files (depending
// on the language used). ENGLISH.C, JAPANESE.C, FRENCH.C, GERMAN.C, SPANISH.C, etc...

#include "wiz8/application.h"

// The gfKeyState table is used to track which of the keys is up or down at any one time. This is used while polling
// the interface.

// GLOBAL: WIZ8 0x006f0520
BOOLEAN gfKeyState[256]; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x00650db8
BOOLEAN fCursorWasClipped = FALSE;
SGPRect gCursorClipRect;

// The gsKeyTranslationTables basically translates scan codes to our own key value table. Please note that the table is 2 bytes
// wide per entry. This will be used since we will use 2 byte characters for translation purposes.

// GLOBAL: WIZ8 0x006f04ea
UINT16 gfShiftState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f051c
UINT16 gfAltState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f0508
UINT16 gfCtrlState; // TRUE = Pressed, FALSE = Not Pressed

// These data structure are used to track the mouse while polling

// GLOBAL: WIZ8 0x006f04f4
BOOLEAN gfTrackDblClick;
// GLOBAL: WIZ8 0x006f04e4
UINT32 guiDoubleClkDelay; // Current delay in milliseconds for a delay
// GLOBAL: WIZ8 0x006f0504
UINT32 guiSingleClickTimer;
UINT32 guiRecordedWParam;
UINT32 guiRecordedLParam;
// GLOBAL: WIZ8 0x006f0514
UINT16 gusRecordedKeyState;
// GLOBAL: WIZ8 0x006f04e9
BOOLEAN gfRecordedLeftButtonUp;

// GLOBAL: WIZ8 0x006f0518
UINT32 guiLeftButtonRepeatTimer;
// GLOBAL: WIZ8 0x006f04f0
UINT32 guiRightButtonRepeatTimer;

// GLOBAL: WIZ8 0x006f04ec
BOOLEAN gfTrackMousePos; // TRUE = queue mouse movement events, FALSE = don't
// GLOBAL: WIZ8 0x006f04ed
BOOLEAN gfLeftButtonState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f04e8
BOOLEAN gfRightButtonState; // TRUE = Pressed, FALSE = Not Pressed
// GLOBAL: WIZ8 0x006f050a
UINT16 gusMouseXPos; // X position of the mouse on screen
// GLOBAL: WIZ8 0x006f04f8
UINT16 gusMouseYPos; // y position of the mouse on screen

// GLOBAL: WIZ8 0x006ef4e0
static std::queue<InputAtom> gEventQueue;
static constexpr std::size_t max_input_events = 256;

// ATE: Added to signal if we have had input this frame - cleared by the SGP main loop
// GLOBAL: WIZ8 0x00650db9
BOOLEAN gfSGPInputReceived = FALSE;

void HandleSingleClicksAndButtonRepeats(void);

// These are the hook functions for both keyboard and mouse

// FUNCTION: WIZ8 0x00401ea0
BOOLEAN InitializeInputManager(void)
{
    std::fill(std::begin(gfKeyState), std::end(gfKeyState), FALSE);
    // Initialize the Event Queue
    gEventQueue = {};
    // By default, we will not queue mousemove events
    gfTrackMousePos = FALSE;
    // Initialize other variables
    gfShiftState = FALSE;
    gfAltState = FALSE;
    gfCtrlState = FALSE;
    // Initialize variables pertaining to DOUBLE CLIK stuff
    gfTrackDblClick = TRUE;
    guiDoubleClkDelay = DBL_CLK_TIME;
    guiSingleClickTimer = 0;
    gfRecordedLeftButtonUp = FALSE;
    // Initialize variables pertaining to the button states
    gfLeftButtonState = FALSE;
    gfRightButtonState = FALSE;
    // Initialize variables pertaining to the repeat mechanism
    guiLeftButtonRepeatTimer = 0;
    guiRightButtonRepeatTimer = 0;
    // Set the mouse to the center of the screen
    gusMouseXPos = 320;
    gusMouseYPos = 240;
    // Activate the hook functions for both keyboard and Mouse
    return TRUE;
}

// FUNCTION: WIZ8 0x00401f90
void QueueEvent(UINT16 ubInputEvent, UINT32 usParam, UINT32 uiParam)
{
    if (gEventQueue.size() == max_input_events)
        return;
    const UINT32 uiTimer = w8_get_ticks();
    const UINT16 usKeyState = gfShiftState | gfCtrlState | gfAltState;
    switch (ubInputEvent) {
    case LEFT_BUTTON_DOWN:
        guiLeftButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIMEOUT;
        break;
    case RIGHT_BUTTON_DOWN:
        guiRightButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIMEOUT;
        break;
    case LEFT_BUTTON_UP:
        guiLeftButtonRepeatTimer = 0;
        if ((uiTimer - guiSingleClickTimer) < DBL_CLK_TIME) {
            guiSingleClickTimer = 0;
            gEventQueue.push({uiTimer, gusRecordedKeyState, LEFT_BUTTON_UP, usParam, uiParam});
            if (gEventQueue.size() < max_input_events)
                gEventQueue.push({uiTimer, gusRecordedKeyState, LEFT_BUTTON_DBL_CLK, usParam, uiParam});
            return;
        }
        guiSingleClickTimer = uiTimer;
        break;
    case RIGHT_BUTTON_UP:
        guiRightButtonRepeatTimer = 0;
        break;
    }
    gEventQueue.push({uiTimer, usKeyState, ubInputEvent, usParam, uiParam});
}

// FUNCTION: WIZ8 0x00402140
BOOLEAN DequeueEvent(InputAtom* Event)
{
    HandleSingleClicksAndButtonRepeats();
    if (gEventQueue.empty())
        return FALSE;
    *Event = gEventQueue.front();
    gEventQueue.pop();
    return TRUE;
}

// GLOBAL: WIZ8 0x005ff51c
unsigned short g_key_remap_5ff51c[14] = {0x0069, 0x0063, 0x0061, 0x0067, 0x0064, 0x0068, 0x0066,
                                         0x0062, 0x0000, 0x0000, 0x0000, 0x0000, 0x0060, 0x006e};

// FUNCTION: WIZ8 0x00402270
void KeyChange(UINT32 key, UINT32 flags, UINT8 pressed)
{
    SGPPoint point;
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
    packed = ((unsigned int)point.iY << 0x10) | ((unsigned int)point.iX & 0xffff);
    code = key & 0xffff;
    if (code >= std::size(gfKeyState))
        return;
    if (pressed == 1) {
        QueueEvent(gfKeyState[code] ? KEY_REPEAT : KEY_DOWN, code, packed);
        gfKeyState[code] = TRUE;
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

void KeyDown(UINT32 usParam, UINT32 uiParam)
{                        // Are we PRESSING down one of SHIFT, ALT or CTRL ???
    if (usParam == 16) { // SHIFT key is PRESSED
        gfShiftState = SHIFT_DOWN;
        gfKeyState[16] = TRUE;
    } else {
        if (usParam == 17) { // CTRL key is PRESSED
            gfCtrlState = CTRL_DOWN;
            gfKeyState[17] = TRUE;
        } else {
            if (usParam == 18) { // ALT key is pressed
                gfAltState = ALT_DOWN;
                gfKeyState[18] = TRUE;
            } else {
                if (usParam == SNAPSHOT) {
                    //PrintScreen();
                    // DB Done in the KeyUp function
                    // this used to be keyed to SCRL_LOCK
                    // which I believe Luis gave the wrong value
                } else {
                    // No special keys have been pressed
                    // Call KeyChange() and pass TRUE to indicate key has been PRESSED and not RELEASED
                    KeyChange(usParam, uiParam, TRUE);
                }
            }
        }
    }
}

void KeyUp(UINT32 usParam, UINT32 uiParam)
{                        // Are we RELEASING one of SHIFT, ALT or CTRL ???
    if (usParam == 16) { // SHIFT key is RELEASED
        gfShiftState = FALSE;
        gfKeyState[16] = FALSE;
    } else {
        if (usParam == 17) { // CTRL key is RELEASED
            gfCtrlState = FALSE;
            gfKeyState[17] = FALSE;
        } else {
            if (usParam == 18) { // ALT key is RELEASED
                gfAltState = FALSE;
                gfKeyState[18] = FALSE;
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
                    KeyChange(usParam, uiParam, FALSE);
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
    fCursorWasClipped = FALSE;
}

void HandleSingleClicksAndButtonRepeats(void)
{
    UINT32 uiTimer;

    uiTimer = w8_get_ticks();

    // Is there a LEFT mouse button repeat
    if (gfLeftButtonState) {
        if ((guiLeftButtonRepeatTimer > 0) && (guiLeftButtonRepeatTimer <= uiTimer)) {
            UINT32 uiTmpLParam;
            SGPPoint MousePos;

            GetGameMousePosition(&MousePos);
            uiTmpLParam = ((MousePos.iY << 16) & 0xffff0000) | (MousePos.iX & 0x0000ffff);
            QueueEvent(LEFT_BUTTON_REPEAT, 0, uiTmpLParam);
            guiLeftButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIME;
        }
    } else {
        guiLeftButtonRepeatTimer = 0;
    }

    // Is there a RIGHT mouse button repeat
    if (gfRightButtonState) {
        if ((guiRightButtonRepeatTimer > 0) && (guiRightButtonRepeatTimer <= uiTimer)) {
            UINT32 uiTmpLParam;
            SGPPoint MousePos;

            GetGameMousePosition(&MousePos);
            uiTmpLParam = ((MousePos.iY << 16) & 0xffff0000) | (MousePos.iX & 0x0000ffff);
            QueueEvent(RIGHT_BUTTON_REPEAT, 0, uiTmpLParam);
            guiRightButtonRepeatTimer = uiTimer + BUTTON_REPEAT_TIME;
        }
    } else {
        guiRightButtonRepeatTimer = 0;
    }
}

// FUNCTION: WIZ8 0x00402760
INT16 GetMouseWheelDeltaValue(UINT32 wParam)
{
    INT16 sDelta = HIWORD(wParam);

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
    UINT16 key;
    for (key = 0; key < 0x200; ++key) {
        if (gsKeyTranslationTable[key] == character) {
            return key % 256;
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x00402800
BOOLEAN IsUppercaseWideChar(unsigned short character)
{
    if (character >= L'A' && character <= L'Z')
        return TRUE;
    return FALSE;
}

// FUNCTION: WIZ8 0x00402820
BOOLEAN IsLowercaseWideChar(unsigned short character)
{
    if (character >= L'a' && character <= L'z')
        return TRUE;
    return FALSE;
}

// FUNCTION: WIZ8 0x00402840
BOOLEAN IsPunctuationWideChar(unsigned short character)
{
    if ((character >= L'!' && character <= L'/') || (character >= L':' && character <= L'@') ||
        (character >= L'[' && character <= L'_') || (character >= L'{' && character <= L'}'))
        return TRUE;
    return FALSE;
}

// FUNCTION: WIZ8 0x00402880
int ToUppercaseWideChar(int character)
{
    if ((unsigned short)character > L'`' && (unsigned short)character < L'{') {
        character -= L'a' - L'A';
    }
    return character;
}

// FUNCTION: WIZ8 0x004028A0
int ToLowercaseWideChar(int character)
{
    if ((unsigned short)character > L'@' && (unsigned short)character < L'[') {
        character += L'a' - L'A';
    }
    return character;
}

// FUNCTION: WIZ8 0x00402920
int CompareWideTextIgnoreAsciiCase(const wchar_t* first, const wchar_t* second)
{
    unsigned short left;
    unsigned short right;
    do {
        left = ToLowercaseWideChar(*first++);
        right = ToLowercaseWideChar(*second++);
    } while (left != 0 && left == right);
    return (UINT32)left - (UINT32)right;
}

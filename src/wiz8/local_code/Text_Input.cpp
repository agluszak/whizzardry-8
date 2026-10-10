#include "wiz8/unicode.h"
#include <algorithm>
#include "wiz8/wiz8_windows.h"
#include "wiz8/cursor.h"
#include "Font.h"
#include "input.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/wiz8_windows.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/text_input.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/local_screens/Screens.h"

#include "himage.h"
#include "Types.h"
#include "mousesystem.h"
#include "vobject_blitters.h"
#include "vsurface.h"

#include <stddef.h>
#include <memory>
#include <string.h>
#include "wiz8/local_screens/OptionsScreen.h"

/*
 * Wizardry's product fork of Sir-Tech's released Utils/Text_Input.c.
 *
 * Released ancestor: ja2-stracciatella/ja2-stracciatella commit
 * 5ac0a9d56d27e8a7e2c4a7b48ed8932ae7f64033,
 * ja2/Build/Utils/Text_Input.{c,h}. This derivative is distributed under the
 * SFI Source Code License Agreement retained in src/sgp.
 *
 * The released JA2 source establishes ancestry, not Wizardry's original source
 * path. Retail contains no Text_Input / Utils path string, so the recovered
 * .cpp / local_code/ placement remains a recovery choice.
 */

typedef void (*INPUT_CALLBACK)(unsigned char index, int active);

/* The allocation at 0x005D39B0 is 0x6c bytes. Sir-Tech's released
   Text_Input.c establishes the inherited members through InputCallback.
   Retail traversal and rendering then identify two Wizardry additions:
   +0x60 selects the alternate inactive-field background, and +0x61 suppresses
   both mouse callbacks. 0x62-0x63 only align the following list links. */
struct TEXTINPUTNODE {
    unsigned char ubID;
    unsigned char _padding01;
    short usInputType;
    unsigned char ubMaxChars; // capacity in UTF-8 bytes
    std::size_t max_code_units; // retail name/editor limit
    unsigned char _padding05[3];
    std::unique_ptr<char[]> szString;
    unsigned char ubStrLen;
    // bool-byte-ok: JA2 declares fEnabled as BOOLEAN (UINT8)
    unsigned char fEnabled;
    bool fUserField;
    unsigned char _padding0f;
    MOUSE_REGION region;
    INPUT_CALLBACK InputCallback;
    bool fUseInactiveTextFieldColor; /* Wizardry extension at +0x60 */
    bool fBlockMouseCallbacks;       /* Wizardry extension at +0x61 */
    unsigned char _padding62[2];
    std::unique_ptr<TEXTINPUTNODE> next;
    TEXTINPUTNODE* prev;

    ~TEXTINPUTNODE()
    {
        if (region.uiFlags & MSYS_REGION_EXISTS)
            MSYS_RemoveRegion(&region);
    }
};

/* 0x005D3520 allocates this record once per active input session, and
   0x005D35E0 fills the same fixed layout for its three presentation modes. */
struct TextInputColors {
    short usFont;
    unsigned short usTextFieldColor;
    unsigned char ubForeColor;
    unsigned char ubShadowColor;
    unsigned char ubHiForeColor;
    unsigned char ubHiShadowColor;
    unsigned char ubHiBackColor;
    bool fBevelling;
    unsigned short usBrighterColor;
    unsigned short usDarkerColor;
    unsigned short usCursorColor;
    bool fUseDisabledAutoShade;
    unsigned char ubDisabledForeColor;
    unsigned char ubDisabledShadowColor;
    unsigned char _padding13;
    unsigned short usDisabledTextFieldColor;
    unsigned short usInactiveTextFieldColor;
};

struct STACKTEXTINPUTNODE {
    std::unique_ptr<TEXTINPUTNODE> head;
    std::unique_ptr<TextInputColors> pColors;
    std::unique_ptr<STACKTEXTINPUTNODE> next;
};

W8_ABI_ASSERT(sizeof(TEXTINPUTNODE) == 0x6c, "text input field must match the retail allocation");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, ubID) == 0x00, "TEXTINPUTNODE.ubID");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, _padding01) == 0x01, "TEXTINPUTNODE._padding01");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, usInputType) == 0x02, "TEXTINPUTNODE.usInputType");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, ubMaxChars) == 0x04, "TEXTINPUTNODE.ubMaxChars");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, _padding05) == 0x05, "TEXTINPUTNODE._padding05");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, szString) == 0x08, "TEXTINPUTNODE.szString");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, ubStrLen) == 0x0c, "TEXTINPUTNODE.ubStrLen");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, fEnabled) == 0x0d, "TEXTINPUTNODE.fEnabled");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, fUserField) == 0x0e, "TEXTINPUTNODE.fUserField");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, _padding0f) == 0x0f, "TEXTINPUTNODE._padding0f");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, region) == 0x10, "TEXTINPUTNODE.region");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, InputCallback) == 0x5c, "TEXTINPUTNODE.InputCallback");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, fUseInactiveTextFieldColor) == 0x60,
              "TEXTINPUTNODE.fUseInactiveTextFieldColor");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, fBlockMouseCallbacks) == 0x61,
              "TEXTINPUTNODE.fBlockMouseCallbacks");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, _padding62) == 0x62, "TEXTINPUTNODE._padding62");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, next) == 0x64, "TEXTINPUTNODE.next");
W8_ABI_ASSERT(offsetof(TEXTINPUTNODE, prev) == 0x68, "TEXTINPUTNODE.prev");
W8_ABI_ASSERT(sizeof(TextInputColors) == 0x18, "text input style must match the retail allocation");
W8_ABI_ASSERT(sizeof(STACKTEXTINPUTNODE) == 0x0c,
              "text input session must match the retail allocation");

// GLOBAL: WIZ8 0x0069C808
static bool gfEditingText;

// GLOBAL: WIZ8 0x0069C7EC
static std::unique_ptr<TextInputColors> pColors;
// GLOBAL: WIZ8 0x0069C7F0
static std::unique_ptr<STACKTEXTINPUTNODE> pInputStack;
// GLOBAL: WIZ8 0x0069C7F4
static std::unique_ptr<TEXTINPUTNODE> gpTextInputHead;
// GLOBAL: WIZ8 0x0069C7F8
static TEXTINPUTNODE* gpTextInputTail;
// GLOBAL: WIZ8 0x0069C7FC
static TEXTINPUTNODE* gpActive;
// GLOBAL: WIZ8 0x0069C809
static bool gfTextInputMode;
// GLOBAL: WIZ8 0x0069C80A
static bool gfHiliteMode;
// GLOBAL: WIZ8 0x0069C80B
static unsigned char gubCursorPos;
// GLOBAL: WIZ8 0x0069C80C
static unsigned char gubStartHilite;
// GLOBAL: WIZ8 0x0069C80D
static unsigned char gubEndHilite;
// GLOBAL: WIZ8 0x0069C80E
static unsigned char gubParkingPos;
// GLOBAL: WIZ8 0x0069C80F
static unsigned char gubVisibleStart;
// GLOBAL: WIZ8 0x0069C7E9
static unsigned char gubMouseDownPos;
// GLOBAL: WIZ8 0x0069C5D8
static int gsCursorX;
// GLOBAL: WIZ8 0x0069C7E4
static size_t guiVisibleCount;

// FUNCTION: WIZ8 0x005D3470
void InitTextInputMode(void)
{
    if (gpTextInputHead != 0) {
        auto session = std::make_unique<STACKTEXTINPUTNODE>();
        auto* head = gpTextInputHead.get();
        session->head = std::move(gpTextInputHead);
        session->pColors = std::move(pColors);
        session->next = std::move(pInputStack);
        pInputStack = std::move(session);
        for (TEXTINPUTNODE* field = head; field != 0; field = field->next.get()) {
            if (field->fEnabled != 0) {
                MSYS_DisableRegion(&field->region);
                field->fEnabled = 0;
            }
        }
        gpActive = 0;
    }
    gpTextInputHead.reset();
    gpTextInputTail = nullptr;
    pColors = std::make_unique<TextInputColors>();
    gfTextInputMode = true;
    gfEditingText = false;
    pColors->fBevelling = false;
    pColors->fUseDisabledAutoShade = true;
    pColors->usCursorColor = Get16BPPColor(0x0a0a0a);
    gubVisibleStart = 0;
}

// FUNCTION: WIZ8 0x005D3520
void InitTextInputModeWithScheme(int mode)
{
    InitTextInputMode();
    SetTextInputScheme(mode);
}

// FUNCTION: WIZ8 0x005D35E0
void SetTextInputScheme(char mode)
{
    if (mode == 0) {
        pColors->usFont = static_cast<short>(g_font12point1);
        pColors->usTextFieldColor = Get16BPPColor(0x00c8c8);
        pColors->usInactiveTextFieldColor = Get16BPPColor(0xffffff);
        pColors->usDarkerColor = Get16BPPColor(0x513d18);
        pColors->usBrighterColor = Get16BPPColor(0x878a88);
        pColors->fBevelling = true;
        pColors->ubForeColor = 0x8d;
        pColors->ubShadowColor = 0;
        pColors->ubHiForeColor = 0xd0;
    } else if (mode == 1) {
        pColors->usFont = static_cast<short>(g_wiz_text_mono_font);
        pColors->usTextFieldColor = Get16BPPColor(0x632a1e);
        pColors->usInactiveTextFieldColor = Get16BPPColor(0xffffff);
        pColors->usInactiveTextFieldColor = Get16BPPColor(0x0a0a0a);
        pColors->usDarkerColor = Get16BPPColor(0);
        pColors->usBrighterColor = Get16BPPColor(0);
        pColors->fBevelling = true;
        pColors->ubForeColor = 4;
        pColors->ubShadowColor = 5;
        pColors->ubHiForeColor = 4;
        pColors->ubHiShadowColor = 5;
        pColors->ubHiBackColor = 3;
        pColors->usCursorColor = Get16BPPColor(0xffffff);
        return;
    } else if (mode == 2) {
        pColors->usFont = static_cast<short>(g_wiz_text_mono_font);
        pColors->usTextFieldColor = Get16BPPColor(0xffffff);
        pColors->usInactiveTextFieldColor = Get16BPPColor(0xffffff);
        pColors->usDarkerColor = Get16BPPColor(0);
        pColors->usBrighterColor = Get16BPPColor(0);
        pColors->fBevelling = true;
        pColors->ubForeColor = 0x8d;
        pColors->ubShadowColor = 0;
        pColors->ubHiForeColor = 0xd0;
    } else {
        return;
    }
    pColors->ubHiShadowColor = 0xcd;
    pColors->ubHiBackColor = 0xcd;
    pColors->usCursorColor = Get16BPPColor(0x0a0a0a);
}

// FUNCTION: WIZ8 0x005D3800
void KillTextInputMode(void)
{
    gpActive = nullptr;
    gpTextInputTail = nullptr;
    gpTextInputHead.reset();
    pColors.reset();
    auto session = std::move(pInputStack);
    if (!session) {
        gfTextInputMode = false;
        gfEditingText = false;
        return;
    }
    gpTextInputHead = std::move(session->head);
    pColors = std::move(session->pColors);
    pInputStack = std::move(session->next);
    gfTextInputMode = true;
    auto* field = gpTextInputHead.get();
    for (field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        gpTextInputTail = field;
        if (field->fEnabled == 0) {
            MSYS_EnableRegion(&field->region);
            field->fEnabled = 1;
        }
    }
    field = gpTextInputHead.get();
    if (gpActive == 0)
        gpActive = gpTextInputHead.get();
    for (; field != 0; field = field->next.get()) {
        if (field == gpActive || field->ubID != 0 || field->fEnabled == 0)
            continue;
        gpActive = field;
        if (field->szString == 0) {
            gfHiliteMode = false;
            gfEditingText = false;
            if (field->InputCallback != 0)
                field->InputCallback(field->ubID, 1);
        } else {
            gubStartHilite = 0;
            gubEndHilite = field->ubStrLen;
            gubCursorPos = field->ubStrLen;
            gubParkingPos = CalculateCursorPos(
                field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10, gubCursorPos,
                field->szString.get(), &gsCursorX, &guiVisibleCount);
            gubCursorPos = gpActive->ubStrLen;
            gfHiliteMode = true;
            gfEditingText = true;
        }
        break;
    }
    if (gpTextInputHead == 0)
        gpActive = 0;
}

// FUNCTION: WIZ8 0x005D39B0
char AddTextInputField(int left, int top, int width, int height, int priority, const char* text,
                       unsigned char capacity, short input_type,
                       unsigned char use_inactive_text_field_color)
{
    auto owner = std::make_unique<TEXTINPUTNODE>();
    auto* field = owner.get();
    field->usInputType = input_type;
    if (input_type == 0x1002)
        capacity = 6;
    field->max_code_units = capacity;
    capacity = static_cast<unsigned char>(std::min<unsigned>(3u * capacity, 255));
    field->szString = std::make_unique<char[]>(capacity + 1);
    if (text == 0) {
        field->ubStrLen = 0;
        sprintf(field->szString.get(), &g_empty_text);
    } else {
        wiz8::text::copy(field->szString.get(), capacity + 1, text);
        field->ubStrLen = static_cast<unsigned char>(strlen(field->szString.get()));
    }
    field->ubMaxChars = capacity;
    if (gpTextInputHead == 0) {
        gpTextInputHead = std::move(owner);
        gpTextInputTail = field;
        field->ubID = 0;
    } else {
        gpTextInputTail->next = std::move(owner);
        field->prev = gpTextInputTail;
        field->ubID = gpTextInputTail->ubID + 1;
        gpTextInputTail = field;
    }
    if (gpTextInputHead.get() == field) {
        gubStartHilite = 0;
        gubEndHilite = field->ubStrLen;
        gubCursorPos = field->ubStrLen;
        if (gpActive != 0) {
            gubParkingPos = CalculateCursorPos(
                gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
        }
        gfHiliteMode = true;
    }
    field->fUserField = false;
    field->fEnabled = 1;
    field->fBlockMouseCallbacks = false;
    MSYS_DefineRegion(&field->region, static_cast<unsigned short>(left),
                      static_cast<unsigned short>(top), static_cast<unsigned short>(left + width),
                      static_cast<unsigned short>(top + height), static_cast<signed char>(priority),
                      MouseMovedInTextRegionCallback,
                      MouseClickedInTextRegionCallback);
    MSYS_SetRegionUserData(&field->region, 0, field->ubID);
    field->fUseInactiveTextFieldColor = use_inactive_text_field_color;
    return field->ubID;
}

// FUNCTION: WIZ8 0x005D3B40
void RemoveTextInputField(int index)
{
    TEXTINPUTNODE* field = gpTextInputHead.get();
    while (field != 0 && field->ubID != index)
        field = field->next.get();
    if (field == 0)
        return;
    if (field == gpTextInputTail)
        gpTextInputTail = field->prev;
    if (field->next)
        field->next->prev = field->prev;
    if (field == gpActive)
        gpActive = nullptr;
    auto& link = field->prev ? field->prev->next : gpTextInputHead;
    auto removed = std::move(link);
    link = std::move(removed->next);
    if (gpTextInputHead == 0) {
        gfTextInputMode = false;
        gfEditingText = false;
    }
}

// FUNCTION: WIZ8 0x005D3C10
void SetInputFieldText(unsigned char index, char* text)
{
    TEXTINPUTNODE* field = gpTextInputHead.get();
    while (field != 0) {
        if (field->ubID == index) {
            if (text != 0) {
                wiz8::text::copy(field->szString.get(), field->ubMaxChars + 1, text);
                field->ubStrLen = static_cast<unsigned char>(strlen(field->szString.get()));
            } else if (!field->fUserField) {
                field->ubStrLen = 0;
                sprintf(field->szString.get(), &g_empty_text);
            }
            gfHiliteMode = false;
            SetTextInputCursor(0);
            return;
        }
        field = field->next.get();
    }
}

// FUNCTION: WIZ8 0x005D3BF0
short GetActiveTextInputField(void)
{
    if (gpActive != 0)
        return gpActive->ubID;
    return -1;
}

// FUNCTION: WIZ8 0x005D3CC0
void GetTextFromField(unsigned char index, std::span<char> text)
{
    TEXTINPUTNODE* field = gpTextInputHead.get();
    while (field != 0) {
        if (field->ubID == index) {
            wiz8::text::copy(text.data(), text.size(), field->szString.get());
            return;
        }
        field = field->next.get();
    }
    if (!text.empty()) text[0] = '\0';
}

// FUNCTION: WIZ8 0x005D3D00
unsigned char GetTextInputFieldLength(unsigned char index)
{
    for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        if (field->ubID == index)
            return field->ubStrLen;
    }
    return 0;
}

// FUNCTION: WIZ8 0x005D3D20
void SetActiveField(char index)
{
    TEXTINPUTNODE* field = gpTextInputHead.get();
    while (field != 0 && (field == gpActive || field->ubID != index || field->fEnabled == 0)) {
        field = field->next.get();
    }
    if (field == 0)
        return;
    gpActive = field;
    if (field->szString != 0) {
        gubStartHilite = 0;
        gubEndHilite = field->ubStrLen;
        gubCursorPos = field->ubStrLen;
        gubParkingPos =
            CalculateCursorPos(field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10,
                               gubCursorPos, field->szString.get(), &gsCursorX, &guiVisibleCount);
        gubCursorPos = gpActive->ubStrLen;
        gfHiliteMode = true;
        gfEditingText = true;
        return;
    }
    gfHiliteMode = false;
    gfEditingText = false;
    if (field->InputCallback != 0)
        field->InputCallback(field->ubID, 1);
}

// FUNCTION: WIZ8 0x005D3DF0
void SelectNextField(void)
{
    if (gpActive == 0)
        return;
    TEXTINPUTNODE* previous = gpActive;
    if (previous->szString == 0) {
        if (previous->InputCallback != 0)
            previous->InputCallback(previous->ubID, 0);
    } else {
        RenderInactiveTextFieldNode(previous);
    }

    bool found = false;
    do {
        gpActive = gpActive->next.get();
        if (gpActive == 0)
            gpActive = gpTextInputHead.get();
        if (gpActive->fEnabled != 0) {
            found = true;
            if (gpActive->szString == 0) {
                gfHiliteMode = false;
                gfEditingText = false;
                if (gpActive->InputCallback != 0)
                    gpActive->InputCallback(gpActive->ubID, 1);
            } else {
                gubStartHilite = 0;
                gubEndHilite = gpActive->ubStrLen;
                gubCursorPos = gpActive->ubStrLen;
                gubParkingPos = CalculateCursorPos(
                    gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                    gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
                gfHiliteMode = true;
                gfEditingText = true;
            }
        }
        if (gpActive == previous)
            break;
        if (found)
            return;
    } while (true);
    gfEditingText = false;
}

// FUNCTION: WIZ8 0x005D3F00
void ClearActiveField(void)
{
    if (gpActive == 0)
        return;
    if (gpActive->szString != 0) {
        RenderInactiveTextFieldNode(gpActive);
    } else if (gpActive->InputCallback != 0) {
        gpActive->InputCallback(gpActive->ubID, 0);
    }
    gfEditingText = false;
    gpActive = 0;
}

static void DeleteHighlightedText(unsigned char first, unsigned char last)
{
    if (last < first) {
        unsigned char swap = first;
        first = last;
        last = swap;
    }
    memmove(gpActive->szString.get() + first, gpActive->szString.get() + last,
            (gpActive->ubStrLen - last + 1) * sizeof(char));
    gpActive->ubStrLen -= last - first;
    gubStartHilite = 0;
    gubEndHilite = 0;
    SetTextInputCursor(first);
}

// FUNCTION: WIZ8 0x005D3F50
unsigned int HandleTextInput(const InputAtom* input)
{
    if (!gfTextInputMode || !gfEditingText || gpActive == 0 ||
        (input->usEvent != KEY_DOWN && input->usEvent != KEY_REPEAT) ||
        input->usParam == VK_ESCAPE || input->usParam == VK_RETURN || input->usParam == VK_TAB ||
        (input->usKeyState & ALT_DOWN) != 0 ||
        ((input->usKeyState & CTRL_DOWN) != 0 && input->usParam != VK_DELETE &&
         input->usParam != VK_RIGHT && input->usParam != VK_LEFT) ||
        (input->usParam > 0x6f && input->usParam < 0x7c)) {
        return 0;
    }

    unsigned char selection_end = gubEndHilite;
    switch (input->usParam) {
    case 0x25: /* Left */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            if (gubCursorPos != 0) {
                gubCursorPos = static_cast<unsigned char>(wiz8::text::previous(gpActive->szString.get(), gubCursorPos));
                gubParkingPos = CalculateCursorPos(
                    gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                    gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
            }
            gubEndHilite = gubCursorPos;
            return 1;
        }
        if (gfHiliteMode) {
            gubCursorPos = gubStartHilite;
            gfHiliteMode = false;
            gubParkingPos = CalculateCursorPos(
                gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
            return 1;
        }
        if (gubCursorPos != 0) {
            gubCursorPos = static_cast<unsigned char>(wiz8::text::previous(gpActive->szString.get(), gubCursorPos));
            gubParkingPos = CalculateCursorPos(
                gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
        }
        return 1;

    case 0x27: /* Right */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            if (gubCursorPos < gpActive->ubStrLen) {
                { std::size_t next = gubCursorPos;
                wiz8::text::next(gpActive->szString.get(), next);
                gubCursorPos = static_cast<unsigned char>(next); }
                gubParkingPos = CalculateCursorPos(
                    gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                    gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
            }
            gubEndHilite = gubCursorPos;
            return 1;
        }
        if (gfHiliteMode) {
            gubCursorPos = selection_end;
            gfHiliteMode = false;
            gubParkingPos = CalculateCursorPos(
                gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
            return 1;
        }
        if (gubCursorPos < gpActive->ubStrLen) {
            { std::size_t next = gubCursorPos;
                wiz8::text::next(gpActive->szString.get(), next);
                gubCursorPos = static_cast<unsigned char>(next); }
            gubParkingPos = CalculateCursorPos(
                gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
        }
        return 1;

    case 0x23: /* End */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            gubCursorPos = gpActive->ubStrLen;
            gubEndHilite = gubCursorPos;
        } else {
            gfHiliteMode = false;
            gubCursorPos = gpActive->ubStrLen;
        }
        gubParkingPos = CalculateCursorPos(
            gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
            gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
        return 1;

    case 0x24: /* Home */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            gubCursorPos = 0;
            gubEndHilite = 0;
        } else {
            gubCursorPos = 0;
            gfHiliteMode = false;
        }
        gubParkingPos = CalculateCursorPos(gpActive->region.RegionBottomRightX -
                                               gpActive->region.RegionTopLeftX - 10,
                                           0, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
        return 1;

    case 0x2e: /* Delete */
        if ((input->usKeyState & CTRL_DOWN) != 0) {
            gpActive->szString[0] = '\0';
            gpActive->ubStrLen = 0;
            gubCursorPos = 0;
            gubStartHilite = 0;
            gubEndHilite = 0;
            gfHiliteMode = false;
            SetTextInputCursor(0);
            return 1;
        }
        if (!gfHiliteMode) {
            /* 0x005D5D0C returns from inside the guard without repositioning the
               cursor, so gParkingPos keeps the value it had for the longer
               string. The retail's one SetTextInputCursor call in this function
               is on the highlighted path below. */
            if (gubCursorPos < gpActive->ubStrLen) {
                std::size_t end = gubCursorPos;
                wiz8::text::next(gpActive->szString.get(), end);
                memmove(gpActive->szString.get() + gubCursorPos, gpActive->szString.get() + end,
                        gpActive->ubStrLen - end + 1);
                gpActive->ubStrLen -= end - gubCursorPos;
            }
            return 1;
        }
        gfHiliteMode = false;
        if (gubStartHilite != selection_end) {
            DeleteHighlightedText(gubStartHilite, selection_end);
        }
        return 1;

    case 8:
        if (!gfHiliteMode) {
            if (gubCursorPos != 0) {
                const auto old_position = gubCursorPos;
                gubCursorPos = static_cast<unsigned char>(wiz8::text::previous(gpActive->szString.get(), gubCursorPos));
                memmove(gpActive->szString.get() + gubCursorPos, gpActive->szString.get() + old_position,
                        gpActive->ubStrLen - old_position + 1);
                gpActive->ubStrLen -= old_position - gubCursorPos;
                gubParkingPos = CalculateCursorPos(
                    gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10,
                    gubCursorPos, gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
                return 1;
            }
        } else {
            gfHiliteMode = false;
            if (gubStartHilite != selection_end) {
                DeleteHighlightedText(gubStartHilite, selection_end);
                return 1;
            }
        }
        break;

    default: {
        unsigned int character =
            TranslateKeyToCharacter(static_cast<unsigned short>(input->usParam), input->usKeyState);
        if (character == 0)
            return 1;
        if (character == 0x25 || character == 0x5c)
            return 0;

        if (gfHiliteMode) {
            gfHiliteMode = false;
            if (gubStartHilite != gubEndHilite) {
                DeleteHighlightedText(gubStartHilite, gubEndHilite);
            }
        }

        unsigned short input_type = static_cast<unsigned short>(gpActive->usInputType);
        if (input_type > 0x0fff) {
            HandleExclusiveInput(static_cast<unsigned short>(character));
            return 1;
        }
        if (character == ' ' && (input_type & 4) != 0) {
            AddChar(' ');
            return 1;
        }
        if (character == '-' && (input_type & 2) != 0 && gubCursorPos == 0) {
            AddChar('-');
            return 1;
        }
        if (character >= '0' && character <= '9' && (input_type & 1) != 0) {
            AddChar(static_cast<unsigned short>(character));
            return 1;
        }
        if ((input_type & 2) != 0) {
            if (IsUppercaseWideChar(static_cast<unsigned short>(character)) != 0) {
                if ((input_type & 0x20) != 0)
                    character = ToLowercaseWideChar(character);
                AddChar(static_cast<unsigned short>(character));
                return 1;
            }
            if (IsLowercaseWideChar(static_cast<unsigned short>(character)) != 0) {
                if ((input_type & 0x10) != 0)
                    character = ToUppercaseWideChar(character);
                AddChar(static_cast<unsigned short>(character));
                return 1;
            }
        }
        if ((input_type & 8) != 0 &&
            IsPunctuationWideChar(static_cast<unsigned short>(character)) != 0) {
            AddChar(static_cast<unsigned short>(character));
        }
        return 1;
    }
    }
    return 1;
}

// FUNCTION: WIZ8 0x005D4970
void HandleExclusiveInput(unsigned short character)
{
    short input_type = gpActive->usInputType;
    if (input_type == 0x1000) {
        if (IsUppercaseWideChar(character) == 0 && IsLowercaseWideChar(character) == 0 &&
            (character < '0' || character > '9') && character != '_' && character != '.') {
            return;
        }
        if (gubCursorPos == 0 && character >= '0' && character <= '9')
            return;
        AddChar(character);
        return;
    }
    if (input_type == 0x1001) {
        if (gubCursorPos == 0) {
            if (IsLowercaseWideChar(character) != 0) {
                AddChar(character);
                return;
            }
            if (IsUppercaseWideChar(character) == 0)
                return;
            AddChar(static_cast<unsigned short>(ToLowercaseWideChar(character)));
            return;
        }
        if (character >= '0' && character <= '9')
            AddChar(character);
        return;
    }
    if (input_type != 0x1002)
        return;
    if (gubCursorPos == 0) {
        if (character >= '0' && character <= '2')
            AddChar(character);
        return;
    }
    if (gubCursorPos == 1) {
        if (character >= '0' && character <= '9') {
            if (gpActive->szString[0] != '2' || character <= '3')
                AddChar(character);
        }
        if (gpActive->szString[2] == '\0') {
            AddChar(':');
            return;
        }
        { std::size_t next = gubCursorPos;
                wiz8::text::next(gpActive->szString.get(), next);
                gubCursorPos = static_cast<unsigned char>(next); }
        SetTextInputCursor(gubCursorPos);
        return;
    }
    if (gubCursorPos == 2) {
        if (character == ':') {
            AddChar(':');
            return;
        }
        if (character < '0' || character > '9')
            return;
        AddChar(':');
        AddChar(character);
        return;
    }
    if (gubCursorPos == 3) {
        if (character >= '0' && character <= '5')
            AddChar(character);
        return;
    }
    if (gubCursorPos == 4 && character >= '0' && character <= '9') {
        AddChar(character);
    }
}

// FUNCTION: WIZ8 0x005D4B70
void AddChar(unsigned short character)
{
    const auto scalar = static_cast<char16_t>(character);
    const auto bytes = wiz8::text::from_utf16(std::u16string_view(&scalar, 1));
    const auto length = gpActive->ubStrLen;
    if (length + bytes.size() > gpActive->ubMaxChars ||
        wiz8::text::to_utf16(gpActive->szString.get()).size() + 1 > gpActive->max_code_units) return;
    memmove(gpActive->szString.get() + gubCursorPos + bytes.size(),
            gpActive->szString.get() + gubCursorPos, length - gubCursorPos + 1);
    memcpy(gpActive->szString.get() + gubCursorPos, bytes.data(), bytes.size());
    gpActive->ubStrLen += bytes.size();
    gubCursorPos += bytes.size();
    gubParkingPos = CalculateCursorPos(
        gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10, gubCursorPos,
        gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
}

static unsigned char FindTextInputMousePosition(TEXTINPUTNODE* field, unsigned char position,
                                                int mouse_offset)
{
    unsigned int start = position;
    short width = StringPixLengthArg(pColors->usFont, 1, field->szString.get() + start);
    if ((width / 2) / 2 < mouse_offset) {
        int count = 1;
        int previous_width = width / 2;
        do {
            if (field->ubStrLen <= position)
                break;
            std::size_t advance = position;
            wiz8::text::next(field->szString.get(), advance);
            position = static_cast<unsigned char>(advance);
            ++count;
            width = StringPixLengthArg(pColors->usFont, count, field->szString.get() + start);
            int midpoint = (width - previous_width) / 2 + previous_width;
            previous_width = width;
            if (mouse_offset <= midpoint)
                break;
        } while (true);
    }
    return position;
}

// FUNCTION: WIZ8 0x005D4CB0
void MouseMovedInTextRegionCallback(MOUSE_REGION* region, int reason)
{
    if (IsModalOpen())
        return;

    int field_index = MSYS_GetRegionUserData(region, 0);
    for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        if (field->ubID == field_index && field->fBlockMouseCallbacks)
            return;
    }

    if ((reason & MSYS_CALLBACK_REASON_GAIN_MOUSE) != 0)
        SetTargetCursor(GetTextInputCursor());
    if ((reason & MSYS_CALLBACK_REASON_LOST_MOUSE) != 0)
        SetTargetCursor(W8_CURSOR_NONE);

    if (gfLeftButtonState == 0 || gpActive == 0 || (reason & MSYS_CALLBACK_REASON_MOVE) == 0) {
        return;
    }

    field_index = MSYS_GetRegionUserData(region, 0);
    if (field_index != gpActive->ubID) {
        RenderInactiveTextFieldNode(gpActive);
        for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
            if (field->ubID == field_index) {
                gubMouseDownPos = 0;
                gubCursorPos = 0;
                gpActive = field;
                gubParkingPos = CalculateCursorPos(
                    field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10, 0,
                    field->szString.get(), &gsCursorX, &guiVisibleCount);
                gfHiliteMode = false;
                gubStartHilite = 0;
                gubEndHilite = 0;
                break;
            }
        }
    }

    TEXTINPUTNODE* current_field = gpActive;
    if (current_field->szString == 0 || gfLeftButtonState == 0)
        return;

    unsigned char position = gubParkingPos;
    int mouse_offset = gusMouseXPos - current_field->region.RegionTopLeftX;
    position = FindTextInputMousePosition(current_field, position, mouse_offset);

    if (position == gubMouseDownPos) {
        gfHiliteMode = false;
        return;
    }
    if (gubMouseDownPos < position) {
        gubStartHilite = gubMouseDownPos;
        gubEndHilite = position;
    } else {
        gubEndHilite = gubMouseDownPos;
        gubStartHilite = position;
    }
    gfHiliteMode = true;
    gubCursorPos = position;
    gubParkingPos = CalculateCursorPos(
        current_field->region.RegionBottomRightX - current_field->region.RegionTopLeftX - 10,
        position, current_field->szString.get(), &gsCursorX, &guiVisibleCount);
}

// FUNCTION: WIZ8 0x005D4F10
void MouseClickedInTextRegionCallback(MOUSE_REGION* region, int reason)
{
    int field_index = MSYS_GetRegionUserData(region, 0);
    if (IsModalOpen())
        return;

    for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        if (field->ubID == field_index && field->fBlockMouseCallbacks)
            return;
    }

    if ((reason & MSYS_CALLBACK_REASON_LBUTTON_DOUBLECLICK) != 0) {
        if (gpActive != 0)
            SelectAllText();
        return;
    }

    if ((reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) != 0) {
        TEXTINPUTNODE* field = gpActive;
        if (field == 0 || field_index != field->ubID)
            return;

        unsigned char position = gubParkingPos;
        if (field->szString == 0) {
            position = 0;
        } else {
            int mouse_offset = gusMouseXPos - field->region.RegionTopLeftX;
            unsigned int start = gubParkingPos;
            short width = StringPixLengthArg(pColors->usFont, 1, field->szString.get() + start);
            if ((width / 2) / 2 < mouse_offset) {
                int count = 1;
                int previous_width = width / 2;
                do {
                    std::size_t advance = position;
                    wiz8::text::next(field->szString.get(), advance);
                    position = static_cast<unsigned char>(advance);
                    ++count;
                    width = StringPixLengthArg(pColors->usFont, count, field->szString.get() + start);
                    int midpoint = (width - previous_width) / 2 + previous_width;
                    previous_width = width;
                    if (field->ubStrLen <= position || mouse_offset <= midpoint)
                        break;
                } while (true);
            }
        }
        SetTextInputCursor(position);
        gubMouseDownPos = gubCursorPos;
        MSYS_GrabMouse(region);
        return;
    }

    if ((reason & MSYS_CALLBACK_REASON_LBUTTON_UP) == 0)
        return;
    MSYS_ReleaseMouse(region);

    TEXTINPUTNODE* clicked = gpTextInputHead.get();
    if (gpActive != 0) {
        if (field_index != gpActive->ubID)
            RenderInactiveTextFieldNode(gpActive);
        clicked = gpTextInputHead.get();
        if (field_index == gpActive->ubID)
            clicked = gpActive;
    }

    if (clicked != gpActive) {
        while (clicked != 0 && clicked->ubID != field_index)
            clicked = clicked->next.get();
        if (clicked == 0)
            return;

        TEXTINPUTNODE* candidate = gpTextInputHead.get();
        while (candidate != 0 && (candidate == gpActive || candidate->ubID != clicked->ubID ||
                                  candidate->fEnabled == 0)) {
            candidate = candidate->next.get();
        }
        if (candidate == 0)
            return;

        gpActive = candidate;
        if (candidate->szString == 0) {
            gfHiliteMode = false;
            gfEditingText = false;
            if (candidate->InputCallback != 0)
                candidate->InputCallback(candidate->ubID, 1);
            return;
        }
        gubStartHilite = 0;
        gubEndHilite = candidate->ubStrLen;
        gubCursorPos = candidate->ubStrLen;
        SetTextInputCursor(gubCursorPos);
        gubCursorPos = candidate->ubStrLen;
        gfHiliteMode = true;
        gfEditingText = true;
        return;
    }

    unsigned char position = gubParkingPos;
    if (gfLeftButtonState != 0) {
        if (gpActive->szString == 0) {
            position = 0;
        } else {
            TEXTINPUTNODE* field = gpActive;
            int mouse_offset = gusMouseXPos - field->region.RegionTopLeftX;
            position = FindTextInputMousePosition(field, position, mouse_offset);
        }
        if (position == gubMouseDownPos)
            gfHiliteMode = false;
        SetTextInputCursor(position);
    }
}

// FUNCTION: WIZ8 0x005D52C0
void RenderBackgroundField(TEXTINPUTNODE* field)
{
    TextInputColors* style = pColors.get();
    int left = field->region.RegionTopLeftX;
    int top = field->region.RegionTopLeftY;
    int right = field->region.RegionBottomRightX;
    int bottom = field->region.RegionBottomRightY;

    if (style->fBevelling) {
        ColorFillVideoSurfaceArea(FRAME_BUFFER, left, top, right, bottom, style->usDarkerColor);
        ColorFillVideoSurfaceArea(FRAME_BUFFER, left + 1, top + 1, right, bottom,
                                  style->usBrighterColor);
    }

    unsigned short colour;
    if (field->fEnabled == 0 && !style->fUseDisabledAutoShade)
        colour = style->usDisabledTextFieldColor;
    else
        colour = style->usTextFieldColor;
    if (field->fUseInactiveTextFieldColor && field != gpActive)
        colour = style->usInactiveTextFieldColor;

    ColorFillVideoSurfaceArea(FRAME_BUFFER, left, top, right, bottom, colour);
    InvalidateRegion(left, top, right, bottom, 0);
}

// FUNCTION: WIZ8 0x005D5390
void RenderActiveTextField(void)
{
    TEXTINPUTNODE* field = gpActive;
    if (field == 0 || field->szString == 0)
        return;

    if (gfLeftButtonState != 0) {
        if (static_cast<int>(gusMouseXPos) < field->region.RegionTopLeftX) {
            if (gubCursorPos != 0) {
                gubCursorPos = static_cast<unsigned char>(wiz8::text::previous(gpActive->szString.get(), gubCursorPos));
                gubParkingPos = CalculateCursorPos(
                    field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10,
                    gubCursorPos, field->szString.get(), &gsCursorX, &guiVisibleCount);
            }
            if (gfHiliteMode)
                gubStartHilite = gubVisibleStart;
        } else if (field->region.RegionBottomRightX < static_cast<int>(gusMouseXPos)) {
            if (gubCursorPos < field->ubStrLen) {
                { std::size_t next = gubCursorPos;
                wiz8::text::next(gpActive->szString.get(), next);
                gubCursorPos = static_cast<unsigned char>(next); }
                gubParkingPos = CalculateCursorPos(
                    field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10,
                    gubCursorPos, field->szString.get(), &gsCursorX, &guiVisibleCount);
            }
            if (gfHiliteMode)
                gubEndHilite = static_cast<unsigned char>(guiVisibleCount + gubVisibleStart);
        }
    }

    SaveFontSettings();
    SetFont(pColors->usFont);
    unsigned short font_height = GetFontHeight(pColors->usFont);
    unsigned int vertical_offset =
        (field->region.RegionBottomRightY - field->region.RegionTopLeftY - font_height) / 2;
    RenderBackgroundField(field);

    const char* visible = field->szString.get() + gubParkingPos;

    bool has_selection = gfHiliteMode && gubStartHilite != gubEndHilite;
    unsigned char selection_first = gubEndHilite;
    unsigned char selection_last = gubStartHilite;
    if (gubStartHilite < gubEndHilite) {
        selection_first = gubStartHilite;
        selection_last = gubEndHilite;
    }

    for (size_t index = 0; index < guiVisibleCount && visible[index];) {
        short prefix = StringNPixLength(const_cast<char*>(visible), index, pColors->usFont);
        unsigned char background;
        if (has_selection &&
            static_cast<int>(selection_first - gubParkingPos) <= static_cast<int>(index) &&
            static_cast<int>(index) < static_cast<int>(selection_last - gubParkingPos)) {
            SetFontForeground(pColors->ubHiForeColor);
            SetFontShadow(pColors->ubHiShadowColor);
            background = pColors->ubHiBackColor;
        } else {
            SetFontForeground(pColors->ubForeColor);
            SetFontShadow(pColors->ubShadowColor);
            background = 0;
        }
        SetFontBackground(background);
        std::size_t end = index;
        wiz8::text::next(visible, end);
        const std::string glyph(visible + index, end - index);
        mprintf(field->region.RegionTopLeftX + prefix + 3,
                field->region.RegionTopLeftY + vertical_offset, "%s", glyph.c_str());
        index = end;
    }

    if (gfEditingText && field->szString != 0 && gfLeftButtonState == 0 &&
        GetTickCount() % 1000 < 500) {
        int left = field->region.RegionTopLeftX + gsCursorX;
        int top = field->region.RegionTopLeftY + vertical_offset;
        ColorFillVideoSurfaceArea(FRAME_BUFFER, left, top, left + 1, top + font_height,
                                  pColors->usCursorColor);
    }
    RestoreFontSettings();
}

// FUNCTION: WIZ8 0x005D5770
void RenderInactiveTextFieldNode(TEXTINPUTNODE* field)
{
    if (field == 0 || field->szString == 0)
        return;

    SaveFontSettings();
    SetFont(pColors->usFont);
    bool disabled = field->fEnabled == 0 && pColors->fUseDisabledAutoShade;
    unsigned char shadow;
    if (disabled) {
        SetFontForeground(pColors->ubDisabledForeColor);
        shadow = pColors->ubDisabledShadowColor;
    } else {
        SetFontForeground(pColors->ubForeColor);
        shadow = pColors->ubShadowColor;
    }
    SetFontShadow(shadow);
    unsigned short font_height = GetFontHeight(pColors->usFont);
    unsigned int vertical_offset =
        (field->region.RegionBottomRightY - field->region.RegionTopLeftY - font_height) / 2;
    SetFontBackground(0);
    RenderBackgroundField(field);

    const char* visible = field->szString.get();
    for (size_t index = 0; visible[index];) {
        short prefix = StringNPixLength(field->szString.get(), index, pColors->usFont);
        if (field->region.RegionBottomRightX - field->region.RegionTopLeftX - 10 < prefix + 3) break;
        std::size_t end = index;
        wiz8::text::next(visible, end);
        const std::string glyph(visible + index, end - index);
        mprintf(field->region.RegionTopLeftX + prefix + 3,
                field->region.RegionTopLeftY + vertical_offset, "%s", glyph.c_str());
        index = end;
    }
    RestoreFontSettings();

    if (disabled) {
        SGPRect rectangle = {
            field->region.RegionTopLeftX,
            field->region.RegionTopLeftY,
            field->region.RegionBottomRightX,
            field->region.RegionBottomRightY,
        };
        unsigned int pitch;
        void* pixels = LockVideoSurface(FRAME_BUFFER, &pitch);
        Blt16BPPBufferShadowRect(static_cast<unsigned short*>(pixels), pitch, &rectangle);
        UnLockVideoSurface(FRAME_BUFFER);
    }
}

// FUNCTION: WIZ8 0x005D59A0
void RenderAllTextFields(void)
{
    for (STACKTEXTINPUTNODE* session = pInputStack.get(); session != 0; session = session->next.get()) {
        for (TEXTINPUTNODE* field = session->head.get(); field != 0; field = field->next.get()) {
            RenderInactiveTextFieldNode(field);
        }
    }
    for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        if (field == gpActive)
            RenderActiveTextField();
        else
            RenderInactiveTextFieldNode(field);
    }
}

// FUNCTION: WIZ8 0x005D5A00
bool EditingText(void)
{
    return gfEditingText;
}

// FUNCTION: WIZ8 0x005D5A10
unsigned int CalculateCursorPos(int width, int cursor, const char* text, int* cursor_width,
                                size_t* visible_count)
{
    char buffer[3 * (512) + 1];
    if (cursor < gubVisibleStart)
        gubVisibleStart = static_cast<unsigned char>(cursor);

    unsigned int start = gubVisibleStart;
    strcpy(buffer, text + start);
    buffer[cursor - start] = '\0';
    int measured = StringPixLength(buffer, pColors->usFont);
    size_t count = strlen(buffer);
    unsigned char retained_start;

    if (width < measured) {
        char* suffix = buffer;
        do {
            std::size_t advance = 0;
            wiz8::text::next(suffix, advance);
            suffix += advance;
            start += advance;
            measured = StringPixLength(suffix, pColors->usFont);
        } while (width < measured);
        retained_start = static_cast<unsigned char>(start);

        if (gubVisibleStart < start) {
            strcpy(buffer, text + start);
            size_t length = strlen(buffer);
            count = length;
            for (size_t index = 0; index < strlen(buffer); wiz8::text::next(buffer, index)) {
                short prefix = StringNPixLength(buffer, index, pColors->usFont);
                count = index;
                if (width < prefix + 3)
                    break;
                count = length;
            }
        }
    } else {
        strcpy(buffer, text + start);
        size_t length = strlen(buffer);
        count = length;
        retained_start = gubVisibleStart;
        for (size_t index = 0; index < strlen(buffer); wiz8::text::next(buffer, index)) {
            short prefix = StringNPixLength(buffer, index, pColors->usFont);
            retained_start = gubVisibleStart;
            count = index;
            if (width < prefix + 3)
                break;
            count = length;
        }
    }

    gubVisibleStart = retained_start;
    *cursor_width = measured + 2;
    *visible_count = count;
    return start;
}

// FUNCTION: WIZ8 0x005D5BF0
void SetTextInputCursor(unsigned char cursor)
{
    if (gpActive) cursor = static_cast<unsigned char>(wiz8::text::prefix(gpActive->szString.get(), cursor));
    gubCursorPos = cursor;
    if (gpActive != 0) {
        gubParkingPos = CalculateCursorPos(
            gpActive->region.RegionBottomRightX - gpActive->region.RegionTopLeftX - 10, cursor,
            gpActive->szString.get(), &gsCursorX, &guiVisibleCount);
    }
}

// FUNCTION: WIZ8 0x005D5C40
void SelectAllText(void)
{
    TEXTINPUTNODE* field = gpActive;
    unsigned char position = gubParkingPos;
    if (field->szString == 0) {
        position = 0;
    } else {
        int mouse_offset = gusMouseXPos - field->region.RegionTopLeftX;
        position = FindTextInputMousePosition(field, position, mouse_offset);
    }

    if (field->szString[position] == ' ')
        return;
    unsigned char first = 0;
    if (position != 0) {
        unsigned int scan = position;
        const char* character = field->szString.get() + position;
        do {
            if (*character == ' ') {
                first = static_cast<unsigned char>(scan + 1);
                break;
            }
            --scan;
            --character;
        } while (scan != 0);
    }

    unsigned char last = static_cast<unsigned char>(strlen(field->szString.get()));
    for (unsigned int scan = position + 1; scan < strlen(field->szString.get()); ++scan) {
        if (field->szString[scan] == ' ') {
            last = static_cast<unsigned char>(scan);
            break;
        }
    }
    gubStartHilite = first;
    gubEndHilite = last;
    gfHiliteMode = true;
}

/* Sets whether the named field's mouse region callback is
   suppressed; the NPC dialogue toggles it on field 0 while the modal is up. */
// FUNCTION: WIZ8 0x005D5DA0
void SetInputFieldBlocksMouseCallback(unsigned char field_id, bool blocks)
{
    TEXTINPUTNODE* field = gpTextInputHead.get();
    if (field != 0) {
        while (field->ubID != field_id) {
            field = field->next.get();
            if (field == 0) {
                return;
            }
        }
        field->fBlockMouseCallbacks = blocks;
    }
}

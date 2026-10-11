#include <SDL3/SDL_pixels.h>
#include "Types.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_mouse.h>
#include <string>
#include <algorithm>
#include <memory>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

#include "native/input_events.h"
#include "utf8/checked.h"
#include "wiz8/unicode.h"
#include "wiz8/cursor.h"
#include "Font.h"
#include "input.h"
#include "Button System.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/regions.h"
#include "wiz8/fonts.h"
#include "wiz8/text_input.h"
#include "wiz8/local_screens/Screens.h"
#include "himage.h"
#include "vobject_blitters.h"
#include "vsurface.h"
#include "compat/native.h"

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
    std::size_t max_code_units; // retail name/editor limit
    unsigned char _padding05[3];
    std::string szString;
    // bool-byte-ok: JA2 declares fEnabled as BOOLEAN (UINT8)
    unsigned char fEnabled;
    bool fUserField;
    unsigned char _padding0f;
    unsigned int region_index = 0;
    INPUT_CALLBACK InputCallback;
    bool fUseInactiveTextFieldColor; /* Wizardry extension at +0x60 */
    bool fBlockMouseCallbacks;       /* Wizardry extension at +0x61 */
    unsigned char _padding62[2];
    std::unique_ptr<TEXTINPUTNODE> next;
    TEXTINPUTNODE* prev;

};

namespace {
int g_text_field_input_x = 0;
SDL_MouseButtonFlags g_text_field_input_buttons = 0;

W8Region& TextFieldRegion(const TEXTINPUTNODE* field)
{
    const auto index = field && field->region_index < g_regions.size()
                           ? field->region_index
                           : 0;
    return g_regions[index];
}

unsigned char TextFieldRegionEvent(const InputAtom* event, W8Region* region)
{
    auto* field = region->text_field;
    if (!field) return 0;
    g_text_field_input_x = GETXPOS(event);
    g_text_field_input_buttons = event->mouseButtons;
    switch (event->usEvent) {
    case MOUSE_POS: {
        int reason = BUTTON_REASON_MOVE;
        if (region->flags & W8_REGION_MOUSE_LEAVE) reason = BUTTON_REASON_LOST_MOUSE;
        else if (region->flags & W8_REGION_MOUSE_ENTER) reason = BUTTON_REASON_GAIN_MOUSE;
        MouseMovedInTextRegionCallback(field, reason);
        return 1;
    }
    case LEFT_BUTTON_DOWN:
        g_captured_region_index = field->region_index;
        MouseClickedInTextRegionCallback(
            field, BUTTON_REASON_LBUTTON_DWN |
                       (event->clicks >= 2 ? BUTTON_REASON_LBUTTON_DOUBLECLICK : 0));
        return 1;
    case LEFT_BUTTON_UP:
        if (g_captured_region_index == field->region_index) g_captured_region_index = 0;
        MouseClickedInTextRegionCallback(field, BUTTON_REASON_LBUTTON_UP);
        return 1;
    case LEFT_BUTTON_DBL_CLK:
        return 1;
    }
    return 0;
}
}

/* 0x005D3520 allocates this record once per active input session, and
   0x005D35E0 fills the same fixed layout for its three presentation modes. */
struct TextInputColors {
    Font* usFont;
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
static std::size_t gubCursorPos;
// GLOBAL: WIZ8 0x0069C80C
static std::size_t gubStartHilite;
// GLOBAL: WIZ8 0x0069C80D
static std::size_t gubEndHilite;
// GLOBAL: WIZ8 0x0069C80E
static std::size_t gubParkingPos;
// GLOBAL: WIZ8 0x0069C80F
static std::size_t gubVisibleStart;
// GLOBAL: WIZ8 0x0069C7E9
static std::size_t gubMouseDownPos;
// GLOBAL: WIZ8 0x0069C5D8
static int gsCursorX;
// GLOBAL: WIZ8 0x0069C7E4
static size_t guiVisibleCount;

// FUNCTION: WIZ8 0x005D3470
void InitTextInputMode(Font& font)
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
                DisableRegionInput(field->region_index);
                field->fEnabled = 0;
            }
        }
        gpActive = 0;
    }
    gpTextInputHead.reset();
    gpTextInputTail = nullptr;
    pColors = std::make_unique<TextInputColors>();
    pColors->usFont = &font;
    gfTextInputMode = true;
    gfEditingText = false;
    SetNativeTextInputEnabled(false);
    pColors->fBevelling = false;
    pColors->fUseDisabledAutoShade = true;
    pColors->usCursorColor = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
    gubVisibleStart = 0;
}

// FUNCTION: WIZ8 0x005D3520
void InitTextInputModeWithScheme(int mode)
{
    Assert(mode >= 0 && mode <= 2);
    InitTextInputMode(mode == 0 ? g_font12point1 : g_wiz_text_mono_font);
    SetTextInputScheme(mode);
}

// FUNCTION: WIZ8 0x005D35E0
void SetTextInputScheme(char mode)
{
    if (mode == 0) {
        pColors->usFont = &g_font12point1;
        pColors->usTextFieldColor = Get16BPPColor(SDL_Color{200, 200, 0, SDL_ALPHA_OPAQUE});
        pColors->usInactiveTextFieldColor = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        pColors->usDarkerColor = Get16BPPColor(SDL_Color{24, 61, 81, SDL_ALPHA_OPAQUE});
        pColors->usBrighterColor = Get16BPPColor(SDL_Color{136, 138, 135, SDL_ALPHA_OPAQUE});
        pColors->fBevelling = true;
        pColors->ubForeColor = 0x8d;
        pColors->ubShadowColor = 0;
        pColors->ubHiForeColor = 0xd0;
    } else if (mode == 1) {
        pColors->usFont = &g_wiz_text_mono_font;
        pColors->usTextFieldColor = Get16BPPColor(SDL_Color{30, 42, 99, SDL_ALPHA_OPAQUE});
        pColors->usInactiveTextFieldColor = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        pColors->usInactiveTextFieldColor = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
        pColors->usDarkerColor = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        pColors->usBrighterColor = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        pColors->fBevelling = true;
        pColors->ubForeColor = 4;
        pColors->ubShadowColor = 5;
        pColors->ubHiForeColor = 4;
        pColors->ubHiShadowColor = 5;
        pColors->ubHiBackColor = 3;
        pColors->usCursorColor = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        return;
    } else if (mode == 2) {
        pColors->usFont = &g_wiz_text_mono_font;
        pColors->usTextFieldColor = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        pColors->usInactiveTextFieldColor = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        pColors->usDarkerColor = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        pColors->usBrighterColor = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        pColors->fBevelling = true;
        pColors->ubForeColor = 0x8d;
        pColors->ubShadowColor = 0;
        pColors->ubHiForeColor = 0xd0;
    } else {
        return;
    }
    pColors->ubHiShadowColor = 0xcd;
    pColors->ubHiBackColor = 0xcd;
    pColors->usCursorColor = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
}

// FUNCTION: WIZ8 0x005D3800
void KillTextInputMode(void)
{
    gpActive = nullptr;
    gpTextInputTail = nullptr;
    for (auto* field = gpTextInputHead.get(); field != nullptr; field = field->next.get()) {
        RemoveRegionFromSet(ButtonRegionSetIndex(), field->region_index);
        field->region_index = 0;
    }
    gpTextInputHead.reset();
    pColors.reset();
    auto session = std::move(pInputStack);
    if (!session) {
        gfTextInputMode = false;
        gfEditingText = false;
        SetNativeTextInputEnabled(false);
        SetNativeTextInputEnabled(false);
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
            EnableRegionInput(field->region_index);
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
        if (field->fUserField) {
            gfHiliteMode = false;
            gfEditingText = false;
            SetNativeTextInputEnabled(false);
            if (field->InputCallback != 0)
                field->InputCallback(field->ubID, 1);
        } else {
            gubStartHilite = 0;
            gubEndHilite = field->szString.size();
            gubCursorPos = field->szString.size();
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10, gubCursorPos,
                field->szString.c_str(), &gsCursorX, &guiVisibleCount);
            gubCursorPos = gpActive->szString.size();
            gfHiliteMode = true;
            gfEditingText = true;
            SetNativeTextInputEnabled(true);
        }
        break;
    }
    if (gpTextInputHead == 0)
        gpActive = 0;
}

// FUNCTION: WIZ8 0x005D39B0
char AddTextInputField(int left, int top, int width, int height, int priority, std::string_view text,
                       std::size_t capacity, short input_type,
                       unsigned char use_inactive_text_field_color)
{
    auto owner = std::make_unique<TEXTINPUTNODE>();
    auto* field = owner.get();
    field->usInputType = input_type;
    if (input_type == 0x1002)
        capacity = 6;
    field->max_code_units = capacity;
    field->szString.assign(text);
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
        gubEndHilite = field->szString.size();
        gubCursorPos = field->szString.size();
        if (gpActive != 0) {
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
        }
        gfHiliteMode = true;
    }
    field->fUserField = false;
    field->fEnabled = 1;
    field->fBlockMouseCallbacks = false;
    field->region_index = AddRegionToSet(ButtonRegionSetIndex(), priority);
    auto& region = g_regions[field->region_index];
    region.text_field = field;
    SetRegionBounds(field->region_index, static_cast<unsigned short>(left),
                    static_cast<unsigned short>(top), static_cast<unsigned short>(left + width),
                    static_cast<unsigned short>(top + height));
    SetRegionCallback(field->region_index, TextFieldRegionEvent, field->ubID);
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
    if (g_captured_region_index == field->region_index) g_captured_region_index = 0;
    RemoveRegionFromSet(ButtonRegionSetIndex(), field->region_index);
    field->region_index = 0;
    if (field == gpTextInputTail)
        gpTextInputTail = field->prev;
    if (field->next)
        field->next->prev = field->prev;
    if (field == gpActive) {
        gpActive = nullptr;
        gfEditingText = false;
        SetNativeTextInputEnabled(false);
    }
    auto& link = field->prev ? field->prev->next : gpTextInputHead;
    auto removed = std::move(link);
    link = std::move(removed->next);
    if (gpTextInputHead == 0) {
        gfTextInputMode = false;
        gfEditingText = false;
        SetNativeTextInputEnabled(false);
    }
}

// FUNCTION: WIZ8 0x005D3C10
void SetInputFieldText(unsigned char index, std::string_view text)
{
    for (auto* field = gpTextInputHead.get(); field; field = field->next.get()) {
        if (field->ubID != index) continue;
        field->szString.assign(text);
        gfHiliteMode = false;
        SetTextInputCursor(0);
        return;
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
std::string GetTextFromField(unsigned char index)
{
    for (auto* field = gpTextInputHead.get(); field; field = field->next.get())
        if (field->ubID == index) return field->szString;
    return {};
}

// FUNCTION: WIZ8 0x005D3D00
std::size_t GetTextInputFieldByteLength(unsigned char index)
{
    for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
        if (field->ubID == index)
            return field->szString.size();
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
    if (!field->fUserField) {
        gubStartHilite = 0;
        gubEndHilite = field->szString.size();
        gubCursorPos = field->szString.size();
        gubParkingPos =
            CalculateCursorPos(TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10,
                               gubCursorPos, field->szString.c_str(), &gsCursorX, &guiVisibleCount);
        gubCursorPos = gpActive->szString.size();
        gfHiliteMode = true;
        gfEditingText = true;
        SetNativeTextInputEnabled(true);
        return;
    }
    gfHiliteMode = false;
    gfEditingText = false;
    SetNativeTextInputEnabled(false);
    if (field->InputCallback != 0)
        field->InputCallback(field->ubID, 1);
}

// FUNCTION: WIZ8 0x005D3DF0
void SelectNextField(void)
{
    if (gpActive == 0)
        return;
    TEXTINPUTNODE* previous = gpActive;
    if (previous->fUserField) {
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
            if (gpActive->fUserField) {
                gfHiliteMode = false;
                gfEditingText = false;
                SetNativeTextInputEnabled(false);
                if (gpActive->InputCallback != 0)
                    gpActive->InputCallback(gpActive->ubID, 1);
            } else {
                gubStartHilite = 0;
                gubEndHilite = gpActive->szString.size();
                gubCursorPos = gpActive->szString.size();
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                    gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
                gfHiliteMode = true;
                gfEditingText = true;
                SetNativeTextInputEnabled(true);
            }
        }
        if (gpActive == previous)
            break;
        if (found)
            return;
    } while (true);
    gfEditingText = false;
    SetNativeTextInputEnabled(false);
}

// FUNCTION: WIZ8 0x005D3F00
void ClearActiveField(void)
{
    if (gpActive == 0)
        return;
    if (!gpActive->fUserField) {
        RenderInactiveTextFieldNode(gpActive);
    } else if (gpActive->InputCallback != 0) {
        gpActive->InputCallback(gpActive->ubID, 0);
    }
    gfEditingText = false;
    SetNativeTextInputEnabled(false);
    gpActive = 0;
}

static void DeleteHighlightedText(std::size_t first, std::size_t last)
{
    if (last < first) {
        std::size_t swap = first;
        first = last;
        last = swap;
    }
    gpActive->szString.erase(first, last - first);
    gubStartHilite = 0;
    gubEndHilite = 0;
    SetTextInputCursor(first);
}

// FUNCTION: WIZ8 0x005D3F50
unsigned int HandleTextInput(const InputAtom* input)
{
    const bool committed = (input->uiParam & NATIVE_TEXT_INPUT_FLAG) != 0;
    const char32_t committed_character = input->usParam;
    InputAtom normalized = *input;
    if (committed) { normalized.usParam = 0; normalized.usKeyState = 0; input = &normalized; }

    if (!gfTextInputMode || !gfEditingText || gpActive == 0 ||
        (input->usEvent != KEY_DOWN && input->usEvent != KEY_REPEAT) ||
        input->usParam == VK_ESCAPE || input->usParam == VK_RETURN || input->usParam == VK_TAB ||
        (input->usKeyState & ALT_DOWN) != 0 ||
        ((input->usKeyState & CTRL_DOWN) != 0 && input->usParam != VK_DELETE &&
         input->usParam != VK_RIGHT && input->usParam != VK_LEFT) ||
        (input->usParam > 0x6f && input->usParam < 0x7c)) {
        return 0;
    }

    std::size_t selection_end = gubEndHilite;
    switch (input->usParam) {
    case 0x25: /* Left */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            if (gubCursorPos != 0) {
                gubCursorPos = wiz8::text::previous_grapheme(gpActive->szString, gubCursorPos);
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                    gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
            }
            gubEndHilite = gubCursorPos;
            return 1;
        }
        if (gfHiliteMode) {
            gubCursorPos = gubStartHilite;
            gfHiliteMode = false;
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
            return 1;
        }
        if (gubCursorPos != 0) {
            gubCursorPos = wiz8::text::previous_grapheme(gpActive->szString, gubCursorPos);
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
        }
        return 1;

    case 0x27: /* Right */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            if (gubCursorPos < gpActive->szString.size()) {
                { std::size_t next = gubCursorPos;
                next = wiz8::text::next_grapheme(gpActive->szString, next);
                gubCursorPos = next; }
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                    gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
            }
            gubEndHilite = gubCursorPos;
            return 1;
        }
        if (gfHiliteMode) {
            gubCursorPos = selection_end;
            gfHiliteMode = false;
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
            return 1;
        }
        if (gubCursorPos < gpActive->szString.size()) {
            { std::size_t next = gubCursorPos;
                next = wiz8::text::next_grapheme(gpActive->szString, next);
                gubCursorPos = next; }
            gubParkingPos = CalculateCursorPos(
                TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
        }
        return 1;

    case 0x23: /* End */
        if ((input->usKeyState & SHIFT_DOWN) != 0) {
            if (!gfHiliteMode) {
                gfHiliteMode = true;
                gubStartHilite = gubCursorPos;
            }
            gubCursorPos = gpActive->szString.size();
            gubEndHilite = gubCursorPos;
        } else {
            gfHiliteMode = false;
            gubCursorPos = gpActive->szString.size();
        }
        gubParkingPos = CalculateCursorPos(
            TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
            gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
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
        gubParkingPos = CalculateCursorPos(TextFieldRegion(gpActive).x2 -
                                               TextFieldRegion(gpActive).x1 - 10,
                                           0, gpActive->szString, &gsCursorX, &guiVisibleCount);
        return 1;

    case 0x2e: /* Delete */
        if ((input->usKeyState & CTRL_DOWN) != 0) {
            gpActive->szString.clear();

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
            if (gubCursorPos < gpActive->szString.size()) {
                std::size_t end = gubCursorPos;
                end = wiz8::text::next_grapheme(gpActive->szString, end);
                gpActive->szString.erase(gubCursorPos, end - gubCursorPos);
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
                gubCursorPos = wiz8::text::previous_grapheme(gpActive->szString, gubCursorPos);
                gpActive->szString.erase(gubCursorPos, old_position - gubCursorPos);
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10,
                    gubCursorPos, gpActive->szString, &gsCursorX, &guiVisibleCount);
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
        char32_t character = committed ? committed_character :
            TranslateKeyToCharacter(static_cast<unsigned short>(input->usParam), input->usKeyState);
        if (!committed && NativeTextInputEnabled()) return 1;
        if (character == 0)
            return 1;

        if (gfHiliteMode) {
            gfHiliteMode = false;
            if (gubStartHilite != gubEndHilite) {
                DeleteHighlightedText(gubStartHilite, gubEndHilite);
            }
        }

        unsigned short input_type = static_cast<unsigned short>(gpActive->usInputType);
        if (input_type > 0x0fff) {
            HandleExclusiveInput(character);
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
            AddChar(character);
            return 1;
        }
        if ((input_type & 2) != 0) {
            if (IsUppercaseCharacter(character) != 0) {
                if ((input_type & 0x20) != 0)
                    character = ToLowercaseCharacter(character);
                AddChar(character);
                return 1;
            }
            if (IsLowercaseCharacter(character) != 0) {
                if ((input_type & 0x10) != 0)
                    character = ToUppercaseCharacter(character);
                AddChar(character);
                return 1;
            }
        }
        if ((input_type & 8) != 0 &&
            IsPunctuationCharacter(character) != 0) {
            AddChar(character);
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
        if (IsUppercaseCharacter(character) == 0 && IsLowercaseCharacter(character) == 0 &&
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
            if (IsLowercaseCharacter(character) != 0) {
                AddChar(character);
                return;
            }
            if (IsUppercaseCharacter(character) == 0)
                return;
            AddChar(static_cast<unsigned short>(ToLowercaseCharacter(character)));
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
        if (gpActive->szString.size() <= 2) {
            AddChar(':');
            return;
        }
        { std::size_t next = gubCursorPos;
                next = wiz8::text::next_grapheme(gpActive->szString, next);
                gubCursorPos = next; }
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
void AddChar(char32_t character)
{
    std::string bytes;
    utf8::append(character, std::back_inserter(bytes));
    if (wiz8::text::to_utf16(gpActive->szString).size() +
        wiz8::text::to_utf16(bytes).size() > gpActive->max_code_units) return;
    gpActive->szString.insert(gubCursorPos, bytes);
    gubCursorPos += bytes.size();
    gubParkingPos = CalculateCursorPos(
        TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10, gubCursorPos,
        gpActive->szString, &gsCursorX, &guiVisibleCount);
}

static std::size_t FindTextInputMousePosition(TEXTINPUTNODE* field, std::size_t start,
                                             int mouse_offset)
{
    int previous_width = 0;
    for (auto position = start; position < field->szString.size();) {
        const auto end = wiz8::text::next_grapheme(field->szString, position);
        const auto width = pColors->usFont->textWidth(std::string_view(field->szString).substr(start, end - start));
        if (mouse_offset < (width + previous_width) / 2) return position;
        previous_width = width;
        position = end;
    }
    return field->szString.size();
}

// FUNCTION: WIZ8 0x005D4CB0
void MouseMovedInTextRegionCallback(TEXTINPUTNODE* field, int reason)
{
    if (!field) return;
    if (IsModalOpen())
        return;

    if (field->fBlockMouseCallbacks) return;
    int field_index = field->ubID;

    if ((reason & BUTTON_REASON_GAIN_MOUSE) != 0)
        SetTargetCursor(GetTextInputCursor());
    if ((reason & BUTTON_REASON_LOST_MOUSE) != 0)
        SetTargetCursor(W8_CURSOR_NONE);

    if ((g_text_field_input_buttons & SDL_BUTTON_LMASK) == 0 || gpActive == 0 ||
        (reason & BUTTON_REASON_MOVE) == 0) {
        return;
    }

    if (field->ubID != gpActive->ubID) {
        RenderInactiveTextFieldNode(gpActive);
        for (TEXTINPUTNODE* field = gpTextInputHead.get(); field != 0; field = field->next.get()) {
            if (field->ubID == field_index) {
                gubMouseDownPos = 0;
                gubCursorPos = 0;
                gpActive = field;
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10, 0,
                    field->szString.c_str(), &gsCursorX, &guiVisibleCount);
                gfHiliteMode = false;
                gubStartHilite = 0;
                gubEndHilite = 0;
                break;
            }
        }
    }

    TEXTINPUTNODE* current_field = gpActive;
    if (current_field->fUserField)
        return;

    std::size_t position = gubParkingPos;
    int mouse_offset = g_text_field_input_x - TextFieldRegion(current_field).x1;
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
        TextFieldRegion(current_field).x2 - TextFieldRegion(current_field).x1 - 10,
        position, current_field->szString.c_str(), &gsCursorX, &guiVisibleCount);
}

// FUNCTION: WIZ8 0x005D4F10
void MouseClickedInTextRegionCallback(TEXTINPUTNODE* field, int reason)
{
    if (!field) return;
    int field_index = field->ubID;
    if (IsModalOpen())
        return;

    if (field->fBlockMouseCallbacks) return;

    if ((reason & BUTTON_REASON_LBUTTON_DOUBLECLICK) != 0) {
        if (gpActive != 0)
            SelectAllText();
        return;
    }

    if ((reason & BUTTON_REASON_LBUTTON_DWN) != 0) {
        TEXTINPUTNODE* field = gpActive;
        if (field == 0 || field_index != field->ubID)
            return;

        std::size_t position = gubParkingPos;
        if (field->fUserField) {
            position = 0;
        } else {
            int mouse_offset = g_text_field_input_x - TextFieldRegion(field).x1;
            position = FindTextInputMousePosition(field, gubParkingPos, mouse_offset);
        }
        SetTextInputCursor(position);
        gubMouseDownPos = gubCursorPos;
        return;
    }

    if ((reason & BUTTON_REASON_LBUTTON_UP) == 0)
        return;
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
        if (candidate->fUserField) {
            gfHiliteMode = false;
            gfEditingText = false;
            SetNativeTextInputEnabled(false);
            if (candidate->InputCallback != 0)
                candidate->InputCallback(candidate->ubID, 1);
            return;
        }
        gubStartHilite = 0;
        gubEndHilite = candidate->szString.size();
        gubCursorPos = candidate->szString.size();
        SetTextInputCursor(gubCursorPos);
        gubCursorPos = candidate->szString.size();
        gfHiliteMode = true;
        gfEditingText = true;
        SetNativeTextInputEnabled(true);
        return;
    }

    std::size_t position = gubParkingPos;
    if ((g_text_field_input_buttons & SDL_BUTTON_LMASK) != 0) {
        if (gpActive->fUserField) {
            position = 0;
        } else {
            TEXTINPUTNODE* field = gpActive;
            int mouse_offset = g_text_field_input_x - TextFieldRegion(field).x1;
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
    int left = TextFieldRegion(field).x1;
    int top = TextFieldRegion(field).y1;
    int right = TextFieldRegion(field).x2;
    int bottom = TextFieldRegion(field).y2;

    if (style->fBevelling) {
        ColorFillVideoSurfaceArea(GetFrameBufferSurface(), left, top, right, bottom, style->usDarkerColor);
        ColorFillVideoSurfaceArea(GetFrameBufferSurface(), left + 1, top + 1, right, bottom,
                                  style->usBrighterColor);
    }

    unsigned short colour;
    if (field->fEnabled == 0 && !style->fUseDisabledAutoShade)
        colour = style->usDisabledTextFieldColor;
    else
        colour = style->usTextFieldColor;
    if (field->fUseInactiveTextFieldColor && field != gpActive)
        colour = style->usInactiveTextFieldColor;

    ColorFillVideoSurfaceArea(GetFrameBufferSurface(), left, top, right, bottom, colour);
    InvalidateRegion(left, top, right, bottom, 0);
}

// FUNCTION: WIZ8 0x005D5390
void RenderActiveTextField(void)
{
    TEXTINPUTNODE* field = gpActive;
    if (field == 0 || field->fUserField)
        return;

    if (gfLeftButtonState != 0) {
        if (static_cast<int>(gusMouseXPos) < TextFieldRegion(field).x1) {
            if (gubCursorPos != 0) {
                gubCursorPos = wiz8::text::previous_grapheme(gpActive->szString, gubCursorPos);
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10,
                    gubCursorPos, field->szString.c_str(), &gsCursorX, &guiVisibleCount);
            }
            if (gfHiliteMode)
                gubStartHilite = gubVisibleStart;
        } else if (TextFieldRegion(field).x2 < static_cast<int>(gusMouseXPos)) {
            if (gubCursorPos < field->szString.size()) {
                { std::size_t next = gubCursorPos;
                next = wiz8::text::next_grapheme(gpActive->szString, next);
                gubCursorPos = next; }
                gubParkingPos = CalculateCursorPos(
                    TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10,
                    gubCursorPos, field->szString.c_str(), &gsCursorX, &guiVisibleCount);
            }
            if (gfHiliteMode)
                gubEndHilite = static_cast<unsigned char>(guiVisibleCount + gubVisibleStart);
        }
    }

    {

        FontRenderScope font_scope;
        SetFont(*pColors->usFont);
        unsigned short font_height = pColors->usFont->height();
        unsigned int vertical_offset =
            (TextFieldRegion(field).y2 - TextFieldRegion(field).y1 - font_height) / 2;
        RenderBackgroundField(field);

        const char* visible = field->szString.c_str() + gubParkingPos;

        bool has_selection = gfHiliteMode && gubStartHilite != gubEndHilite;
        std::size_t selection_first = gubEndHilite;
        std::size_t selection_last = gubStartHilite;
        if (gubStartHilite < gubEndHilite) {
            selection_first = gubStartHilite;
            selection_last = gubEndHilite;
        }

        for (size_t index = 0; index < guiVisibleCount && visible[index];) {
            short prefix = pColors->usFont->prefixWidth(const_cast<char*>(visible), index);
            unsigned char background;
            if (has_selection &&
                selection_first <= gubParkingPos + index &&
                gubParkingPos + index < selection_last) {
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
            DrawMonoFontText(TextFieldRegion(field).x1 + prefix + 3,
                             TextFieldRegion(field).y1 + vertical_offset, glyph);
            index = end;
        }

        if (gfEditingText && !field->fUserField && gfLeftButtonState == 0 &&
            w8_get_ticks() % 1000 < 500) {
            int left = TextFieldRegion(field).x1 + gsCursorX;
            int top = TextFieldRegion(field).y1 + vertical_offset;
            ColorFillVideoSurfaceArea(GetFrameBufferSurface(), left, top, left + 1, top + font_height,
                                      pColors->usCursorColor);
        }

    }
}

// FUNCTION: WIZ8 0x005D5770
void RenderInactiveTextFieldNode(TEXTINPUTNODE* field)
{
    if (field == 0 || field->fUserField)
        return;

    const bool disabled = field->fEnabled == 0 && pColors->fUseDisabledAutoShade;
    {

        FontRenderScope font_scope;
        SetFont(*pColors->usFont);
        unsigned char shadow;
        if (disabled) {
            SetFontForeground(pColors->ubDisabledForeColor);
            shadow = pColors->ubDisabledShadowColor;
        } else {
            SetFontForeground(pColors->ubForeColor);
            shadow = pColors->ubShadowColor;
        }
        SetFontShadow(shadow);
        unsigned short font_height = pColors->usFont->height();
        unsigned int vertical_offset =
            (TextFieldRegion(field).y2 - TextFieldRegion(field).y1 - font_height) / 2;
        SetFontBackground(0);
        RenderBackgroundField(field);

        const char* visible = field->szString.c_str();
        for (size_t index = 0; visible[index];) {
            short prefix = pColors->usFont->prefixWidth(field->szString.c_str(), index);
            if (TextFieldRegion(field).x2 - TextFieldRegion(field).x1 - 10 < prefix + 3) break;
            std::size_t end = index;
            wiz8::text::next(visible, end);
            const std::string glyph(visible + index, end - index);
            DrawMonoFontText(TextFieldRegion(field).x1 + prefix + 3,
                             TextFieldRegion(field).y1 + vertical_offset, glyph);
            index = end;
        }

    }

    if (disabled) {
        SDL_Rect rectangle = {
            TextFieldRegion(field).x1,
            TextFieldRegion(field).y1,
            TextFieldRegion(field).x2 - TextFieldRegion(field).x1,
            TextFieldRegion(field).y2 - TextFieldRegion(field).y1,
        };
        const auto& surface = *GetFrameBufferSurface().cpu().surface;
        Blt16BPPBufferShadowRect(static_cast<unsigned short*>(surface.pixels), surface.pitch, &rectangle);
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
std::size_t CalculateCursorPos(int width, std::size_t cursor, std::string_view text,
                              int* cursor_width, size_t* visible_count)
{
    cursor = std::min(cursor, text.size());
    auto start = std::min(gubVisibleStart, cursor);
    while (start < cursor && pColors->usFont->textWidth(text.substr(start, cursor - start)) > width)
        start = wiz8::text::next_grapheme(text, start);
    auto end = start;
    while (end < text.size()) {
        auto next = wiz8::text::next_grapheme(text, end);
        if (pColors->usFont->textWidth(text.substr(start, next - start)) > width) break;
        end = next;
    }
    gubVisibleStart = start;
    *cursor_width = pColors->usFont->textWidth(text.substr(start, cursor - start)) + 2;
    *visible_count = end - start; // Byte extent; the renderer decodes glyphs.
    return start;
}

std::size_t GetTextInputByteCursor(void) { return gubCursorPos; }

// FUNCTION: WIZ8 0x005D5BF0
void SetTextInputCursor(std::size_t cursor)
{
    if (gpActive) {
        cursor = std::min(cursor, gpActive->szString.size());
        std::size_t boundary = 0;
        while (boundary < cursor) {
            const auto next = wiz8::text::next_grapheme(gpActive->szString, boundary);
            if (next > cursor) break;
            boundary = next;
        }
        cursor = boundary;
    }
    gubCursorPos = cursor;
    if (gpActive != 0) {
        gubParkingPos = CalculateCursorPos(
            TextFieldRegion(gpActive).x2 - TextFieldRegion(gpActive).x1 - 10, cursor,
            gpActive->szString, &gsCursorX, &guiVisibleCount);
    }
}

// FUNCTION: WIZ8 0x005D5C40
void SelectAllText(void)
{
    TEXTINPUTNODE* field = gpActive;
    std::size_t position = gubParkingPos;
    if (field->fUserField) {
        position = 0;
    } else {
        int mouse_offset = g_text_field_input_x - TextFieldRegion(field).x1;
        position = FindTextInputMousePosition(field, position, mouse_offset);
    }

    if (field->szString[position] == ' ')
        return;
    std::size_t first = 0;
    if (position != 0) {
        std::size_t scan = position;
        const char* character = field->szString.c_str() + position;
        do {
            if (*character == ' ') {
                first = scan + 1;
                break;
            }
            --scan;
            --character;
        } while (scan != 0);
    }

    std::size_t last = field->szString.size();
    for (std::size_t scan = position + 1; scan < field->szString.size(); ++scan) {
        if (field->szString[scan] == ' ') {
            last = scan;
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

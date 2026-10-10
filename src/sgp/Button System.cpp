#include <string_view>
#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
/***********************************************************************************************
	Button System.c

	Rewritten mostly by Kris Morness
***********************************************************************************************/

#include "Types.h"
#include <stdio.h>
#include <memory.h>
#include "input.h"
#include <array>
#include <memory>
#include <vector>
#include <algorithm>
#include "english.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "soundman.h"
#include "Button System.h"
#include "Font.h"
#include "wiz8/application.h"
#include "line.h"
#include <stdarg.h>
#include <array>
#include <memory>
#include "Video2.h"

// The following should be moved from here
#define COLOR_RED 162                  // From "lighting.h" in JA2
#define COLOR_BLUE 203
#define COLOR_YELLOW 144
#define COLOR_GREEN 184
#define COLOR_LTGREY 134
#define COLOR_BROWN 80
#define COLOR_PURPLE 160
#define COLOR_ORANGE 76
#define COLOR_WHITE 208
#define COLOR_BLACK 72
// this doesn't exactly belong here either... (From "Font Control.h" in JA2)
#define FONT_MCOLOR_BLACK 0
#define COLOR_DKGREY 136



// GLOBAL: WIZ8 0x006e1940
CHAR8 str[128];

//Kris:  December 2, 1997
//Special internal debugging utilities that will ensure that you don't attempt to delete
//an already deleted button, or it's images, etc.  It will also ensure that you don't create
//the same button that already exists.
//TO REMOVE ALL DEBUG FUNCTIONALITY:  simply comment out BUTTONSYSTEM_DEBUGGING definition

static std::vector<std::weak_ptr<GUI_BUTTON>> active_buttons;
static INT32 next_button_id = 1;

#ifdef BUTTONSYSTEM_DEBUGGING
BOOLEAN gfIgnoreShutdownAssertions;
//Called immediately before assigning the button to the button list.
void AssertFailIfIdenticalButtonAttributesFound(GUI_BUTTON* b)
{
    for (const auto& weak : active_buttons) {
        auto retained = weak.lock();
        GUI_BUTTON* c = retained.get();
        if (!c)
            continue;
        if (c->UserData[3] == 0xffffffff)
            continue;
        if (b->Area.PriorityLevel != c->Area.PriorityLevel)
            continue;
        if (b->Area.RegionTopLeftX != c->Area.RegionTopLeftX)
            continue;
        if (b->Area.RegionTopLeftY != c->Area.RegionTopLeftY)
            continue;
        if (b->Area.RegionBottomRightX != c->Area.RegionBottomRightX)
            continue;
        if (b->Area.RegionBottomRightY != c->Area.RegionBottomRightY)
            continue;
        if (b->ClickCallback != c->ClickCallback)
            continue;
        if (b->MoveCallback != c->MoveCallback)
            continue;
        if (b->XLoc != c->XLoc)
            continue;
        if (b->YLoc != c->YLoc)
            continue;
        //if we get this far, it is reasonably safe to assume that the newly created
        //button already exists.  Placing a break point on the following assert will
        //allow the coder to easily isolate the case!
        sprintf(
            str,
            "Attempting to create a button that has already been created (existing buttonID %d).",
            c->IDNum);
        AssertMsg(0, str);
    }
}
#endif

//Kris:
//These are the variables used for the anchoring of a particular button.
//When you click on a button, it get's anchored, until you release the mouse button.
//When you move around, you don't want to select other buttons, even when you release
//it.  This follows the Windows 95 convention.
// GLOBAL: WIZ8 0x006e1198
GUI_BUTTON* gpAnchoredButton;
// GLOBAL: WIZ8 0x006E10C0
GUI_BUTTON* gpPrevAnchoredButton;
// GLOBAL: WIZ8 0x006e1880
BOOLEAN gfAnchoredState;
// GLOBAL: WIZ8 0x006e1190
INT8 gbDisabledButtonStyle;
void DrawHatchOnButton(GUI_BUTTON* b);
void DrawShadeOnButton(GUI_BUTTON* b);
void DrawDefaultOnButton(GUI_BUTTON* b);

// GLOBAL: WIZ8 0x005ff824
BOOLEAN gfRenderHilights = TRUE;

// GLOBAL: WIZ8 0x006e1bc0
// GLOBAL: WIZ8 0x006e1194

// GLOBAL: WIZ8 0x005ff828
UINT32 ButtonDestBuffer = BACKBUFFER;
// GLOBAL: WIZ8 0x005ff82c
UINT32 ButtonDestPitch = 640 * 2;
// GLOBAL: WIZ8 0x005ff830
UINT32 ButtonDestBPP = 16;

// GLOBAL: WIZ8 0x006e1240
// Buttons are owned by their C++ clients. Rendering and mouse dispatch only
// retain weak references, so a button does not require a global numeric slot.

size_t RegisteredButtonCount()
{
    return std::count_if(active_buttons.begin(), active_buttons.end(),
                         [](const auto& button) { return !button.expired(); });
}

_GUI_BUTTON::~_GUI_BUTTON()
{
    if (Area.uiFlags & MSYS_REGION_EXISTS) MSYS_RemoveRegion(&Area);
    if (gpAnchoredButton == this) gpAnchoredButton = nullptr;
    if (gpPrevAnchoredButton == this) gpPrevAnchoredButton = nullptr;
}

// GLOBAL: WIZ8 0x00650ea4

static GenericButtonImage default_generic_image;

// flag to state we wish to render buttons on the one after the next pass through render buttons
// GLOBAL: WIZ8 0x00650EA8
BOOLEAN fPausedMarkButtonsDirtyFlag = FALSE;
// GLOBAL: WIZ8 0x00650EA9
BOOLEAN fDisableHelpTextRestoreFlag = FALSE;

// GLOBAL: WIZ8 0x00650EAA
// Mouse callbacks hold a strong reference until the current dispatch returns.
static std::shared_ptr<GUI_BUTTON> ButtonFromRegion(MOUSE_REGION* region)
{
    if (!region) return {};
    return std::static_pointer_cast<GUI_BUTTON>(region->dispatch_owner.lock());
}

extern MOUSE_REGION* MSYS_PrevRegion;
extern MOUSE_REGION* MSYS_CurrRegion;



// Load and validate all state frames before publishing the graphics resource.
ButtonImage ButtonGraphics::Load(std::string_view filename, INT32 grayed, INT32 off_normal,
                                 INT32 off_hilite, INT32 on_normal, INT32 on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (filename.empty() || std::all_of(frames.begin(), frames.end(),
                                      [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
    desc.ImageFile = filename;
    auto image = std::make_shared<ButtonGraphics>();
    image->ownedObject.reset(CreateVideoObject(&desc));
    if (!image->ownedObject)
        return {};

    image->MaxWidth = image->MaxHeight = 0;
    for (int index : frames) {
        if (index == BUTTON_NO_IMAGE)
            continue;
        if (index < 0 || static_cast<size_t>(index) >= image->object()->sprites.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid button frame %d in %s",
                         index, desc.ImageFile.c_str());
            return {};
        }
        const auto& frame = image->object()->sprites[index];
        image->MaxWidth = std::max(image->MaxWidth,
            static_cast<UINT32>(std::max(0, frame.usWidth + frame.sOffsetX)));
        image->MaxHeight = std::max(image->MaxHeight,
            static_cast<UINT32>(std::max(0, frame.usHeight + frame.sOffsetY)));
    }
    image->Grayed = grayed;
    image->OffNormal = off_normal;
    image->OffHilite = off_hilite;
    image->OnNormal = on_normal;
    image->OnHilite = on_hilite;
    return image;
}

//	DisableButton
//	Disables a button. The button remains in the system list, and can be
//	reactivated by calling EnableButton.
//	Diabled buttons will appear "grayed out" on the screen (unless the
//	graphics for such are not available).
// FUNCTION: WIZ8 0x0040c7e0
BOOLEAN DisableButton(const GUIButton& button)
{
    if (!button) return FALSE;
    bool was_enabled = (button->uiFlags & BUTTON_ENABLED) != 0;
    button->uiFlags &= ~BUTTON_ENABLED;
    button->uiFlags |= BUTTON_DIRTY;
    return was_enabled ? TRUE : FALSE;
}

namespace {
HVOBJECT LoadButtonVideoObject(const char* filename)
{
    VOBJECT_DESC desc{};
    desc.fCreateFlags = VOBJECT_CREATE_FROMFILE;
    desc.ImageFile = filename;
    return CreateVideoObject(&desc);
}
}

//	InitializeButtonImageManager
//	Initializes the button image sub-system. This function is called by
//	InitButtonSystem.
// FUNCTION: WIZ8 0x0040c840
BOOLEAN InitializeButtonImageManager(INT32 DefaultBuffer, INT32 DefaultPitch, INT32 DefaultBPP)
{
    UINT8 Pix;
    // Set up the default settings
    if (DefaultBuffer != BUTTON_USE_DEFAULT)
        ButtonDestBuffer = (UINT32)DefaultBuffer;
    else
        ButtonDestBuffer = FRAME_BUFFER;

    if (DefaultPitch != BUTTON_USE_DEFAULT)
        ButtonDestPitch = (UINT32)DefaultPitch;
    else
        ButtonDestPitch = 640 * 2;

    if (DefaultBPP != BUTTON_USE_DEFAULT)
        ButtonDestBPP = (UINT32)DefaultBPP;
    else
        ButtonDestBPP = 16;

    default_generic_image.reset();
    auto image = std::make_shared<GenericButtonImages>();

    const std::array<const char*, 6> filenames{
        DEFAULT_GENERIC_BUTTON_OFF, DEFAULT_GENERIC_BUTTON_ON, nullptr,
        DEFAULT_GENERIC_BUTTON_OFF_HI, DEFAULT_GENERIC_BUTTON_ON_HI, nullptr};
    for (size_t index = 0; index < filenames.size(); ++index) {
        if (!filenames[index])
            continue;
        image->frames[index].reset(LoadButtonVideoObject(filenames[index]));
        // Default highlights are optional even when their files are missing.
        if (!image->frames[index] && index < 2) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create VOBJECT for %s", std::string(filenames[index]).c_str());
            return false;
        }
    }

    Pix = 0;
    if (!GetETRLEPixelValue(&Pix, image->frames[0].get(), 8, 0, 0)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Couldn't get generic button's background pixel value");
        return false;
    }

    image->fill_color = image->frames[0]->p16BPPPalette[Pix];

    default_generic_image = std::move(image);
    return TRUE;
}





//	LoadGenericButtonImages
//	Loads the image files required for displaying a generic button.
// FUNCTION: WIZ8 0x0040cb70
GenericButtonImage LoadGenericButtonImages(std::string_view GrayName, std::string_view OffNormName, std::string_view OffHiliteName,
                              std::string_view OnNormName, std::string_view OnHiliteName, std::string_view BkGrndName,
                              INT16 Index, INT16 OffsetX, INT16 OffsetY)
{
    if (OffNormName.empty() || OnNormName.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadGenericButtonImages: Missing normal-state filenames");
        return {};
    }
    auto image = std::make_shared<GenericButtonImages>();
    const std::array<std::string_view, 6> filenames{
        OffNormName, OnNormName, GrayName, OffHiliteName, OnHiliteName, BkGrndName};
    std::array<std::unique_ptr<SGPVObject>, 6> loaded;
    for (size_t index = 0; index < filenames.size(); ++index) {
        if (filenames[index].empty())
            continue;
        loaded[index].reset(LoadButtonVideoObject(std::string(filenames[index]).c_str()));
        if (!loaded[index]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "LoadGenericButtonImages: Couldn't create VOBJECT for %s", std::string(filenames[index]).c_str());
            return {};
        }
    }
    UINT8 pixel = 0;
    if (!GetETRLEPixelValue(&pixel, loaded[0].get(), 8, 0, 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadGenericButtonImages: Couldn't get background pixel value");
        return {};
    }
    image->fill_color = loaded[0]->p16BPPPalette[pixel];
    image->frames = std::move(loaded);
    image->background_frame = Index;
    image->offset_x = OffsetX;
    image->offset_y = OffsetY;
    return image;
}

//	ShutdownButtonImageManager
//	Cleans up, and shuts down the button image manager sub-system.
//	This function is called by ShutdownButtonSystem.
// FUNCTION: WIZ8 0x0040ce60
void ShutdownButtonImageManager(void)
{
    default_generic_image.reset();
}

//	InitButtonSystem
//	Initializes the GUI button system for use. Must be called before using
//	any other button functions.
// FUNCTION: WIZ8 0x0040cf60
BOOLEAN InitButtonSystem(void)
{
    ShutdownButtonSystem();

#ifdef BUTTONSYSTEM_DEBUGGING
    gfIgnoreShutdownAssertions = FALSE;
#endif


    // Initialize the button image manager sub-system
    if (InitializeButtonImageManager(-1, -1, -1) == FALSE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "Failed button image manager init\n");
        return (FALSE);
    }


    return (TRUE);
}

// Stop receiving events before the surrounding screens release their buttons.
// FUNCTION: WIZ8 0x0040cf90
void ShutdownButtonSystem(void)
{
    for (auto& weak : active_buttons) {
        if (auto button = weak.lock()) {
            if (button->Area.uiFlags & MSYS_REGION_EXISTS)
                MSYS_RemoveRegion(&button->Area);
        }
    }
    active_buttons.clear();
    ShutdownButtonImageManager();
}

//	ResizeButton
//	Changes the size of a generic button.
//	QuickButtons cannot be resized, therefore this function ignores the
//	call if a QuickButton is given.
// FUNCTION: WIZ8 0x0040d210
void ResizeButton(const GUIButton& button, INT16 w, INT16 h)
{
    GUI_BUTTON* b;
    INT32 xloc, yloc;

    // if button size is too small, adjust it.
    if (w < 4)
        w = 4;
    if (h < 3)
        h = 3;

    b = button.get();
    if (!b) return;

    // If this is a QuickButton, ignore this call
    if ((b->uiFlags & BUTTON_TYPES) == BUTTON_QUICK)
        return;

    // Get current button screen location
    xloc = b->XLoc;
    yloc = b->YLoc;

    // Set the new MOUSE_REGION area values to reflect change in size.
    b->Area.RegionTopLeftX = (UINT16)xloc;
    b->Area.RegionTopLeftY = (UINT16)yloc;
    b->Area.RegionBottomRightX = (UINT16)(xloc + w);
    b->Area.RegionBottomRightY = (UINT16)(yloc + h);
    b->uiFlags |= BUTTON_DIRTY;
}

//	SetButtonPosition
//	Sets the position of a button on the screen. The position is relative
//	to the top left corner of the button.
// FUNCTION: WIZ8 0x0040d2b0
void SetButtonPosition(const GUIButton& button, INT16 x, INT16 y)
{
    GUI_BUTTON* b;
    INT32 xloc, yloc, w, h;

    b = button.get();
    if (!b) return;

    // Get new screen position
    xloc = (INT16)x;
    yloc = (INT16)y;
    // Compute current width and height of this button
    w = b->Area.RegionBottomRightX - b->Area.RegionTopLeftX;
    h = b->Area.RegionBottomRightY - b->Area.RegionTopLeftY;

    // Set button to new location
    b->XLoc = x;
    b->YLoc = y;
    // Set the buttons MOUSE_REGION to appropriate area
    b->Area.RegionTopLeftX = (UINT16)xloc;
    b->Area.RegionTopLeftY = (UINT16)yloc;
    b->Area.RegionBottomRightX = (UINT16)(xloc + w);
    b->Area.RegionBottomRightY = (UINT16)(yloc + h);
    b->uiFlags |= BUTTON_DIRTY;
}

//Creates a generic button with text on it.
// FUNCTION: WIZ8 0x0040d350
GUIButton CreateTextButton(std::string_view string, UINT32 uiFont, INT16 sForeColor, INT16 sShadowColor,
                       GenericButtonImage GenImg, INT16 xloc, INT16 yloc, INT16 w, INT16 h, INT32 Type,
                       INT16 Priority, GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback)
{
    GUI_BUTTON* b;
    INT32 BType, x;

    if (xloc < 0 || yloc < 0) {
        sprintf(str, "Attempting to CreateTextButton with invalid position of %d,%d", xloc, yloc);
        AssertMsg(0, str);
    }
    // if button size is too small, adjust it.
    if (w < 4)
        w = 4;
    if (h < 3)
        h = 3;

    // Strip off any extraneous bits from button type
    BType = Type & (BUTTON_TYPE_MASK | BUTTON_NEWTOGGLE);

    // Allocate memory for a GUI_BUTTON structure
    auto owner = std::make_shared<GUI_BUTTON>();
    b = owner.get();

    // Allocate memory for the button's text string...
    b->string = string;

    // Init the button structure variables
    b->uiFlags = BUTTON_DIRTY;
    b->uiOldFlags = 0;
    b->IDNum = next_button_id++;
    b->XLoc = xloc;
    b->YLoc = yloc;

    b->generic_image = GenImg ? std::move(GenImg) : default_generic_image;

    for (x = 0; x < 4; x++)
        b->UserData[x] = 0;
    b->Group = -1;
    b->bDefaultStatus = DEFAULT_STATUS_NONE;
    b->bDisabledStyle = DISABLED_STYLE_DEFAULT;
    //Init string
    b->usFont = (UINT16)uiFont;
    b->fMultiColor = FALSE;
    b->sForeColor = sForeColor;
    b->sWrappedWidth = -1;
    b->sShadowColor = sShadowColor;
    b->sForeColorDown = -1;
    b->sShadowColorDown = -1;
    b->sForeColorHilited = -1;
    b->sShadowColorHilited = -1;
    b->bJustification = BUTTON_TEXT_CENTER;
    b->bTextXOffset = -1;
    b->bTextYOffset = -1;
    b->bTextXSubOffSet = 0;
    b->bTextYSubOffSet = 0;
    b->fShiftText = TRUE;

    // Set the button click callback function (if any)
    if (ClickCallback != BUTTON_NO_CALLBACK) {
        b->ClickCallback = ClickCallback;
        BType |= BUTTON_CLICK_CALLBACK;
    } else
        b->ClickCallback = BUTTON_NO_CALLBACK;

    // Set the button's mouse movement callback function (if any)
    if (MoveCallback != BUTTON_NO_CALLBACK) {
        b->MoveCallback = MoveCallback;
        BType |= BUTTON_MOVE_CALLBACK;
    } else
        b->MoveCallback = BUTTON_NO_CALLBACK;

    // Define a MOUSE_REGION for this button
    MSYS_DefineRegion(&b->Area, (UINT16)xloc, (UINT16)yloc, (UINT16)(xloc + w), (UINT16)(yloc + h),
                      (INT8)Priority,
                      (MOUSE_CALLBACK)QuickButtonCallbackMMove,
                      (MOUSE_CALLBACK)QuickButtonCallbackMButn);

    // Link the MOUSE_REGION to this button

    // Set the flags for this button
    b->uiFlags |= (BUTTON_ENABLED | BType | BUTTON_GENERIC);

    // Add this button to the button list
#ifdef BUTTONSYSTEM_DEBUGGING
    AssertFailIfIdenticalButtonAttributesFound(b);
#endif
    b->Area.dispatch_owner = owner;
    active_buttons.push_back(owner);


    // Return the owned button.
    return owner;
}

//	QuickCreateButton
//	Creates a QuickButton. QuickButtons only have graphics associated with
//	them. They cannot be re-sized, nor can the graphic be changed.
// FUNCTION: WIZ8 0x0040d5e0
GUIButton QuickCreateButton(ButtonImage Image, INT16 xloc, INT16 yloc, INT32 Type, INT16 Priority,
                        GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback)
{
    GUI_BUTTON* b;
    INT32 BType, x;

    if (xloc < 0 || yloc < 0) {
        sprintf(str, "Attempting to QuickCreateButton with invalid position of %d,%d", xloc, yloc);
        AssertMsg(0, str);
    }

    // Strip off any extraneous bits from button type
    BType = Type & (BUTTON_TYPE_MASK | BUTTON_NEWTOGGLE);

    // Require a graphics resource before registering the button.
    if (!Image || Image->object() == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "QuickCreateButton: Missing button graphics");
        return {};
    }

    // Allocate memory for a GUI_BUTTON structure
    auto owner = std::make_shared<GUI_BUTTON>();
    b = owner.get();

    // Set the values for this buttn
    b->uiFlags = BUTTON_DIRTY;
    b->uiOldFlags = 0;

    // Set someflags if of s certain type....
    if (Type & BUTTON_NEWTOGGLE) {
        b->uiFlags |= BUTTON_NEWTOGGLE;
    }

    // shadow style
    b->bDefaultStatus = DEFAULT_STATUS_NONE;
    b->bDisabledStyle = DISABLED_STYLE_DEFAULT;

    b->Group = -1;
    //Init string
    b->string.clear();
    b->usFont = 0;
    b->fMultiColor = FALSE;
    b->sForeColor = 0;
    b->sWrappedWidth = -1;
    b->sShadowColor = -1;
    b->sForeColorDown = -1;
    b->sShadowColorDown = -1;
    b->sForeColorHilited = -1;
    b->sShadowColorHilited = -1;
    b->bJustification = BUTTON_TEXT_CENTER;
    b->bTextXOffset = -1;
    b->bTextYOffset = -1;
    b->bTextXSubOffSet = 0;
    b->bTextYSubOffSet = 0;
    b->fShiftText = TRUE;
    //Init quickbutton
    b->IDNum = next_button_id++;
    b->quick_image = Image;
    for (x = 0; x < 4; x++)
        b->UserData[x] = 0;

    b->XLoc = xloc;
    b->YLoc = yloc;

    b->ubToggleButtonOldState = 0;
    b->ubToggleButtonActivated = FALSE;

    // Set the button click callback function (if any)
    if (ClickCallback != BUTTON_NO_CALLBACK) {
        b->ClickCallback = ClickCallback;
        BType |= BUTTON_CLICK_CALLBACK;
    } else
        b->ClickCallback = BUTTON_NO_CALLBACK;

    // Set the button's mouse movement callback function (if any)
    if (MoveCallback != BUTTON_NO_CALLBACK) {
        b->MoveCallback = MoveCallback;
        BType |= BUTTON_MOVE_CALLBACK;
    } else
        b->MoveCallback = BUTTON_NO_CALLBACK;

    b->Area = {};
    // Define a MOUSE_REGION for this QuickButton
    MSYS_DefineRegion(&b->Area, (UINT16)xloc, (UINT16)yloc,
                      (UINT16)(xloc + (INT16)Image->MaxWidth),
                      (UINT16)(yloc + (INT16)Image->MaxHeight), (INT8)Priority,
                      (MOUSE_CALLBACK)QuickButtonCallbackMMove,
                      (MOUSE_CALLBACK)QuickButtonCallbackMButn);

    // Link the MOUSE_REGION with this QuickButton

    // Set the flags for this button
    b->uiFlags |= BUTTON_ENABLED | BType | BUTTON_QUICK;

    // Add this QuickButton to the button list
#ifdef BUTTONSYSTEM_DEBUGGING
    AssertFailIfIdenticalButtonAttributesFound(b);
#endif
    b->Area.dispatch_owner = owner;
    active_buttons.push_back(owner);


    // Return the owned button.
    return owner;
}

//New functions
// FUNCTION: WIZ8 0x0040d850
void SpecifyButtonText(const GUIButton& button, std::string_view string)
{
    if (!button) return;
    button->string = string;
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040d8c0
void SpecifyButtonMultiColorFont(const GUIButton& button, BOOLEAN enabled)
{
    if (!button) return;
    button->fMultiColor = enabled;
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040d8e0
void SpecifyButtonTextOffsets(const GUIButton& button, INT8 x, INT8 y, BOOLEAN shift)
{
    if (!button) return;
    button->bTextXOffset = x;
    button->bTextYOffset = y;
    button->fShiftText = shift;
}

//	SetButtonFastHelpText
//	Set the text that will be displayed as the FastHelp
// FUNCTION: WIZ8 0x0040d910
void SetButtonFastHelpText(const GUIButton& button, std::string_view text)
{
    if (button) SetRegionFastHelpText(&button->Area, text);
}

//	QuickButtonCallbackMMove
//	Dispatches all button callbacks for mouse movement. This function gets
//	called by the Mouse System. *DO NOT CALL DIRECTLY*
// FUNCTION: WIZ8 0x0040d940
void QuickButtonCallbackMMove(MOUSE_REGION* reg, INT32 reason)
{
    auto keep_alive = ButtonFromRegion(reg);
    GUI_BUTTON* b = keep_alive.get();
    if (!b) return;

    if (b->uiFlags & BUTTON_ENABLED &&
        reason & (MSYS_CALLBACK_REASON_LOST_MOUSE | MSYS_CALLBACK_REASON_GAIN_MOUSE)) {
        b->uiFlags |= BUTTON_DIRTY;
    }

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if ((b->uiFlags & BUTTON_NEWTOGGLE)) {
        if (reason & MSYS_CALLBACK_REASON_LOST_MOUSE) {
            if (b->ubToggleButtonActivated) {
                b->ubToggleButtonActivated = FALSE;

                if (!b->ubToggleButtonOldState) {
                    b->uiFlags &= (~BUTTON_CLICKED_ON);
                } else {
                    b->uiFlags |= BUTTON_CLICKED_ON;
                }
            }
        }
    }

    // If this button is enabled and there is a callback function associated with it,
    // call the callback function.
    if ((b->uiFlags & BUTTON_ENABLED) && (b->uiFlags & BUTTON_MOVE_CALLBACK)) {
                (b->MoveCallback)(b, reason);
    }
}

//	QuickButtonCallbackMButn
//	Dispatches all button callbacks for button presses. This function is
//	called by the Mouse System. *DO NOT CALL DIRECTLY*
// FUNCTION: WIZ8 0x0040da50
void QuickButtonCallbackMButn(MOUSE_REGION* reg, INT32 reason)
{
    auto keep_alive = ButtonFromRegion(reg);
    GUI_BUTTON* b = keep_alive.get();
    if (!b) return;
    bool StateBefore;

    StateBefore = (b->uiFlags & BUTTON_CLICKED_ON) ? (TRUE) : (FALSE);

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if (b->uiFlags & BUTTON_NEWTOGGLE && b->uiFlags & BUTTON_ENABLED) {
        if (reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) {
            if (!b->ubToggleButtonActivated) {
                if (!(b->uiFlags & BUTTON_CLICKED_ON)) {
                    b->ubToggleButtonOldState = FALSE;
                    b->uiFlags |= BUTTON_CLICKED_ON;
                } else {
                    b->ubToggleButtonOldState = TRUE;
                    b->uiFlags &= (~BUTTON_CLICKED_ON);
                }
                b->ubToggleButtonActivated = TRUE;
            }
        } else if (reason & MSYS_CALLBACK_REASON_LBUTTON_UP) {
            b->ubToggleButtonActivated = FALSE;
        }
    }

    //Kris:
    //Set the anchored button incase the user moves mouse off region while still holding
    //down the button, but only if the button is up.  In Win95, buttons that are already
    //down, and anchored never change state, unless you release the mouse in the button area.

    if (b->MoveCallback == DEFAULT_MOVE_CALLBACK && b->uiFlags & BUTTON_ENABLED) {
        if (reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) {
            gpAnchoredButton = b;
            gfAnchoredState = StateBefore;
            b->uiFlags |= BUTTON_CLICKED_ON;
        } else if (reason & MSYS_CALLBACK_REASON_LBUTTON_UP && b->uiFlags & BUTTON_NO_TOGGLE) {
            b->uiFlags &= (~BUTTON_CLICKED_ON);
        }
    } else if (b->uiFlags & BUTTON_CHECKBOX) {
        if (reason &
            MSYS_CALLBACK_REASON_LBUTTON_DWN) { //the check box button gets anchored, though it doesn't actually use the anchoring move callback.
            //The effect is different, we don't want to toggle the button state, but we do want to anchor this
            //button so that we don't effect any other buttons while we move the mouse around in anchor mode.
            gpAnchoredButton = b;
            gfAnchoredState = StateBefore;

        } else if (reason & MSYS_CALLBACK_REASON_LBUTTON_UP) {
            b->uiFlags ^=
                BUTTON_CLICKED_ON; //toggle the checkbox state upon release inside button area.
        }
    }

    // If this button is disabled, and no callbacks allowed when disabled
    // callback
    if (!(b->uiFlags & BUTTON_ENABLED) && !(b->uiFlags & BUTTON_ALLOW_DISABLED_CALLBACK))
        return;

    // Button not enabled but allowed to use callback, then do that!
    if (!(b->uiFlags & BUTTON_ENABLED) && (b->uiFlags & BUTTON_ALLOW_DISABLED_CALLBACK)) {
        if (b->uiFlags & BUTTON_CLICK_CALLBACK) {
            (b->ClickCallback)(b, reason | BUTTON_DISABLED_CALLBACK);
        }
        return;
    }

    // If there is a callback function with this button, call it
    if (b->uiFlags & BUTTON_CLICK_CALLBACK) {
        //Kris:  January 6, 1998
        //Added these checks to avoid a case where it was possible to process a leftbuttonup message when
        //the button wasn't anchored, and should have been.
        if (!(reason & MSYS_CALLBACK_REASON_LBUTTON_UP) ||
            b->MoveCallback != DEFAULT_MOVE_CALLBACK || gpPrevAnchoredButton == b)
            (b->ClickCallback)(b, reason);
    } else if ((reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) &&
               !(b->uiFlags & BUTTON_IGNORE_CLICKS)) {
        // Otherwise, do default action with this button.
        b->uiFlags ^= BUTTON_CLICKED_ON;
    }

}

// FUNCTION: WIZ8 0x0040dc80
void RenderButtons(void)
{
    BOOLEAN fOldButtonDown, fOldEnabled;
    GUI_BUTTON* b;

    SaveFontSettings();
    for (auto it = active_buttons.begin(); it != active_buttons.end();) {
        auto retained = it->lock();
        if (!retained) { it = active_buttons.erase(it); continue; }
        ++it;
        b = retained.get();
        if (b && b->Area.uiFlags & MSYS_REGION_ENABLED) {
            // Check for buttonchanged status
            fOldButtonDown = (BOOLEAN)(b->uiFlags & BUTTON_CLICKED_ON);

            if (fOldButtonDown != (b->uiOldFlags & BUTTON_CLICKED_ON)) {
                //Something is different, set dirty!
                b->uiFlags |= BUTTON_DIRTY;
            }

            // Check for button dirty flags
            fOldEnabled = (BOOLEAN)(b->uiFlags & BUTTON_ENABLED);

            if (fOldEnabled != (b->uiOldFlags & BUTTON_ENABLED)) {
                //Something is different, set dirty!
                b->uiFlags |= BUTTON_DIRTY;
            }

            // If we ABSOLUTELY want to render every frame....
            if (b->uiFlags & BUTTON_SAVEBACKGROUND) {
                b->uiFlags |= BUTTON_DIRTY;
            }

            // Set old flags
            b->uiOldFlags = b->uiFlags;

            if (b->uiFlags & BUTTON_FORCE_UNDIRTY) {
                b->uiFlags &= ~(BUTTON_DIRTY);
                b->uiFlags &= ~(BUTTON_FORCE_UNDIRTY);
            }

            // Check if we need to update!
            if (b->uiFlags & BUTTON_DIRTY) {
                // Turn off dirty flag
                b->uiFlags &= (~BUTTON_DIRTY);
                DrawButtonFromPtr(b);
            }
        }
    }

    // check if we want to render 1 frame later?
    if ((fPausedMarkButtonsDirtyFlag == TRUE) && (fDisableHelpTextRestoreFlag == FALSE)) {
        fPausedMarkButtonsDirtyFlag = FALSE;
        MarkButtonsDirty();
    }

    RestoreFontSettings();
}

//	MarkButtonsDirty
// FUNCTION: WIZ8 0x0040de90
void MarkButtonsDirty(void)
{
    for (const auto& weak : active_buttons)
        if (auto button = weak.lock()) button->uiFlags |= BUTTON_DIRTY;
}

// PauseMarkButtonsDirty

//	DrawButton
//	Draws a single button on the screen.
// FUNCTION: WIZ8 0x0040deb0
BOOLEAN DrawButton(const GUIButton& button)
{
    if (!button) return FALSE;
    if (!button->string.empty()) SaveFontSettings();
    if (button->Area.uiFlags & MSYS_REGION_ENABLED) DrawButtonFromPtr(button.get());
    if (!button->string.empty()) RestoreFontSettings();
    return TRUE;
}

//	DrawButtonFromPtr
//	Given a pointer to a GUI_BUTTON structure, draws the button on the
//	screen.
void DrawButtonFromPtr(GUI_BUTTON* b)
{
    Assert(b);
    // Draw the appropriate button according to button type
    gbDisabledButtonStyle = DISABLED_STYLE_NONE;
    switch (b->uiFlags & BUTTON_TYPES) {
    case BUTTON_QUICK:
        DrawQuickButton(b);
        break;
    case BUTTON_GENERIC:
        DrawGenericButton(b);
        break;
    case BUTTON_HOT_SPOT:
        if (b->uiFlags & BUTTON_NO_TOGGLE)
            b->uiFlags &= (~BUTTON_CLICKED_ON);
        return; //hotspots don't have text, but if you want to, change this to a break!
    case BUTTON_CHECKBOX:
        DrawQuickButton(b);
        break;
    }
    //If button has text, draw it now
    if (!b->string.empty())
        DrawTextOnButton(b);
    //If the button is disabled, and a style has been calculated, then
    //draw the style last.
    switch (gbDisabledButtonStyle) {
    case DISABLED_STYLE_HATCHED:
        DrawHatchOnButton(b);
        break;
    case DISABLED_STYLE_SHADED:
        DrawShadeOnButton(b);
        break;
    }
    if (b->bDefaultStatus) {
        DrawDefaultOnButton(b);
    }
}

//	DrawQuickButton
//	Draws a QuickButton type button on the screen.
// FUNCTION: WIZ8 0x0040e060
void DrawQuickButton(GUI_BUTTON* b)
{
    if (!b->quick_image) return;
    const auto& image = *b->quick_image;
    const bool checkbox = (b->uiFlags & BUTTON_CHECKBOX) != 0;
    const bool pressed = (b->uiFlags & BUTTON_CLICKED_ON) != 0;
    INT32 frame = 0;
    if (b->uiFlags & BUTTON_ENABLED) {
        const bool highlighted = (b->Area.uiFlags & MSYS_MOUSE_IN_AREA) && gfRenderHilights &&
                                 (!checkbox || gfLeftButtonState);
        const auto normal = pressed ? image.OnNormal : image.OffNormal;
        const auto hilite = pressed ? image.OnHilite : image.OffHilite;
        if (highlighted && hilite != BUTTON_NO_IMAGE) frame = hilite;
        else if (normal != BUTTON_NO_IMAGE) frame = normal;
    } else if (image.Grayed != BUTTON_NO_IMAGE) {
        frame = image.Grayed;
    } else {
        frame = checkbox ? (pressed ? image.OnHilite : image.OffHilite) : image.OffNormal;
        switch (b->bDisabledStyle) {
        case DISABLED_STYLE_DEFAULT:
            gbDisabledButtonStyle = !checkbox && !b->string.empty()
                ? DISABLED_STYLE_SHADED : DISABLED_STYLE_HATCHED;
            break;
        case DISABLED_STYLE_HATCHED:
        case DISABLED_STYLE_SHADED:
            gbDisabledButtonStyle = b->bDisabledStyle;
            break;
        }
    }
    if (frame != BUTTON_NO_IMAGE)
        BltVideoObject(ButtonDestBuffer, image.object(), static_cast<UINT16>(frame),
                       b->XLoc, b->YLoc, VO_BLT_SRCTRANSPARENCY, nullptr);
}

void DrawHatchOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    SDL_Rect ClipRect{b->Area.RegionTopLeftX, b->Area.RegionTopLeftY,
                            b->Area.RegionBottomRightX - 1 - b->Area.RegionTopLeftX,
                            b->Area.RegionBottomRightY - 1 - b->Area.RegionTopLeftY};
    pDestBuf = LockVideoSurface(ButtonDestBuffer, &uiDestPitchBYTES);
    Blt16BPPBufferHatchRect((UINT16*)pDestBuf, uiDestPitchBYTES, &ClipRect);
    UnLockVideoSurface(ButtonDestBuffer);
}

void DrawShadeOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    SDL_Rect ClipRect{b->Area.RegionTopLeftX, b->Area.RegionTopLeftY,
                            b->Area.RegionBottomRightX - 1 - b->Area.RegionTopLeftX,
                            b->Area.RegionBottomRightY - 1 - b->Area.RegionTopLeftY};
    pDestBuf = LockVideoSurface(ButtonDestBuffer, &uiDestPitchBYTES);
    Blt16BPPBufferShadowRect((UINT16*)pDestBuf, uiDestPitchBYTES, &ClipRect);
    UnLockVideoSurface(ButtonDestBuffer);
}

// FUNCTION: WIZ8 0x0040e170
void DrawDefaultOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    pDestBuf = LockVideoSurface(ButtonDestBuffer, &uiDestPitchBYTES);
    SetClippingRegionAndImageWidth(uiDestPitchBYTES, 0, 0, 640, 480);
    if (b->bDefaultStatus == DEFAULT_STATUS_DARKBORDER ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) {
        //left (one thick)
        LineDraw(TRUE, b->Area.RegionTopLeftX - 1, b->Area.RegionTopLeftY - 1,
                 b->Area.RegionTopLeftX - 1, b->Area.RegionBottomRightY + 1, 0, (char*)pDestBuf);
        //top (one thick)
        LineDraw(TRUE, b->Area.RegionTopLeftX - 1, b->Area.RegionTopLeftY - 1,
                 b->Area.RegionBottomRightX + 1, b->Area.RegionTopLeftY - 1, 0, (char*)pDestBuf);
        //right (two thick)
        LineDraw(TRUE, b->Area.RegionBottomRightX, b->Area.RegionTopLeftY - 1,
                 b->Area.RegionBottomRightX, b->Area.RegionBottomRightY + 1, 0, (char*)pDestBuf);
        LineDraw(TRUE, b->Area.RegionBottomRightX + 1, b->Area.RegionTopLeftY - 1,
                 b->Area.RegionBottomRightX + 1, b->Area.RegionBottomRightY + 1, 0,
                 (char*)pDestBuf);
        //bottom (two thick)
        LineDraw(TRUE, b->Area.RegionTopLeftX - 1, b->Area.RegionBottomRightY,
                 b->Area.RegionBottomRightX + 1, b->Area.RegionBottomRightY, 0, (char*)pDestBuf);
        LineDraw(TRUE, b->Area.RegionTopLeftX - 1, b->Area.RegionBottomRightY + 1,
                 b->Area.RegionBottomRightX + 1, b->Area.RegionBottomRightY + 1, 0,
                 (char*)pDestBuf);
    }
    if (b->bDefaultStatus == DEFAULT_STATUS_DOTTEDINTERIOR ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) { //Draw an internal dotted rectangle.
    }
    UnLockVideoSurface(ButtonDestBuffer);
}

//If a button has text attached to it, then it'll draw it last.
// FUNCTION: WIZ8 0x0040e5d0
void DrawTextOnButton(GUI_BUTTON* b)
{
    INT32 xp, yp, width, height, TextX, TextY;
    SDL_Rect NewClip, OldClip;

    // If this button actually has a string to print
    if (!b->string.empty()) {
        // Get the width and height of this button
        width = b->Area.RegionBottomRightX - b->Area.RegionTopLeftX;
        height = b->Area.RegionBottomRightY - b->Area.RegionTopLeftY;

        // Compute the viewable area on this button
        NewClip = {b->XLoc + 3, b->YLoc + 2, width - 6, height - 4};

        // Get the starting coordinates to print
        TextX = NewClip.x;
        TextY = NewClip.y;

        // Get the current clipping area and clip the viewable area to it
        GetClippingRect(&OldClip);
        if (!SDL_GetRectIntersection(&NewClip, &OldClip, &NewClip))
            return;

        // Set the font printing settings to the buttons viewable area
        SetFontDestBuffer(ButtonDestBuffer, NewClip.x, NewClip.y, (NewClip.x + NewClip.w),
                          (NewClip.y + NewClip.h), FALSE);

        // Compute the coordinates to center the text
        if (b->bTextYOffset == -1)
            yp = (((height)-GetFontHeight(b->usFont)) / 2) + TextY - 1;
        else
            yp = b->Area.RegionTopLeftY + b->bTextYOffset;
        if (b->bTextXOffset == -1) {
            switch (b->bJustification) {
            case BUTTON_TEXT_LEFT:
                xp = TextX + 3;
                break;
            case BUTTON_TEXT_RIGHT:
                xp = (NewClip.x + NewClip.w) - StringPixLength(b->string, b->usFont) - 3;
                break;
            case BUTTON_TEXT_CENTER:
            default:
                xp = (((width - 6) - StringPixLength(b->string, b->usFont)) / 2) + TextX;
                break;
            }
        } else
            xp = b->Area.RegionTopLeftX + b->bTextXOffset;

        // Set the printing font to the button text font
        SetFont(b->usFont);

        // print the text
        SetFontBackground(FONT_MCOLOR_BLACK);
        SetFontForeground((UINT8)b->sForeColor);
        if (b->sShadowColor != -1)
            SetFontShadow((UINT8)b->sShadowColor);
        //Override the colors if necessary.
        if (b->uiFlags & BUTTON_ENABLED && b->Area.uiFlags & MSYS_MOUSE_IN_AREA &&
            b->sForeColorHilited != -1) {
            SetFontForeground((UINT8)b->sForeColorHilited);
        } else if (b->uiFlags & BUTTON_CLICKED_ON && b->sForeColorDown != -1) {
            SetFontForeground((UINT8)b->sForeColorDown);
        }
        if (b->uiFlags & BUTTON_ENABLED && b->Area.uiFlags & MSYS_MOUSE_IN_AREA &&
            b->sShadowColorHilited != -1) {
            SetFontShadow((UINT8)b->sShadowColorHilited);
        } else if (b->uiFlags & BUTTON_CLICKED_ON && b->sShadowColorDown != -1) {
            SetFontShadow((UINT8)b->sShadowColorDown);
        }
        if (b->uiFlags & BUTTON_CLICKED_ON &&
            b->fShiftText) { // Was the button clicked on? if so, move the text slightly for the illusion
            // that the text moved into the screen.
            xp++;
            yp++;
        }
        if (b->fMultiColor)
            DrawFontText(xp, yp, b->string);
        else
            DrawMonoFontText(xp, yp, b->string);
        // Restore the old text printing settings
    }
}

//	DrawGenericButton
//	This function is called by the DrawIconicButton and DrawTextButton
//	routines to draw the borders and background of the buttons.
// FUNCTION: WIZ8 0x0040e890
void DrawGenericButton(GUI_BUTTON* b)
{
    INT32 NumChunksWide, NumChunksHigh, cx, cy, width, height, hremain, wremain;
    INT32 q, ImgNum, ox, oy;
    INT32 iBorderHeight, iBorderWidth;
    HVOBJECT BPic;
    UINT32 uiDestPitchBYTES;
    UINT8* pDestBuf;
    SDL_Rect ClipRect;
    SpriteFrame* pTrav;

    // Select the graphics to use depending on the current state of the button
    if (b->uiFlags & BUTTON_ENABLED) {
        if (!(b->uiFlags & BUTTON_ENABLED) && (b->generic_image->frames[2] == nullptr))
            BPic = b->generic_image->frames[0].get();
        else if (b->uiFlags & BUTTON_CLICKED_ON) {
            if ((b->Area.uiFlags & MSYS_MOUSE_IN_AREA) &&
                (b->generic_image->frames[4] != nullptr) && gfRenderHilights)
                BPic = b->generic_image->frames[4].get();
            else
                BPic = b->generic_image->frames[1].get();
        } else {
            if ((b->Area.uiFlags & MSYS_MOUSE_IN_AREA) &&
                (b->generic_image->frames[3] != nullptr) && gfRenderHilights)
                BPic = b->generic_image->frames[3].get();
            else
                BPic = b->generic_image->frames[0].get();
        }
    } else if (b->generic_image->frames[2])
        BPic = b->generic_image->frames[2].get();
    else {
        BPic = b->generic_image->frames[0].get();
        switch (b->bDisabledStyle) {
        case DISABLED_STYLE_DEFAULT:
            gbDisabledButtonStyle = !b->string.empty() ? DISABLED_STYLE_SHADED : DISABLED_STYLE_HATCHED;
            break;
        case DISABLED_STYLE_HATCHED:
        case DISABLED_STYLE_SHADED:
            gbDisabledButtonStyle = b->bDisabledStyle;
            break;
        }
    }

    iBorderWidth = 3;
    iBorderHeight = 2;
    pTrav = nullptr;

    // DB - Added this to support more flexible sizing of border images
    // The 3x2 size was a bit limiting. JA2 should default to the original
    // size, unchanged

    pTrav = &(BPic->sprites[0]);
    iBorderHeight = (INT32)pTrav->usHeight;
    iBorderWidth = (INT32)pTrav->usWidth;

    // Compute the number of button "chunks" needed to be blitted
    width = b->Area.RegionBottomRightX - b->Area.RegionTopLeftX;
    height = b->Area.RegionBottomRightY - b->Area.RegionTopLeftY;
    NumChunksWide = width / iBorderWidth;
    NumChunksHigh = height / iBorderHeight;
    hremain = height % iBorderHeight;
    wremain = width % iBorderWidth;

    cx = (b->XLoc + ((NumChunksWide - 1) * iBorderWidth) + wremain);
    cy = (b->YLoc + ((NumChunksHigh - 1) * iBorderHeight) + hremain);

    // Fill the button's area with the button's background color
    ColorFillVideoSurfaceArea(ButtonDestBuffer, b->Area.RegionTopLeftX, b->Area.RegionTopLeftY,
                              b->Area.RegionBottomRightX, b->Area.RegionBottomRightY,
                              b->generic_image->fill_color);

    // If there is a background image, fill the button's area with it
    if (b->generic_image->frames[5] != nullptr) {
        ox = oy = 0;
        // if the button was clicked on, adjust the background image so that we get
        // the illusion that it is sunk into the screen.
        if (b->uiFlags & BUTTON_CLICKED_ON)
            ox = oy = 1;

        // Fill the area with the image, tilling it if need be.
        ImageFillVideoSurfaceArea(
            ButtonDestBuffer, b->Area.RegionTopLeftX + ox, b->Area.RegionTopLeftY + oy,
            b->Area.RegionBottomRightX, b->Area.RegionBottomRightY,
            b->generic_image->frames[5].get(), b->generic_image->background_frame,
            b->generic_image->offset_x, b->generic_image->offset_y);
    }

    // Lock the dest buffer
    pDestBuf = LockVideoSurface(ButtonDestBuffer, &uiDestPitchBYTES);

    GetClippingRect(&ClipRect);

    // Draw the button's borders and corners (horizontally)
    for (q = 0; q < NumChunksWide; q++) {
        if (q == 0)
            ImgNum = 0;
        else
            ImgNum = 1;

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[(UINT16)ImgNum], BPic->pShadeCurrent,
                                   (INT32)(b->XLoc + (q * iBorderWidth)), (INT32)b->YLoc, &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        if (q == 0)
            ImgNum = 5;
        else
            ImgNum = 6;

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[(UINT16)ImgNum], BPic->pShadeCurrent,
                                   (INT32)(b->XLoc + (q * iBorderWidth)), cy, &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }
    // Blit the right side corners
    BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                               BPic->sprites[2], BPic->pShadeCurrent,
                               cx, (INT32)b->YLoc, &ClipRect, false,
                               SpriteDrawOp::Transparent);

    BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                               BPic->sprites[7], BPic->pShadeCurrent,
                               cx, cy, &ClipRect, false,
                               SpriteDrawOp::Transparent);
    // Draw the vertical members of the button's borders
    NumChunksHigh--;

    if (hremain != 0) {
        q = NumChunksHigh;
        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[3], BPic->pShadeCurrent,
                                   (INT32)b->XLoc, (INT32)(b->YLoc + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[4], BPic->pShadeCurrent,
                                   cx, (INT32)(b->YLoc + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    for (q = 1; q < NumChunksHigh; q++) {
        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[3], BPic->pShadeCurrent,
                                   (INT32)b->XLoc, (INT32)(b->YLoc + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic->sprites[4], BPic->pShadeCurrent,
                                   cx, (INT32)(b->YLoc + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    // Unlock buffer
    UnLockVideoSurface(ButtonDestBuffer);
}

// Dialog box code
//	Very preliminary stuff follows

typedef struct _CreateDlgInfo {
    INT32 iFlags; // Holds the creation flags

    INT32 iPosX; // Screen position of dialog box
    INT32 iPosY;
    INT32 iWidth; // Dimensions of dialog box (if needed)
    INT32 iHeight;

    INT32 iAreaWidth;   // Dimensions of area the dialog box will be
    INT32 iAreaHeight;  // placed (for auto-sizing and auto-placement
    INT32 iAreaOffsetX; // only)
    INT32 iAreaOffsetY;

    UINT16* zDlgText;  // Text to be displayed (if any)
    INT32 iTextFont;   // Font to be used for text (if any)
    UINT16 usTextCols; // Font colors (for mono fonts only)

    INT32 iTextAreaX; // Area in dialog box where text is to be
    INT32 iTextAreaY; // put (for non-auto placed text)
    INT32 iTextAreaWidth;
    INT32 iTextAreaHeight;

    HVOBJECT hBackImg;   // Background pic for dialog box (if any)
    INT32 iBackImgIndex; // Sub-image index to use for image
    INT32 iBackOffsetX;  // Offset on dialog box where to put image
    INT32 iBackOffsetY;

    HVOBJECT hIconImg; // Icon image pic and index.
    INT32 iIconImgIndex;
    INT32 iIconPosX;
    INT32 iIconPosY;

    INT32 iBtnTypes;

    INT32 iOkPosX; // Ok button info
    INT32 iOkPosY;
    INT32 iOkWidth;
    INT32 iOkHeight;
    INT32 iOkImg;

    INT32 iCnclPosX; // Cancel button info
    INT32 iCnclPosY;
    INT32 iCnclWidth;
    INT32 iCnclHeight;
    INT32 iCnclImg;
} CreateDlgInfo;

#define DLG_RESTRICT_MOUSE 1
#define DLG_OK_BUTTON 2
#define DLG_CANCEL_BUTTON 4
#define DLG_AUTOSIZE 8
#define DLG_RECREATE 16
#define DLG_AUTOPOSITION 32
#define DLG_TEXT_IN_AREA 64
#define DLG_USE_BKGRND_IMAGE 128
#define DLG_USE_BORDERS 256
#define DLG_USE_BTN_HOTSPOTS 512
#define DLG_USE_MONO_FONTS 1024
#define DLG_IS_ACTIVE 2048

#define DLG_MANUAL_RENDER 0
#define DLG_START_RENDER 1
#define DLG_STOP_RENDER 2
#define DLG_AUTO_RENDER 3

#define DLG_GET_STATUS 0
#define DLG_WAIT_FOR_RESPONSE 1

#define DLG_STATUS_NONE 0
#define DLG_STATUS_OK 1
#define DLG_STATUS_CANCEL 2
#define DLG_STATUS_PENDING 3

#define DLG_CLEARALL 0
#define DLG_POSITION 1
#define DLG_AREA 2
#define DLG_TEXT 3
#define DLG_TEXTAREA 4
#define DLG_BACKPIC 5
#define DLG_ICON 6
#define DLG_OKBUTTON 7
#define DLG_CANCELBUTTON 8
#define DLG_OPTIONS 9
#define DLG_SIZE 10

// Added Oct17, 97 Carter - kind of mindless, but might as well have it
// FUNCTION: WIZ8 0x0040edc0
void MSYS_SetBtnUserData(const GUIButton& button, INT32 index, INT32 userdata)
{
    if (button && index >= 0 && index < 4) button->UserData[index] = userdata;
}

// FUNCTION: WIZ8 0x0040edf0
INT32 MSYS_GetBtnUserData(GUI_BUTTON* b, INT32 index)
{

    if (index < 0 || index > 3)
        return (0);

    return (b->UserData[index]);
}

//Generic Button Movement Callback to reset the mouse button if the mouse is no longer
//in the button region.
// FUNCTION: WIZ8 0x0040ee10
void BtnGenericMouseMoveButtonCallback(GUI_BUTTON* btn, INT32 reason)
{
    //If the button isn't the anchored button, then we don't want to modify the button state.
    if (btn != gpAnchoredButton)
        return;
    if (reason & MSYS_CALLBACK_REASON_LOST_MOUSE) {
        if (!gfAnchoredState) {
            btn->uiFlags &= (~BUTTON_CLICKED_ON);
        }
    } else if (reason & MSYS_CALLBACK_REASON_GAIN_MOUSE) {
        btn->uiFlags |= BUTTON_CLICKED_ON;
    }
}

//Kris:
//Yet new logical additions to the winbart library.
// FUNCTION: WIZ8 0x0040ee80
void HideButton(const GUIButton& button)
{
    if (!button) return;
    button->Area.uiFlags &= ~MSYS_REGION_ENABLED;
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040eea0
void ShowButton(const GUIButton& button)
{
    if (!button) return;
    button->Area.uiFlags |= MSYS_REGION_ENABLED;
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040eec0
BOOLEAN GetButtonArea(const GUIButton& button, SDL_Rect* rect)
{
    if (!button || !rect) return FALSE;
    *rect = {button->Area.RegionTopLeftX, button->Area.RegionTopLeftY,
             button->Area.RegionBottomRightX - button->Area.RegionTopLeftX,
             button->Area.RegionBottomRightY - button->Area.RegionTopLeftY};
    return TRUE;
}

// FUNCTION: WIZ8 0x0040ef00
INT32 GetButtonWidth(const GUIButton& button)
{
    return button ? button->Area.RegionBottomRightX - button->Area.RegionTopLeftX : -1;
}

// FUNCTION: WIZ8 0x0040ef20
INT32 GetButtonHeight(const GUIButton& button)
{
    return button ? button->Area.RegionBottomRightY - button->Area.RegionTopLeftY : -1;
}

// FUNCTION: WIZ8 0x0040ef40
INT32 GetButtonX(const GUIButton& button)
{
    return button ? button->Area.RegionTopLeftX : 0;
}

// FUNCTION: WIZ8 0x0040ef60
INT32 GetButtonY(const GUIButton& button)
{
    return button ? button->Area.RegionTopLeftY : 0;
}

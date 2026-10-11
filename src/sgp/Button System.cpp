#include <SDL3/SDL_rect.h>
#include <cstddef>
#include <fmt/format.h>
#include <SDL3/SDL_log.h>
#include <string_view>
#include <utility>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
/***********************************************************************************************
	Button System.c

	Rewritten mostly by Kris Morness
***********************************************************************************************/

#include <array>
#include <memory>
#include <vector>
#include <algorithm>

#include "Types.h"
#include "input.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "Button System.h"
#include "wiz8/regions.h"
#include "Font.h"
#include "line.h"
#include "vsurface.h"

#define FONT_MCOLOR_BLACK 0

//Kris:  December 2, 1997
//Special internal debugging utilities that will ensure that you don't attempt to delete
//an already deleted button, or it's images, etc.  It will also ensure that you don't create
//the same button that already exists.
//TO REMOVE ALL DEBUG FUNCTIONALITY:  simply comment out BUTTONSYSTEM_DEBUGGING definition

static std::vector<std::weak_ptr<GUI_BUTTON>> active_buttons;
static unsigned int button_region_set = 0;

namespace {
W8Region& ButtonRegion(const GUI_BUTTON* button)
{
    const auto index = button && button->region_index < g_regions.size()
                           ? button->region_index
                           : 0;
    return g_regions[index];
}

bool ButtonRegionIsLive(const GUI_BUTTON* button)
{
    return button && button->region_index != 0 && button->region_index < g_regions.size() &&
           g_regions[button->region_index].button == button;
}

bool ButtonRegionIsVisible(const GUI_BUTTON* button)
{
    return ButtonRegionIsLive(button) &&
           (ButtonRegion(button).flags & W8_REGION_INPUT_DISABLED) == 0;
}

bool ButtonIsHovered(const GUI_BUTTON* button)
{
    return ButtonRegionIsLive(button) &&
           g_hover_region_index == button->region_index;
}

int ButtonPriority(int priority)
{
    if (priority == BUTTON_PRIORITY_AUTO) return BUTTON_PRIORITY_BASE;
    return std::clamp(priority, BUTTON_PRIORITY_LOWEST, BUTTON_PRIORITY_HIGHEST);
}

void ReleaseButtonRegion(GUI_BUTTON* button)
{
    if (!ButtonRegionIsLive(button)) {
        if (button) button->region_index = 0;
        return;
    }
    const auto index = button->region_index;
    button->region_index = 0;
    RemoveRegionFromSet(button_region_set, index);
}

unsigned char DispatchButtonRegionEvent(const InputAtom* event, W8Region* region)
{
    auto* button = region->button;
    if (!button) return 0;
    switch (event->usEvent) {
    case MOUSE_POS:
        if (region->flags & W8_REGION_MOUSE_LEAVE)
            QuickButtonCallbackMMove(button, BUTTON_REASON_LOST_MOUSE);
        else if (region->flags & W8_REGION_MOUSE_ENTER)
            QuickButtonCallbackMMove(button, BUTTON_REASON_GAIN_MOUSE);
        else
            QuickButtonCallbackMMove(button, BUTTON_REASON_MOVE);
        return 1;
    case LEFT_BUTTON_DOWN: {
        INT32 reason = BUTTON_REASON_LBUTTON_DWN;
        if (button->last_left_click_released &&
            event->uiTimeStamp <= button->last_left_down_time + 400) {
            reason |= BUTTON_REASON_LBUTTON_DOUBLECLICK;
            button->double_click_atom_pending = true;
        } else {
            button->last_left_down_time = event->uiTimeStamp;
            button->double_click_atom_pending = false;
        }
        button->last_left_click_released = false;
        g_captured_region_index = button->region_index;
        QuickButtonCallbackMButn(button, reason);
        return 1;
    }
    case LEFT_BUTTON_UP:
        if (g_captured_region_index == button->region_index) g_captured_region_index = 0;
        if (!button->double_click_atom_pending && button->last_left_down_time != 0 &&
            event->uiTimeStamp <= button->last_left_down_time + 400) {
            button->last_left_click_released = true;
        }
        QuickButtonCallbackMButn(button, BUTTON_REASON_LBUTTON_UP);
        return 1;
    case LEFT_BUTTON_DBL_CLK:
        if (button->double_click_atom_pending) {
            button->double_click_atom_pending = false;
            return 1;
        }
        QuickButtonCallbackMButn(
            button, BUTTON_REASON_LBUTTON_DWN | BUTTON_REASON_LBUTTON_DOUBLECLICK);
        return 1;
    case LEFT_BUTTON_REPEAT:
        QuickButtonCallbackMButn(button, BUTTON_REASON_LBUTTON_REPEAT);
        return 1;
    case RIGHT_BUTTON_DOWN:
        g_captured_region_index = button->region_index;
        QuickButtonCallbackMButn(button, BUTTON_REASON_RBUTTON_DWN);
        return 1;
    case RIGHT_BUTTON_UP:
        if (g_captured_region_index == button->region_index) g_captured_region_index = 0;
        QuickButtonCallbackMButn(button, BUTTON_REASON_RBUTTON_UP);
        return 1;
    case RIGHT_BUTTON_REPEAT:
        QuickButtonCallbackMButn(button, BUTTON_REASON_RBUTTON_REPEAT);
        return 1;
    }
    return 0;
}
}

#ifdef BUTTONSYSTEM_DEBUGGING
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
        if (ButtonRegion(b).priority != ButtonRegion(c).priority)
            continue;
        if (ButtonRegion(b).x1 != ButtonRegion(c).x1)
            continue;
        if (ButtonRegion(b).y1 != ButtonRegion(c).y1)
            continue;
        if (ButtonRegion(b).x2 != ButtonRegion(c).x2)
            continue;
        if (ButtonRegion(b).y2 != ButtonRegion(c).y2)
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
        AssertMsg(0, "Attempting to create a button with identical attributes.");
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
void DrawHatchOnButton(GUI_BUTTON* b);
void DrawShadeOnButton(GUI_BUTTON* b);
void DrawDefaultOnButton(GUI_BUTTON* b);

// GLOBAL: WIZ8 0x005ff824
BOOLEAN gfRenderHilights = TRUE;

// GLOBAL: WIZ8 0x006e1bc0
// GLOBAL: WIZ8 0x006e1194

// GLOBAL: WIZ8 0x005ff828
SGPVSurface* ButtonDestBuffer = nullptr;

// GLOBAL: WIZ8 0x006e1240
// Buttons are owned by their C++ clients. Rendering and mouse dispatch only
// retain weak references, so a button does not require a global numeric slot.

size_t RegisteredButtonCount()
{
    return std::count_if(active_buttons.begin(), active_buttons.end(),
                         [](const auto& button) { return !button.expired(); });
}

_GUI_BUTTON::_GUI_BUTTON()
{
}

_GUI_BUTTON::~_GUI_BUTTON()
{
    ReleaseButtonRegion(this);
    if (gpAnchoredButton == this) gpAnchoredButton = nullptr;
    if (gpPrevAnchoredButton == this) gpPrevAnchoredButton = nullptr;
}

// GLOBAL: WIZ8 0x00650ea4

static GenericButtonImage default_generic_image;




// Load and validate all state frames before publishing the graphics resource.
ButtonImage ButtonGraphics::Load(std::string_view filename, INT32 grayed, INT32 off_normal,
                                 INT32 off_hilite, INT32 on_normal, INT32 on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (filename.empty() || std::all_of(frames.begin(), frames.end(),
                                      [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    auto image = std::make_shared<ButtonGraphics>();
    image->ownedObject = LoadVideoObject(filename);
    if (!image->ownedObject)
        return {};

    image->MaxWidth = image->MaxHeight = 0;
    for (int index : frames) {
        if (index == BUTTON_NO_IMAGE)
            continue;
        if (index < 0 || static_cast<size_t>(index) >= image->object().sprites.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid button frame %d in %s",
                         index, std::string(filename).c_str());
            return {};
        }
        const auto& frame = image->object().sprites[index];
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
GenericButtonImage LoadGenericGraphics(const std::array<std::string_view, 6>& filenames,
                                       INT16 background_frame, INT16 offset_x, INT16 offset_y,
                                       bool optional_highlights = false)
{
    if (filenames[0].empty() || filenames[1].empty()) return {};
    auto image = std::make_shared<GenericButtonImages>();
    for (size_t index = 0; index < filenames.size(); ++index) {
        if (filenames[index].empty()) continue;
        auto& object = image->frames[index];
        object = LoadVideoObject(filenames[index]);
        const bool valid = object && (index == 5
            ? background_frame >= 0 && static_cast<size_t>(background_frame) < object->sprites.size()
            : object->sprites.size() >= 9 && object->sprites[0].usWidth > 0 &&
              object->sprites[0].usHeight > 0);
        if (!valid) {
            if (optional_highlights && (index == 3 || index == 4)) {
                object.reset();
                continue;
            }
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid generic button graphics: %s",
                         std::string(filenames[index]).c_str());
            return {};
        }
    }
    const auto pixel = image->frames[0]->sprites[8].paletteIndexAt(0, 0);
    if (!pixel) return {};
    image->fill_color = image->frames[0]->palette[*pixel];
    image->background_frame = background_frame;
    image->offset_x = offset_x;
    image->offset_y = offset_y;
    return image;
}
}

// Default highlights may be absent. Explicitly requested custom states must load.
BOOLEAN InitializeButtonImageManager()
{
    ButtonDestBuffer = &GetFrameBufferSurface();
    default_generic_image = LoadGenericGraphics(
        {DEFAULT_GENERIC_BUTTON_OFF, DEFAULT_GENERIC_BUTTON_ON, {},
         DEFAULT_GENERIC_BUTTON_OFF_HI, DEFAULT_GENERIC_BUTTON_ON_HI, {}}, 0, 0, 0, true);
    return default_generic_image != nullptr;
}

// FUNCTION: WIZ8 0x0040cb70
GenericButtonImage LoadGenericButtonImages(std::string_view gray, std::string_view off_normal,
                                          std::string_view off_hilite, std::string_view on_normal,
                                          std::string_view on_hilite, std::string_view background,
                                          INT16 index, INT16 offset_x, INT16 offset_y)
{
    return LoadGenericGraphics({off_normal, on_normal, gray, off_hilite, on_hilite, background},
                                index, offset_x, offset_y);
}

//	ShutdownButtonImageManager
//	Cleans up, and shuts down the button image manager sub-system.
//	This function is called by ShutdownButtonSystem.
// FUNCTION: WIZ8 0x0040ce60
void ShutdownButtonImageManager(void)
{
    default_generic_image.reset();
}

unsigned int ButtonRegionSetIndex(void)
{
    if (button_region_set == 0) button_region_set = CreateRegionSet();
    RegionSetEnable(button_region_set);
    return button_region_set;
}

//	InitButtonSystem
//	Initializes the GUI button system for use. Must be called before using
//	any other button functions.
// FUNCTION: WIZ8 0x0040cf60
BOOLEAN InitButtonSystem(void)
{
    ShutdownButtonSystem();
    ButtonRegionSetIndex();

    // Initialize the button image manager sub-system
    if (InitializeButtonImageManager() == FALSE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "Failed button image manager init\n");
        return (FALSE);
    }


    return (TRUE);
}

// Stop receiving events before the surrounding screens release their buttons.
// FUNCTION: WIZ8 0x0040cf90
void ShutdownButtonSystem(void)
{
    for (auto& weak : active_buttons)
        if (auto button = weak.lock()) ReleaseButtonRegion(button.get());
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

    SetRegionBounds(b->region_index, static_cast<unsigned short>(xloc),
                    static_cast<unsigned short>(yloc),
                    static_cast<unsigned short>(xloc + w),
                    static_cast<unsigned short>(yloc + h));
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
    w = ButtonRegion(b).x2 - ButtonRegion(b).x1;
    h = ButtonRegion(b).y2 - ButtonRegion(b).y1;

    // Set button to new location
    b->XLoc = x;
    b->YLoc = y;
    // Set the buttons region to appropriate area
    SetRegionBounds(b->region_index, static_cast<unsigned short>(xloc),
                    static_cast<unsigned short>(yloc),
                    static_cast<unsigned short>(xloc + w),
                    static_cast<unsigned short>(yloc + h));
    b->uiFlags |= BUTTON_DIRTY;
}

namespace {
void RegisterButton(const GUIButton& button, INT16 x, INT16 y, INT16 width, INT16 height,
                    INT32 type, INT16 priority, GUI_CALLBACK move, GUI_CALLBACK click)
{
    button->XLoc = x;
    button->YLoc = y;
    button->uiFlags |= BUTTON_ENABLED | type;
    button->MoveCallback = move;
    button->ClickCallback = click;
    if (move) button->uiFlags |= BUTTON_MOVE_CALLBACK;
    if (click) button->uiFlags |= BUTTON_CLICK_CALLBACK;
    button->region_index =
        AddRegionToSet(ButtonRegionSetIndex(), ButtonPriority(priority));
    auto& region = g_regions[button->region_index];
    region.button = button.get();
    SetRegionBounds(button->region_index, static_cast<unsigned short>(x),
                    static_cast<unsigned short>(y), static_cast<unsigned short>(x + width),
                    static_cast<unsigned short>(y + height));
    SetRegionCallback(button->region_index, DispatchButtonRegionEvent, 0);
#ifdef BUTTONSYSTEM_DEBUGGING
    AssertFailIfIdenticalButtonAttributesFound(button.get());
#endif
    // Dialogs can be created and destroyed without RenderButtons being called.
    std::erase_if(active_buttons, [](const auto& entry) { return entry.expired(); });
    active_buttons.push_back(button);
}
}

// FUNCTION: WIZ8 0x0040d350
GUIButton CreateTextButton(std::string_view text, Font& font, INT16 fore_color, INT16 shadow_color,
                           GenericButtonImage image, INT16 x, INT16 y, INT16 width, INT16 height,
                           INT32 type, INT16 priority, GUI_CALLBACK move, GUI_CALLBACK click)
{
    if (x < 0 || y < 0) {
        const auto message = fmt::format("Attempting to CreateTextButton with invalid position of {},{}", x, y);
        AssertMsg(0, message.c_str());
    }
    auto button = std::make_shared<GUI_BUTTON>();
    button->string = text;
    button->usFont = &font;
    button->sForeColor = fore_color;
    button->sShadowColor = shadow_color;
    button->generic_image = image ? std::move(image) : default_generic_image;
    RegisterButton(button, x, y, std::max<INT16>(width, 4), std::max<INT16>(height, 3),
                   (type & (BUTTON_TYPE_MASK | BUTTON_NEWTOGGLE)) | BUTTON_GENERIC,
                   priority, move, click);
    return button;
}

// FUNCTION: WIZ8 0x0040d5e0
GUIButton QuickCreateButton(ButtonImage image, INT16 x, INT16 y, INT32 type, INT16 priority,
                            GUI_CALLBACK move, GUI_CALLBACK click)
{
    if (x < 0 || y < 0) {
        const auto message = fmt::format("Attempting to QuickCreateButton with invalid position of {},{}", x, y);
        AssertMsg(0, message.c_str());
    }
    if (!image) return {};
    auto button = std::make_shared<GUI_BUTTON>();
    button->quick_image = std::move(image);
    RegisterButton(button, x, y, static_cast<INT16>(button->quick_image->MaxWidth),
                   static_cast<INT16>(button->quick_image->MaxHeight),
                   type & (BUTTON_TYPE_MASK | BUTTON_NEWTOGGLE), priority, move, click);
    return button;
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

//	SetButtonHelpText
//	Set the text that will be displayed as help text
// FUNCTION: WIZ8 0x0040d910
void SetButtonHelpText(const GUIButton& button, std::string_view text)
{
    if (!button || !ButtonRegionIsLive(button.get())) return;
    auto& region = ButtonRegion(button.get());
    region.help_text = text;
    region.help_enabled = !text.empty();
}

//	QuickButtonCallbackMMove
//	Dispatches all button callbacks for mouse movement. This function gets
//	called by RegionManager. *DO NOT CALL DIRECTLY*
// FUNCTION: WIZ8 0x0040d940
void QuickButtonCallbackMMove(GUI_BUTTON* b, INT32 reason)
{
    if (!b) return;

    if (b->uiFlags & BUTTON_ENABLED &&
        reason & (BUTTON_REASON_LOST_MOUSE | BUTTON_REASON_GAIN_MOUSE)) {
        b->uiFlags |= BUTTON_DIRTY;
    }

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if ((b->uiFlags & BUTTON_NEWTOGGLE)) {
        if (reason & BUTTON_REASON_LOST_MOUSE) {
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
//	called by RegionManager. *DO NOT CALL DIRECTLY*
// FUNCTION: WIZ8 0x0040da50
void QuickButtonCallbackMButn(GUI_BUTTON* b, INT32 reason)
{
    if (!b) return;
    bool StateBefore;

    StateBefore = (b->uiFlags & BUTTON_CLICKED_ON) ? (TRUE) : (FALSE);

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if (b->uiFlags & BUTTON_NEWTOGGLE && b->uiFlags & BUTTON_ENABLED) {
        if (reason & BUTTON_REASON_LBUTTON_DWN) {
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
        } else if (reason & BUTTON_REASON_LBUTTON_UP) {
            b->ubToggleButtonActivated = FALSE;
        }
    }

    //Kris:
    //Set the anchored button incase the user moves mouse off region while still holding
    //down the button, but only if the button is up.  In Win95, buttons that are already
    //down, and anchored never change state, unless you release the mouse in the button area.

    if (b->MoveCallback == DEFAULT_MOVE_CALLBACK && b->uiFlags & BUTTON_ENABLED) {
        if (reason & BUTTON_REASON_LBUTTON_DWN) {
            gpAnchoredButton = b;
            gfAnchoredState = StateBefore;
            b->uiFlags |= BUTTON_CLICKED_ON;
        } else if (reason & BUTTON_REASON_LBUTTON_UP && b->uiFlags & BUTTON_NO_TOGGLE) {
            b->uiFlags &= (~BUTTON_CLICKED_ON);
        }
    } else if (b->uiFlags & BUTTON_CHECKBOX) {
        if (reason &
            BUTTON_REASON_LBUTTON_DWN) { //the check box button gets anchored, though it doesn't actually use the anchoring move callback.
            //The effect is different, we don't want to toggle the button state, but we do want to anchor this
            //button so that we don't effect any other buttons while we move the mouse around in anchor mode.
            gpAnchoredButton = b;
            gfAnchoredState = StateBefore;

        } else if (reason & BUTTON_REASON_LBUTTON_UP) {
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
        if (!(reason & BUTTON_REASON_LBUTTON_UP) ||
            b->MoveCallback != DEFAULT_MOVE_CALLBACK || gpPrevAnchoredButton == b)
            (b->ClickCallback)(b, reason);
    } else if ((reason & BUTTON_REASON_LBUTTON_DWN) &&
               !(b->uiFlags & BUTTON_IGNORE_CLICKS)) {
        // Otherwise, do default action with this button.
        b->uiFlags ^= BUTTON_CLICKED_ON;
    }

}

// FUNCTION: WIZ8 0x0040dc80
void RenderButtons(void)
{
    {
        FontRenderScope font_scope;
        std::erase_if(active_buttons, [](const auto& entry) { return entry.expired(); });
        for (const auto& weak : active_buttons) {
            auto button = weak.lock();
            if (!button || !(ButtonRegionIsVisible(button.get()))) continue;
            auto& flags = button->uiFlags;
            if (((flags ^ button->uiOldFlags) & (BUTTON_CLICKED_ON | BUTTON_ENABLED)) ||
                (flags & BUTTON_SAVEBACKGROUND))
                flags |= BUTTON_DIRTY;
            button->uiOldFlags = flags;
            if (flags & BUTTON_FORCE_UNDIRTY)
                flags &= ~(BUTTON_DIRTY | BUTTON_FORCE_UNDIRTY);
            if (flags & BUTTON_DIRTY) {
                flags &= ~BUTTON_DIRTY;
                DrawButtonFromPtr(button.get());
            }
        }
    }
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
    FontRenderScope font_scope;
    if (ButtonRegionIsVisible(button.get())) DrawButtonFromPtr(button.get());
    return TRUE;
}

static INT8 DisabledOverlayStyle(const GUI_BUTTON& button)
{
    if (button.bDisabledStyle == DISABLED_STYLE_DEFAULT)
        return !(button.uiFlags & BUTTON_CHECKBOX) && !button.string.empty()
            ? DISABLED_STYLE_SHADED : DISABLED_STYLE_HATCHED;
    return button.bDisabledStyle;
}

static INT8 DrawQuickButton(GUI_BUTTON* button);
static INT8 DrawGenericButton(GUI_BUTTON* button);

//	DrawButtonFromPtr
//	Given a pointer to a GUI_BUTTON structure, draws the button on the
//	screen.
void DrawButtonFromPtr(GUI_BUTTON* b)
{
    Assert(b);
    // Draw the appropriate button according to button type
    INT8 disabled_style = DISABLED_STYLE_NONE;
    switch (b->uiFlags & BUTTON_TYPES) {
    case BUTTON_QUICK:
        disabled_style = DrawQuickButton(b);
        break;
    case BUTTON_GENERIC:
        disabled_style = DrawGenericButton(b);
        break;
    case BUTTON_HOT_SPOT:
        if (b->uiFlags & BUTTON_NO_TOGGLE)
            b->uiFlags &= (~BUTTON_CLICKED_ON);
        return; //hotspots don't have text, but if you want to, change this to a break!
    case BUTTON_CHECKBOX:
        disabled_style = DrawQuickButton(b);
        break;
    }
    //If button has text, draw it now
    if (!b->string.empty())
        DrawTextOnButton(b);
    //If the button is disabled, and a style has been calculated, then
    //draw the style last.
    switch (disabled_style) {
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
static INT8 DrawQuickButton(GUI_BUTTON* b)
{
    INT8 disabled_style = DISABLED_STYLE_NONE;
    if (!b->quick_image) return disabled_style;
    const auto& image = *b->quick_image;
    const bool checkbox = (b->uiFlags & BUTTON_CHECKBOX) != 0;
    const bool pressed = (b->uiFlags & BUTTON_CLICKED_ON) != 0;
    INT32 frame = 0;
    if (b->uiFlags & BUTTON_ENABLED) {
        const bool highlighted = (ButtonIsHovered(b)) && gfRenderHilights &&
                                 (!checkbox || gfLeftButtonState);
        const auto normal = pressed ? image.OnNormal : image.OffNormal;
        const auto hilite = pressed ? image.OnHilite : image.OffHilite;
        if (highlighted && hilite != BUTTON_NO_IMAGE) frame = hilite;
        else if (normal != BUTTON_NO_IMAGE) frame = normal;
    } else if (image.Grayed != BUTTON_NO_IMAGE) {
        frame = image.Grayed;
    } else {
        frame = checkbox ? (pressed ? image.OnHilite : image.OffHilite) : image.OffNormal;
        disabled_style = DisabledOverlayStyle(*b);
    }
    if (frame != BUTTON_NO_IMAGE)
        BltVideoObject(*ButtonDestBuffer, image.object(), static_cast<UINT16>(frame),
                       b->XLoc, b->YLoc, VO_BLT_SRCTRANSPARENCY, nullptr);
    return disabled_style;
}

void DrawHatchOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    SDL_Rect ClipRect{ButtonRegion(b).x1, ButtonRegion(b).y1,
                            ButtonRegion(b).x2 - 1 - ButtonRegion(b).x1,
                            ButtonRegion(b).y2 - 1 - ButtonRegion(b).y1};
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<BYTE*>(destination_storage.pixels);
    Blt16BPPBufferHatchRect((UINT16*)pDestBuf, uiDestPitchBYTES, &ClipRect);
}

void DrawShadeOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    SDL_Rect ClipRect{ButtonRegion(b).x1, ButtonRegion(b).y1,
                            ButtonRegion(b).x2 - 1 - ButtonRegion(b).x1,
                            ButtonRegion(b).y2 - 1 - ButtonRegion(b).y1};
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<BYTE*>(destination_storage.pixels);
    Blt16BPPBufferShadowRect((UINT16*)pDestBuf, uiDestPitchBYTES, &ClipRect);
}

// FUNCTION: WIZ8 0x0040e170
void DrawDefaultOnButton(GUI_BUTTON* b)
{
    UINT8* pDestBuf;
    UINT32 uiDestPitchBYTES;
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<BYTE*>(destination_storage.pixels);
    SetClippingRegionAndImageWidth(uiDestPitchBYTES, 0, 0, 640, 480);
    if (b->bDefaultStatus == DEFAULT_STATUS_DARKBORDER ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) {
        //left (one thick)
        LineDraw(TRUE, ButtonRegion(b).x1 - 1, ButtonRegion(b).y1 - 1,
                 ButtonRegion(b).x1 - 1, ButtonRegion(b).y2 + 1, 0, (char*)pDestBuf);
        //top (one thick)
        LineDraw(TRUE, ButtonRegion(b).x1 - 1, ButtonRegion(b).y1 - 1,
                 ButtonRegion(b).x2 + 1, ButtonRegion(b).y1 - 1, 0, (char*)pDestBuf);
        //right (two thick)
        LineDraw(TRUE, ButtonRegion(b).x2, ButtonRegion(b).y1 - 1,
                 ButtonRegion(b).x2, ButtonRegion(b).y2 + 1, 0, (char*)pDestBuf);
        LineDraw(TRUE, ButtonRegion(b).x2 + 1, ButtonRegion(b).y1 - 1,
                 ButtonRegion(b).x2 + 1, ButtonRegion(b).y2 + 1, 0,
                 (char*)pDestBuf);
        //bottom (two thick)
        LineDraw(TRUE, ButtonRegion(b).x1 - 1, ButtonRegion(b).y2,
                 ButtonRegion(b).x2 + 1, ButtonRegion(b).y2, 0, (char*)pDestBuf);
        LineDraw(TRUE, ButtonRegion(b).x1 - 1, ButtonRegion(b).y2 + 1,
                 ButtonRegion(b).x2 + 1, ButtonRegion(b).y2 + 1, 0,
                 (char*)pDestBuf);
    }
    if (b->bDefaultStatus == DEFAULT_STATUS_DOTTEDINTERIOR ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) { //Draw an internal dotted rectangle.
    }
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
        width = ButtonRegion(b).x2 - ButtonRegion(b).x1;
        height = ButtonRegion(b).y2 - ButtonRegion(b).y1;

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
        SetFontDestBuffer(*ButtonDestBuffer, NewClip.x, NewClip.y, (NewClip.x + NewClip.w),
                          (NewClip.y + NewClip.h), FALSE);

        // Compute the coordinates to center the text
        if (b->bTextYOffset == -1)
            yp = (((height)-b->usFont->height()) / 2) + TextY - 1;
        else
            yp = ButtonRegion(b).y1 + b->bTextYOffset;
        if (b->bTextXOffset == -1) {
            switch (b->bJustification) {
            case BUTTON_TEXT_LEFT:
                xp = TextX + 3;
                break;
            case BUTTON_TEXT_RIGHT:
                xp = (NewClip.x + NewClip.w) - b->usFont->textWidth(b->string) - 3;
                break;
            case BUTTON_TEXT_CENTER:
            default:
                xp = (((width - 6) - b->usFont->textWidth(b->string)) / 2) + TextX;
                break;
            }
        } else
            xp = ButtonRegion(b).x1 + b->bTextXOffset;

        // Set the printing font to the button text font
        SetFont(*b->usFont);

        // print the text
        SetFontBackground(FONT_MCOLOR_BLACK);
        SetFontForeground((UINT8)b->sForeColor);
        if (b->sShadowColor != -1)
            SetFontShadow((UINT8)b->sShadowColor);
        //Override the colors if necessary.
        if (b->uiFlags & BUTTON_ENABLED && ButtonIsHovered(b) &&
            b->sForeColorHilited != -1) {
            SetFontForeground((UINT8)b->sForeColorHilited);
        } else if (b->uiFlags & BUTTON_CLICKED_ON && b->sForeColorDown != -1) {
            SetFontForeground((UINT8)b->sForeColorDown);
        }
        if (b->uiFlags & BUTTON_ENABLED && ButtonIsHovered(b) &&
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
static INT8 DrawGenericButton(GUI_BUTTON* b)
{
    INT8 disabled_style = DISABLED_STYLE_NONE;
    if (!b->generic_image) return disabled_style;
    INT32 NumChunksWide, NumChunksHigh, cx, cy, width, height, hremain, wremain;
    INT32 q, ImgNum, ox, oy;
    INT32 iBorderHeight, iBorderWidth;
    size_t picture_index = 0;
    UINT32 uiDestPitchBYTES;
    UINT8* pDestBuf;
    SDL_Rect ClipRect;
    SpriteFrame* pTrav;

    // Select the graphics to use depending on the current state of the button
    if (b->uiFlags & BUTTON_ENABLED) {
        if (b->uiFlags & BUTTON_CLICKED_ON) {
            if ((ButtonIsHovered(b)) &&
                (b->generic_image->frames[4] != nullptr) && gfRenderHilights)
                picture_index = 4;
            else
                picture_index = 1;
        } else {
            if ((ButtonIsHovered(b)) &&
                (b->generic_image->frames[3] != nullptr) && gfRenderHilights)
                picture_index = 3;
            else
                picture_index = 0;
        }
    } else if (b->generic_image->frames[2])
        picture_index = 2;
    else {
        picture_index = 0;
        disabled_style = DisabledOverlayStyle(*b);
    }

    auto& BPic = *b->generic_image->frames[picture_index];
    iBorderWidth = 3;
    iBorderHeight = 2;
    pTrav = nullptr;

    // DB - Added this to support more flexible sizing of border images
    // The 3x2 size was a bit limiting. JA2 should default to the original
    // size, unchanged

    pTrav = &(BPic.sprites[0]);
    iBorderHeight = (INT32)pTrav->usHeight;
    iBorderWidth = (INT32)pTrav->usWidth;

    // Compute the number of button "chunks" needed to be blitted
    width = ButtonRegion(b).x2 - ButtonRegion(b).x1;
    height = ButtonRegion(b).y2 - ButtonRegion(b).y1;
    NumChunksWide = width / iBorderWidth;
    NumChunksHigh = height / iBorderHeight;
    hremain = height % iBorderHeight;
    wremain = width % iBorderWidth;

    cx = (b->XLoc + ((NumChunksWide - 1) * iBorderWidth) + wremain);
    cy = (b->YLoc + ((NumChunksHigh - 1) * iBorderHeight) + hremain);

    // Fill the button's area with the button's background color
    ColorFillVideoSurfaceArea(*ButtonDestBuffer, ButtonRegion(b).x1, ButtonRegion(b).y1,
                              ButtonRegion(b).x2, ButtonRegion(b).y2,
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
            *ButtonDestBuffer, ButtonRegion(b).x1 + ox, ButtonRegion(b).y1 + oy,
            ButtonRegion(b).x2, ButtonRegion(b).y2,
            *b->generic_image->frames[5], b->generic_image->background_frame,
            b->generic_image->offset_x, b->generic_image->offset_y);
    }

    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<BYTE*>(destination_storage.pixels);

    GetClippingRect(&ClipRect);

    // Draw the button's borders and corners (horizontally)
    for (q = 0; q < NumChunksWide; q++) {
        if (q == 0)
            ImgNum = 0;
        else
            ImgNum = 1;

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[(UINT16)ImgNum], BPic.currentPalette().data(),
                                   (INT32)(b->XLoc + (q * iBorderWidth)), (INT32)b->YLoc, &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        if (q == 0)
            ImgNum = 5;
        else
            ImgNum = 6;

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[(UINT16)ImgNum], BPic.currentPalette().data(),
                                   (INT32)(b->XLoc + (q * iBorderWidth)), cy, &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }
    // Blit the right side corners
    BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                               BPic.sprites[2], BPic.currentPalette().data(),
                               cx, (INT32)b->YLoc, &ClipRect, false,
                               SpriteDrawOp::Transparent);

    BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                               BPic.sprites[7], BPic.currentPalette().data(),
                               cx, cy, &ClipRect, false,
                               SpriteDrawOp::Transparent);
    // Draw the vertical members of the button's borders
    NumChunksHigh--;

    if (hremain != 0) {
        q = NumChunksHigh;
        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[3], BPic.currentPalette().data(),
                                   (INT32)b->XLoc, (INT32)(b->YLoc + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[4], BPic.currentPalette().data(),
                                   cx, (INT32)(b->YLoc + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    for (q = 1; q < NumChunksHigh; q++) {
        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[3], BPic.currentPalette().data(),
                                   (INT32)b->XLoc, (INT32)(b->YLoc + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((UINT16*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[4], BPic.currentPalette().data(),
                                   cx, (INT32)(b->YLoc + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    return disabled_style;
}

// Added Oct17, 97 Carter - kind of mindless, but might as well have it
// FUNCTION: WIZ8 0x0040edc0
void SetButtonUserData(const GUIButton& button, INT32 index, INT32 userdata)
{
    if (button && index >= 0 && index < 4) button->UserData[index] = userdata;
}

// FUNCTION: WIZ8 0x0040edf0
INT32 GetButtonUserData(GUI_BUTTON* b, INT32 index)
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
    if (reason & BUTTON_REASON_LOST_MOUSE) {
        if (!gfAnchoredState) {
            btn->uiFlags &= (~BUTTON_CLICKED_ON);
        }
    } else if (reason & BUTTON_REASON_GAIN_MOUSE) {
        btn->uiFlags |= BUTTON_CLICKED_ON;
    }
}

//Kris:
//Yet new logical additions to the winbart library.
// FUNCTION: WIZ8 0x0040ee80
void HideButton(const GUIButton& button)
{
    if (!button) return;
    DisableRegionInput(button->region_index);
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040eea0
void ShowButton(const GUIButton& button)
{
    if (!button) return;
    EnableRegionInput(button->region_index);
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040eec0
BOOLEAN GetButtonArea(const GUIButton& button, SDL_Rect* rect)
{
    if (!button || !rect) return FALSE;
    *rect = {ButtonRegion(button.get()).x1, ButtonRegion(button.get()).y1,
             ButtonRegion(button.get()).x2 - ButtonRegion(button.get()).x1,
             ButtonRegion(button.get()).y2 - ButtonRegion(button.get()).y1};
    return TRUE;
}

// FUNCTION: WIZ8 0x0040ef00
INT32 GetButtonWidth(const GUIButton& button)
{
    return button ? ButtonRegion(button.get()).x2 - ButtonRegion(button.get()).x1 : -1;
}

// FUNCTION: WIZ8 0x0040ef20
INT32 GetButtonHeight(const GUIButton& button)
{
    return button ? ButtonRegion(button.get()).y2 - ButtonRegion(button.get()).y1 : -1;
}

// FUNCTION: WIZ8 0x0040ef40
INT32 GetButtonX(const GUIButton& button)
{
    return button ? ButtonRegion(button.get()).x1 : 0;
}

// FUNCTION: WIZ8 0x0040ef60
INT32 GetButtonY(const GUIButton& button)
{
    return button ? ButtonRegion(button.get()).y1 : 0;
}

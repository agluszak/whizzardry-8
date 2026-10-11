#include <SDL3/SDL_rect.h>
#include <cstdint>
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
#include "mousesystem.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "Button System.h"
#include "image_assets.h"
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
        if (b->bounds.x != c->bounds.x)
            continue;
        if (b->bounds.y != c->bounds.y)
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
bool gfAnchoredState;
void DrawHatchOnButton(const ButtonAppearance* b);
void DrawShadeOnButton(const ButtonAppearance* b);
void DrawDefaultOnButton(const ButtonAppearance* b);

// GLOBAL: WIZ8 0x005ff824
bool gfRenderHilights = true;

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
    Area.button = this;
}

_GUI_BUTTON::~_GUI_BUTTON()
{
    if (gpAnchoredButton == this) gpAnchoredButton = nullptr;
    if (gpPrevAnchoredButton == this) gpPrevAnchoredButton = nullptr;
}

// GLOBAL: WIZ8 0x00650ea4

static GenericButtonImage default_generic_image;

GenericButtonImage DefaultGenericButtonImage() { return default_generic_image; }




// Load and validate all state frames before publishing the graphics resource.
ButtonImage ButtonGraphics::Load(std::string_view filename, std::int32_t grayed, std::int32_t off_normal,
                                 std::int32_t off_hilite, std::int32_t on_normal, std::int32_t on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (filename.empty() || std::all_of(frames.begin(), frames.end(),
                                      [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    auto image = std::make_shared<ButtonGraphics>();
    image->sprites = g_image_assets.sprites(filename);
    if (!image->sprites)
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
            static_cast<std::uint32_t>(std::max(0, frame.usWidth + frame.sOffsetX)));
        image->MaxHeight = std::max(image->MaxHeight,
            static_cast<std::uint32_t>(std::max(0, frame.usHeight + frame.sOffsetY)));
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
bool DisableButton(const GUIButton& button)
{
    if (!button) return false;
    bool was_enabled = (button->uiFlags & BUTTON_ENABLED) != 0;
    button->uiFlags &= ~BUTTON_ENABLED;
    button->uiFlags |= BUTTON_DIRTY;
    return was_enabled ? true : false;
}

namespace {
GenericButtonImage LoadGenericGraphics(const std::array<std::string_view, 6>& filenames,
                                       std::int16_t background_frame, std::int16_t offset_x, std::int16_t offset_y,
                                       bool optional_highlights = false)
{
    if (filenames[0].empty() || filenames[1].empty()) return {};
    auto image = std::make_shared<GenericButtonImages>();
    for (size_t index = 0; index < filenames.size(); ++index) {
        if (filenames[index].empty()) continue;
        auto& object = image->frames[index];
        object = g_image_assets.sprites(filenames[index]);
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
bool InitializeButtonImageManager()
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
                                          std::int16_t index, std::int16_t offset_x, std::int16_t offset_y)
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

//	InitButtonSystem
//	Initializes the GUI button system for use. Must be called before using
//	any other button functions.
// FUNCTION: WIZ8 0x0040cf60
bool InitButtonSystem(void)
{
    ShutdownButtonSystem();



    // Initialize the button image manager sub-system
    if (InitializeButtonImageManager() == false) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "Failed button image manager init\n");
        return (false);
    }


    return (true);
}

// Stop receiving events before the surrounding screens release their buttons.
// FUNCTION: WIZ8 0x0040cf90
void ShutdownButtonSystem(void)
{
    for (auto& weak : active_buttons) {
        if (auto button = weak.lock()) {
            if (button->Area.uiFlags & MSYS_REGION_EXISTS)
                MSYS_DisableRegion(&button->Area);
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
void ResizeButton(const GUIButton& button, std::int16_t w, std::int16_t h)
{
    GUI_BUTTON* b;
    std::int32_t xloc, yloc;

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
    xloc = b->bounds.x;
    yloc = b->bounds.y;

    // Set the new MOUSE_REGION area values to reflect change in size.
    b->Area.RegionTopLeftX = (std::uint16_t)xloc;
    b->Area.RegionTopLeftY = (std::uint16_t)yloc;
    b->Area.RegionBottomRightX = (std::uint16_t)(xloc + w);
    b->Area.RegionBottomRightY = (std::uint16_t)(yloc + h);
    b->bounds.w = w;
    b->bounds.h = h;
    b->uiFlags |= BUTTON_DIRTY;
}

//	SetButtonPosition
//	Sets the position of a button on the screen. The position is relative
//	to the top left corner of the button.
// FUNCTION: WIZ8 0x0040d2b0
void SetButtonPosition(const GUIButton& button, std::int16_t x, std::int16_t y)
{
    GUI_BUTTON* b;
    std::int32_t xloc, yloc, w, h;

    b = button.get();
    if (!b) return;

    // Get new screen position
    xloc = (std::int16_t)x;
    yloc = (std::int16_t)y;
    // Compute current width and height of this button
    w = b->Area.RegionBottomRightX - b->Area.RegionTopLeftX;
    h = b->Area.RegionBottomRightY - b->Area.RegionTopLeftY;

    // Set button to new location
    b->bounds.x = x;
    b->bounds.y = y;
    // Set the buttons MOUSE_REGION to appropriate area
    b->Area.RegionTopLeftX = (std::uint16_t)xloc;
    b->Area.RegionTopLeftY = (std::uint16_t)yloc;
    b->Area.RegionBottomRightX = (std::uint16_t)(xloc + w);
    b->Area.RegionBottomRightY = (std::uint16_t)(yloc + h);
    b->uiFlags |= BUTTON_DIRTY;
}

namespace {
void RegisterButton(const GUIButton& button, std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height,
                    std::int32_t type, std::int16_t priority, GUI_CALLBACK move, GUI_CALLBACK click)
{
    button->bounds.x = x;
    button->bounds.y = y;
    button->bounds.w = width;
    button->bounds.h = height;
    button->uiFlags |= BUTTON_ENABLED | type;
    button->MoveCallback = move;
    button->ClickCallback = click;
    if (move) button->uiFlags |= BUTTON_MOVE_CALLBACK;
    if (click) button->uiFlags |= BUTTON_CLICK_CALLBACK;
    button->Area.configure(x, y, x + width, y + height,
                      static_cast<std::int8_t>(priority), QuickButtonCallbackMMove, QuickButtonCallbackMButn);
#ifdef BUTTONSYSTEM_DEBUGGING
    AssertFailIfIdenticalButtonAttributesFound(button.get());
#endif
    // Dialogs can be created and destroyed without RenderButtons being called.
    std::erase_if(active_buttons, [](const auto& entry) { return entry.expired(); });
    active_buttons.push_back(button);
}
}

// FUNCTION: WIZ8 0x0040d350
GUIButton CreateTextButton(std::string_view text, Font& font, std::int16_t fore_color, std::int16_t shadow_color,
                           GenericButtonImage image, std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height,
                           std::int32_t type, std::int16_t priority, GUI_CALLBACK move, GUI_CALLBACK click)
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
    RegisterButton(button, x, y, std::max<std::int16_t>(width, 4), std::max<std::int16_t>(height, 3),
                   (type & (BUTTON_TYPE_MASK | BUTTON_NEWTOGGLE)) | BUTTON_GENERIC,
                   priority, move, click);
    return button;
}

// FUNCTION: WIZ8 0x0040d5e0
GUIButton QuickCreateButton(ButtonImage image, std::int16_t x, std::int16_t y, std::int32_t type, std::int16_t priority,
                            GUI_CALLBACK move, GUI_CALLBACK click)
{
    if (x < 0 || y < 0) {
        const auto message = fmt::format("Attempting to QuickCreateButton with invalid position of {},{}", x, y);
        AssertMsg(0, message.c_str());
    }
    if (!image) return {};
    auto button = std::make_shared<GUI_BUTTON>();
    button->quick_image = std::move(image);
    RegisterButton(button, x, y, static_cast<std::int16_t>(button->quick_image->MaxWidth),
                   static_cast<std::int16_t>(button->quick_image->MaxHeight),
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
void SpecifyButtonMultiColorFont(const GUIButton& button, bool enabled)
{
    if (!button) return;
    button->fMultiColor = enabled;
    button->uiFlags |= BUTTON_DIRTY;
}

// FUNCTION: WIZ8 0x0040d8e0
void SpecifyButtonTextOffsets(const GUIButton& button, std::int8_t x, std::int8_t y, bool shift)
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
void QuickButtonCallbackMMove(MOUSE_REGION* reg, std::int32_t reason)
{
    GUI_BUTTON* b = reg ? reg->button : nullptr;
    if (!b) return;

    if (b->uiFlags & BUTTON_ENABLED &&
        reason & (MSYS_CALLBACK_REASON_LOST_MOUSE | MSYS_CALLBACK_REASON_GAIN_MOUSE)) {
        b->uiFlags |= BUTTON_DIRTY;
    }

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if ((b->uiFlags & BUTTON_NEWTOGGLE)) {
        if (reason & MSYS_CALLBACK_REASON_LOST_MOUSE) {
            if (b->ubToggleButtonActivated) {
                b->ubToggleButtonActivated = false;

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
void QuickButtonCallbackMButn(MOUSE_REGION* reg, std::int32_t reason)
{
    GUI_BUTTON* b = reg ? reg->button : nullptr;
    if (!b) return;
    bool StateBefore;

    StateBefore = (b->uiFlags & BUTTON_CLICKED_ON) ? (true) : (false);

    // ATE: New stuff for toggle buttons that work with new Win95 paridigm
    if (b->uiFlags & BUTTON_NEWTOGGLE && b->uiFlags & BUTTON_ENABLED) {
        if (reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) {
            if (!b->ubToggleButtonActivated) {
                if (!(b->uiFlags & BUTTON_CLICKED_ON)) {
                    b->ubToggleButtonOldState = false;
                    b->uiFlags |= BUTTON_CLICKED_ON;
                } else {
                    b->ubToggleButtonOldState = true;
                    b->uiFlags &= (~BUTTON_CLICKED_ON);
                }
                b->ubToggleButtonActivated = true;
            }
        } else if (reason & MSYS_CALLBACK_REASON_LBUTTON_UP) {
            b->ubToggleButtonActivated = false;
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
    {
        FontRenderScope font_scope;
        std::erase_if(active_buttons, [](const auto& entry) { return entry.expired(); });
        for (const auto& weak : active_buttons) {
            auto button = weak.lock();
            if (!button || !(button->Area.uiFlags & MSYS_REGION_ENABLED)) continue;
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
bool DrawButton(const GUIButton& button)
{
    if (!button) return false;
    FontRenderScope font_scope;
    if (button->Area.uiFlags & MSYS_REGION_ENABLED) DrawButtonFromPtr(button.get());
    return true;
}

static std::int8_t DisabledOverlayStyle(const ButtonAppearance& button, const ButtonVisualState& state)
{
    if (button.bDisabledStyle == DISABLED_STYLE_DEFAULT)
        return !state.checkbox && !button.string.empty()
            ? DISABLED_STYLE_SHADED : DISABLED_STYLE_HATCHED;
    return button.bDisabledStyle;
}

static std::int8_t DrawQuickButton(const ButtonAppearance* button, const ButtonVisualState& state);
static std::int8_t DrawGenericButton(const ButtonAppearance* button, const ButtonVisualState& state);

//	DrawButtonFromPtr
//	Given a pointer to a GUI_BUTTON structure, draws the button on the
//	screen.
void DrawButtonFromPtr(GUI_BUTTON* b)
{
    Assert(b);
    if ((b->uiFlags & BUTTON_TYPES) == BUTTON_HOT_SPOT) {
        if (b->uiFlags & BUTTON_NO_TOGGLE) b->uiFlags &= ~BUTTON_CLICKED_ON;
        return;
    }
    DrawButtonAppearance(*b, {
        (b->uiFlags & BUTTON_ENABLED) != 0,
        (b->uiFlags & BUTTON_CLICKED_ON) != 0,
        (b->Area.uiFlags & MSYS_MOUSE_IN_AREA) != 0,
        (b->uiFlags & BUTTON_CHECKBOX) != 0,
        (b->Area.ButtonState & MSYS_LEFT_BUTTON) != 0});
}

static void DrawTextOnButton(const ButtonAppearance* b, const ButtonVisualState& state);

void DrawButtonAppearance(const ButtonAppearance& appearance, const ButtonVisualState& state)
{
    FontRenderScope font_scope;
    const auto* b = &appearance;
    const auto disabled_style = b->quick_image ? DrawQuickButton(b, state) : DrawGenericButton(b, state);
    if (!b->string.empty()) DrawTextOnButton(b, state);
    if (disabled_style == DISABLED_STYLE_HATCHED) DrawHatchOnButton(b);
    else if (disabled_style == DISABLED_STYLE_SHADED) DrawShadeOnButton(b);
    if (b->bDefaultStatus) DrawDefaultOnButton(b);
}

//	DrawQuickButton
//	Draws a QuickButton type button on the screen.
// FUNCTION: WIZ8 0x0040e060
static std::int8_t DrawQuickButton(const ButtonAppearance* b, const ButtonVisualState& state)
{
    std::int8_t disabled_style = DISABLED_STYLE_NONE;
    if (!b->quick_image) return disabled_style;
    const auto& image = *b->quick_image;
    const bool checkbox = state.checkbox;
    const bool pressed = state.pressed;
    std::int32_t frame = 0;
    if (state.enabled) {
        const bool highlighted = (state.hovered) && gfRenderHilights &&
                                 (!checkbox || state.mouse_down);
        const auto normal = pressed ? image.OnNormal : image.OffNormal;
        const auto hilite = pressed ? image.OnHilite : image.OffHilite;
        if (highlighted && hilite != BUTTON_NO_IMAGE) frame = hilite;
        else if (normal != BUTTON_NO_IMAGE) frame = normal;
    } else if (image.Grayed != BUTTON_NO_IMAGE) {
        frame = image.Grayed;
    } else {
        frame = checkbox ? (pressed ? image.OnHilite : image.OffHilite) : image.OffNormal;
        disabled_style = DisabledOverlayStyle(*b, state);
    }
    if (frame != BUTTON_NO_IMAGE)
        BltVideoObject(*ButtonDestBuffer, image.object(), static_cast<std::uint16_t>(frame),
                       b->bounds.x, b->bounds.y, VO_BLT_SRCTRANSPARENCY);
    return disabled_style;
}

void DrawHatchOnButton(const ButtonAppearance* b)
{
    std::uint8_t* pDestBuf;
    std::uint32_t uiDestPitchBYTES;
    SDL_Rect ClipRect{b->bounds.x, b->bounds.y,
                            (b->bounds.x + b->bounds.w) - 1 - b->bounds.x,
                            (b->bounds.y + b->bounds.h) - 1 - b->bounds.y};
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<std::uint8_t*>(destination_storage.pixels);
    Blt16BPPBufferHatchRect((std::uint16_t*)pDestBuf, uiDestPitchBYTES, &ClipRect);
}

void DrawShadeOnButton(const ButtonAppearance* b)
{
    std::uint8_t* pDestBuf;
    std::uint32_t uiDestPitchBYTES;
    SDL_Rect ClipRect{b->bounds.x, b->bounds.y,
                            (b->bounds.x + b->bounds.w) - 1 - b->bounds.x,
                            (b->bounds.y + b->bounds.h) - 1 - b->bounds.y};
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<std::uint8_t*>(destination_storage.pixels);
    Blt16BPPBufferShadowRect((std::uint16_t*)pDestBuf, uiDestPitchBYTES, &ClipRect);
}

// FUNCTION: WIZ8 0x0040e170
void DrawDefaultOnButton(const ButtonAppearance* b)
{
    std::uint8_t* pDestBuf;
    std::uint32_t uiDestPitchBYTES;
    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<std::uint8_t*>(destination_storage.pixels);
    SetClippingRegionAndImageWidth(uiDestPitchBYTES, 0, 0, 640, 480);
    if (b->bDefaultStatus == DEFAULT_STATUS_DARKBORDER ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) {
        //left (one thick)
        LineDraw(true, b->bounds.x - 1, b->bounds.y - 1,
                 b->bounds.x - 1, (b->bounds.y + b->bounds.h) + 1, 0, (char*)pDestBuf);
        //top (one thick)
        LineDraw(true, b->bounds.x - 1, b->bounds.y - 1,
                 (b->bounds.x + b->bounds.w) + 1, b->bounds.y - 1, 0, (char*)pDestBuf);
        //right (two thick)
        LineDraw(true, (b->bounds.x + b->bounds.w), b->bounds.y - 1,
                 (b->bounds.x + b->bounds.w), (b->bounds.y + b->bounds.h) + 1, 0, (char*)pDestBuf);
        LineDraw(true, (b->bounds.x + b->bounds.w) + 1, b->bounds.y - 1,
                 (b->bounds.x + b->bounds.w) + 1, (b->bounds.y + b->bounds.h) + 1, 0,
                 (char*)pDestBuf);
        //bottom (two thick)
        LineDraw(true, b->bounds.x - 1, (b->bounds.y + b->bounds.h),
                 (b->bounds.x + b->bounds.w) + 1, (b->bounds.y + b->bounds.h), 0, (char*)pDestBuf);
        LineDraw(true, b->bounds.x - 1, (b->bounds.y + b->bounds.h) + 1,
                 (b->bounds.x + b->bounds.w) + 1, (b->bounds.y + b->bounds.h) + 1, 0,
                 (char*)pDestBuf);
    }
    if (b->bDefaultStatus == DEFAULT_STATUS_DOTTEDINTERIOR ||
        b->bDefaultStatus == DEFAULT_STATUS_WINDOWS95) { //Draw an internal dotted rectangle.
    }
}

//If a button has text attached to it, then it'll draw it last.
// FUNCTION: WIZ8 0x0040e5d0
static void DrawTextOnButton(const ButtonAppearance* b, const ButtonVisualState& state)
{
    std::int32_t xp, yp, width, height, TextX, TextY;
    SDL_Rect NewClip, OldClip;

    // If this button actually has a string to print
    if (!b->string.empty()) {
        // Get the width and height of this button
        width = b->bounds.w;
        height = b->bounds.h;

        // Compute the viewable area on this button
        NewClip = {b->bounds.x + 3, b->bounds.y + 2, width - 6, height - 4};

        // Get the starting coordinates to print
        TextX = NewClip.x;
        TextY = NewClip.y;

        // Get the current clipping area and clip the viewable area to it
        GetClippingRect(&OldClip);
        if (!SDL_GetRectIntersection(&NewClip, &OldClip, &NewClip))
            return;

        // Set the font printing settings to the buttons viewable area
        SetFontDestBuffer(*ButtonDestBuffer, NewClip.x, NewClip.y, (NewClip.x + NewClip.w),
                          (NewClip.y + NewClip.h), false);

        // Compute the coordinates to center the text
        if (b->bTextYOffset == -1)
            yp = (((height)-b->usFont->height()) / 2) + TextY - 1;
        else
            yp = b->bounds.y + b->bTextYOffset;
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
            xp = b->bounds.x + b->bTextXOffset;

        // Set the printing font to the button text font
        SetFont(*b->usFont);

        // print the text
        SetFontBackground(FONT_MCOLOR_BLACK);
        SetFontForeground((std::uint8_t)b->sForeColor);
        if (b->sShadowColor != -1)
            SetFontShadow((std::uint8_t)b->sShadowColor);
        //Override the colors if necessary.
        if (state.enabled && state.hovered &&
            b->sForeColorHilited != -1) {
            SetFontForeground((std::uint8_t)b->sForeColorHilited);
        } else if (state.pressed && b->sForeColorDown != -1) {
            SetFontForeground((std::uint8_t)b->sForeColorDown);
        }
        if (state.enabled && state.hovered &&
            b->sShadowColorHilited != -1) {
            SetFontShadow((std::uint8_t)b->sShadowColorHilited);
        } else if (state.pressed && b->sShadowColorDown != -1) {
            SetFontShadow((std::uint8_t)b->sShadowColorDown);
        }
        if (state.pressed &&
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
static std::int8_t DrawGenericButton(const ButtonAppearance* b, const ButtonVisualState& state)
{
    std::int8_t disabled_style = DISABLED_STYLE_NONE;
    if (!b->generic_image) return disabled_style;
    std::int32_t NumChunksWide, NumChunksHigh, cx, cy, width, height, hremain, wremain;
    std::int32_t q, ImgNum, ox, oy;
    std::int32_t iBorderHeight, iBorderWidth;
    size_t picture_index = 0;
    std::uint32_t uiDestPitchBYTES;
    std::uint8_t* pDestBuf;
    SDL_Rect ClipRect;
    SpriteFrame* pTrav;

    // Select the graphics to use depending on the current state of the button
    if (state.enabled) {
        if (state.pressed) {
            if ((state.hovered) &&
                (b->generic_image->frames[4] != nullptr) && gfRenderHilights)
                picture_index = 4;
            else
                picture_index = 1;
        } else {
            if ((state.hovered) &&
                (b->generic_image->frames[3] != nullptr) && gfRenderHilights)
                picture_index = 3;
            else
                picture_index = 0;
        }
    } else if (b->generic_image->frames[2])
        picture_index = 2;
    else {
        picture_index = 0;
        disabled_style = DisabledOverlayStyle(*b, state);
    }

    auto& BPic = *b->generic_image->frames[picture_index];
    iBorderWidth = 3;
    iBorderHeight = 2;
    pTrav = nullptr;

    // DB - Added this to support more flexible sizing of border images
    // The 3x2 size was a bit limiting. JA2 should default to the original
    // size, unchanged

    pTrav = &(BPic.sprites[0]);
    iBorderHeight = (std::int32_t)pTrav->usHeight;
    iBorderWidth = (std::int32_t)pTrav->usWidth;

    // Compute the number of button "chunks" needed to be blitted
    width = b->bounds.w;
    height = b->bounds.h;
    NumChunksWide = width / iBorderWidth;
    NumChunksHigh = height / iBorderHeight;
    hremain = height % iBorderHeight;
    wremain = width % iBorderWidth;

    cx = (b->bounds.x + ((NumChunksWide - 1) * iBorderWidth) + wremain);
    cy = (b->bounds.y + ((NumChunksHigh - 1) * iBorderHeight) + hremain);

    // Fill the button's area with the button's background color
    ColorFillVideoSurfaceArea(*ButtonDestBuffer, b->bounds.x, b->bounds.y,
                              (b->bounds.x + b->bounds.w), (b->bounds.y + b->bounds.h),
                              b->generic_image->fill_color);

    // If there is a background image, fill the button's area with it
    if (b->generic_image->frames[5] != nullptr) {
        ox = oy = 0;
        // if the button was clicked on, adjust the background image so that we get
        // the illusion that it is sunk into the screen.
        if (state.pressed)
            ox = oy = 1;

        // Fill the area with the image, tilling it if need be.
        ImageFillVideoSurfaceArea(
            *ButtonDestBuffer, b->bounds.x + ox, b->bounds.y + oy,
            (b->bounds.x + b->bounds.w), (b->bounds.y + b->bounds.h),
            *b->generic_image->frames[5], b->generic_image->background_frame,
            b->generic_image->offset_x, b->generic_image->offset_y);
    }

    const auto& destination_storage = *ButtonDestBuffer->cpu().surface;
    uiDestPitchBYTES = destination_storage.pitch;
    pDestBuf = static_cast<std::uint8_t*>(destination_storage.pixels);

    GetClippingRect(&ClipRect);

    // Draw the button's borders and corners (horizontally)
    for (q = 0; q < NumChunksWide; q++) {
        if (q == 0)
            ImgNum = 0;
        else
            ImgNum = 1;

        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[(std::uint16_t)ImgNum], BPic.currentPalette().data(),
                                   (std::int32_t)(b->bounds.x + (q * iBorderWidth)), (std::int32_t)b->bounds.y, &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        if (q == 0)
            ImgNum = 5;
        else
            ImgNum = 6;

        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[(std::uint16_t)ImgNum], BPic.currentPalette().data(),
                                   (std::int32_t)(b->bounds.x + (q * iBorderWidth)), cy, &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }
    // Blit the right side corners
    BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                               BPic.sprites[2], BPic.currentPalette().data(),
                               cx, (std::int32_t)b->bounds.y, &ClipRect, false,
                               SpriteDrawOp::Transparent);

    BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                               BPic.sprites[7], BPic.currentPalette().data(),
                               cx, cy, &ClipRect, false,
                               SpriteDrawOp::Transparent);
    // Draw the vertical members of the button's borders
    NumChunksHigh--;

    if (hremain != 0) {
        q = NumChunksHigh;
        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[3], BPic.currentPalette().data(),
                                   (std::int32_t)b->bounds.x, (std::int32_t)(b->bounds.y + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[4], BPic.currentPalette().data(),
                                   cx, (std::int32_t)(b->bounds.y + (q * iBorderHeight) - (iBorderHeight - hremain)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    for (q = 1; q < NumChunksHigh; q++) {
        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[3], BPic.currentPalette().data(),
                                   (std::int32_t)b->bounds.x, (std::int32_t)(b->bounds.y + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);

        BltSpriteTo16BPPBuffer((std::uint16_t*)pDestBuf, uiDestPitchBYTES,
                                   BPic.sprites[4], BPic.currentPalette().data(),
                                   cx, (std::int32_t)(b->bounds.y + (q * iBorderHeight)), &ClipRect, false,
                                   SpriteDrawOp::Transparent);
    }

    return disabled_style;
}

// Added Oct17, 97 Carter - kind of mindless, but might as well have it
// FUNCTION: WIZ8 0x0040edc0
void MSYS_SetBtnUserData(const GUIButton& button, std::int32_t index, std::int32_t userdata)
{
    if (button && index >= 0 && index < 4) button->UserData[index] = userdata;
}

// FUNCTION: WIZ8 0x0040edf0
std::int32_t MSYS_GetBtnUserData(GUI_BUTTON* b, std::int32_t index)
{

    if (index < 0 || index > 3)
        return (0);

    return (b->UserData[index]);
}

//Generic Button Movement Callback to reset the mouse button if the mouse is no longer
//in the button region.
// FUNCTION: WIZ8 0x0040ee10
void BtnGenericMouseMoveButtonCallback(GUI_BUTTON* btn, std::int32_t reason)
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
bool GetButtonArea(const GUIButton& button, SDL_Rect* rect)
{
    if (!button || !rect) return false;
    *rect = {button->Area.RegionTopLeftX, button->Area.RegionTopLeftY,
             button->Area.RegionBottomRightX - button->Area.RegionTopLeftX,
             button->Area.RegionBottomRightY - button->Area.RegionTopLeftY};
    return true;
}

// FUNCTION: WIZ8 0x0040ef00
std::int32_t GetButtonWidth(const GUIButton& button)
{
    return button ? button->Area.RegionBottomRightX - button->Area.RegionTopLeftX : -1;
}

// FUNCTION: WIZ8 0x0040ef20
std::int32_t GetButtonHeight(const GUIButton& button)
{
    return button ? button->Area.RegionBottomRightY - button->Area.RegionTopLeftY : -1;
}

// FUNCTION: WIZ8 0x0040ef40
std::int32_t GetButtonX(const GUIButton& button)
{
    return button ? button->Area.RegionTopLeftX : 0;
}

// FUNCTION: WIZ8 0x0040ef60
std::int32_t GetButtonY(const GUIButton& button)
{
    return button ? button->Area.RegionTopLeftY : 0;
}

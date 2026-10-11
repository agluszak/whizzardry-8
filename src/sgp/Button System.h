#ifndef __BUTTON_SYSTEM_H
#define __BUTTON_SYSTEM_H

#include <SDL3/SDL_rect.h>
#include "Font.h"
#include <string>
#include <string_view>
#include <array>
#include <cstddef>

#include "Types.h"
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
//*****************************************************************************************************
//	Button System.h
//
//	by Kris Morness (originally created by Bret Rowden)
//*****************************************************************************************************


#include <memory>

#include "vobject.h"
#include "mousesystem.h"


// Moved here from Button System.c by DB 99/01/07
// Names of the default generic button image files.
#define DEFAULT_GENERIC_BUTTON_OFF "Data\\Message Box\\GENBUTN.STI"
#define DEFAULT_GENERIC_BUTTON_ON "Data\\Message Box\\GENBUTN2.STI"
#define DEFAULT_GENERIC_BUTTON_OFF_HI "Data\\Message Box\\GENBUTN3.STI"
#define DEFAULT_GENERIC_BUTTON_ON_HI "Data\\Message Box\\GENBUTN4.STI"

#define BUTTON_TEXT_LEFT -1
#define BUTTON_TEXT_CENTER 0
#define BUTTON_TEXT_RIGHT 1

#define TEXT_LJUSTIFIED BUTTON_TEXT_LEFT
#define TEXT_CJUSTIFIED BUTTON_TEXT_CENTER
#define TEXT_RJUSTIFIED BUTTON_TEXT_RIGHT

// Some GUI_BUTTON system defines
#define BUTTON_USE_DEFAULT -1
#define BUTTON_NO_FILENAME nullptr
#define BUTTON_NO_CALLBACK nullptr
#define BUTTON_NO_IMAGE -1

#define BUTTON_INIT 1
#define BUTTON_WAS_CLICKED 2

//effects how the button is rendered.
#define BUTTON_TYPES (BUTTON_QUICK | BUTTON_GENERIC | BUTTON_HOT_SPOT | BUTTON_CHECKBOX)
//effects how the button is processed
#define BUTTON_TYPE_MASK                                                                           \
    (BUTTON_NO_TOGGLE | BUTTON_ALLOW_DISABLED_CALLBACK | BUTTON_CHECKBOX | BUTTON_IGNORE_CLICKS)

//button flags
#define BUTTON_TOGGLE 0x00000000
#define BUTTON_QUICK 0x00000000
#define BUTTON_ENABLED 0x00000001
#define BUTTON_CLICKED_ON 0x00000002
#define BUTTON_NO_TOGGLE 0x00000004
#define BUTTON_CLICK_CALLBACK 0x00000008
#define BUTTON_MOVE_CALLBACK 0x00000010
#define BUTTON_GENERIC 0x00000020
#define BUTTON_HOT_SPOT 0x00000040
#define BUTTON_ALLOW_DISABLED_CALLBACK 0x00000200
#define BUTTON_DIRTY 0x00000400
#define BUTTON_SAVEBACKGROUND 0x00000800
#define BUTTON_CHECKBOX 0x00001000
#define BUTTON_NEWTOGGLE 0x00002000
#define BUTTON_FORCE_UNDIRTY                                                                       \
    0x00004000 // no matter what happens this buttons does NOT get marked dirty
#define BUTTON_IGNORE_CLICKS 0x00008000 // Ignore any clicks on this button
#define BUTTON_DISABLED_CALLBACK 0x80000000

extern SGPVSurface* ButtonDestBuffer;

// GUI_BUTTON callback function type
typedef void (*GUI_CALLBACK)(struct _GUI_BUTTON*, INT32);

enum {
    DEFAULT_STATUS_NONE,
    DEFAULT_STATUS_DARKBORDER, //shades the borders 2 pixels deep
    DEFAULT_STATUS_DOTTEDINTERIOR, //draws the familiar dotted line in the interior portion of the button.
    DEFAULT_STATUS_WINDOWS95, //both DARKBORDER and DOTTEDINTERIOR
};
enum //for use with SpecifyDisabledButtonStyle
{
    DISABLED_STYLE_NONE,    //for dummy buttons, panels, etc.  Always displays normal state.
    DISABLED_STYLE_DEFAULT, //if button has text then shade, else hatch
    DISABLED_STYLE_HATCHED, //always hatches the disabled button
    DISABLED_STYLE_SHADED   //always shades the disabled button 25% darker
};

// GUI_BUTTON structure definitions.
struct ButtonGraphics;
struct GenericButtonImages;

using ButtonImage = std::shared_ptr<const ButtonGraphics>;
using GenericButtonImage = std::shared_ptr<GenericButtonImages>;

struct ButtonAppearance {
    ButtonImage quick_image;
    GenericButtonImage generic_image;
    SDL_Rect bounds{};
    INT8 bDefaultStatus = DEFAULT_STATUS_NONE;
    //Button disabled style
    INT8 bDisabledStyle = DISABLED_STYLE_DEFAULT;
    //For buttons with text
    std::string string; //the string
    Font* usFont = 0;       //font for text
    BOOLEAN fMultiColor = FALSE; //font is a multi-color font
    INT16 sForeColor = 0;    //text colors if there is text
    INT16 sShadowColor = -1;
    INT16 sForeColorDown = -1; //text colors when button is down (optional)
    INT16 sShadowColorDown = -1;
    INT16 sForeColorHilited = -1; //text colors when button is down (optional)
    INT16 sShadowColorHilited = -1;
    INT8 bJustification = BUTTON_TEXT_CENTER; // BUTTON_TEXT_LEFT, BUTTON_TEXT_CENTER, BUTTON_TEXT_RIGHT
    INT8 bTextXOffset = -1;
    INT8 bTextYOffset = -1;
    INT8 bTextXSubOffSet = 0;
    INT8 bTextYSubOffSet = 0;
    BOOLEAN fShiftText = TRUE;
    INT16 sWrappedWidth = -1;
};
struct ButtonVisualState {
    bool enabled = true;
    bool pressed = false;
    bool hovered = false;
    bool checkbox = false;
    bool mouse_down = false;
};

void DrawButtonAppearance(const ButtonAppearance& appearance, const ButtonVisualState& state);
GenericButtonImage DefaultGenericButtonImage();

typedef struct _GUI_BUTTON : ButtonAppearance {
    ~_GUI_BUTTON();
    _GUI_BUTTON();
    _GUI_BUTTON(const _GUI_BUTTON&) = delete;
    _GUI_BUTTON& operator=(const _GUI_BUTTON&) = delete;
    MOUSE_REGION Area = {};
    GUI_CALLBACK ClickCallback = nullptr;
    GUI_CALLBACK MoveCallback = nullptr;
    UINT32 uiFlags = BUTTON_DIRTY;
    UINT32 uiOldFlags = 0;
    void* callback_context = nullptr;
    INT32 UserData[4] = {};

    bool ubToggleButtonOldState = false; // Varibles for new toggle buttons that work
    bool ubToggleButtonActivated = false;

} GUI_BUTTON;


using GUIButton = std::shared_ptr<GUI_BUTTON>;

// The interface and screen own their buttons; only weak references are kept
// by the renderer. Mouse dispatch borrows the owning control directly.
size_t RegisteredButtonCount();

// Struct definition for the QuickButton pictures.
struct ButtonGraphics {
    // Each resource owns its decoded sprite sheet. Buttons may share the resource.
    static ButtonImage Load(std::string_view filename, INT32 grayed, INT32 off_normal,
                            INT32 off_hilite, INT32 on_normal, INT32 on_hilite);
    SGPVObject& object() const { return *sprites; }
private:
    std::shared_ptr<SGPVObject> sprites;
public:
    INT32 Grayed;     // Index to use for a "Grayed-out" button
    INT32 OffNormal;  // Index to use when button is OFF
    INT32 OffHilite;  // Index to use when button is OFF w/ hilite on it
    INT32 OnNormal;   // Index to use when button is ON
    INT32 OnHilite;   // Index to use when button is ON w/ hilite on it
    UINT32 MaxWidth;  // Width of largest image in use
    UINT32 MaxHeight; // Height of largest image in use
};


struct GenericButtonImages {
    // Off-normal, on-normal, grayed, off-highlight, on-highlight, background.
    std::array<std::shared_ptr<SGPVObject>, 6> frames;
    UINT16 fill_color = 0;
    UINT16 background_frame = 0;
    INT16 offset_x = 0;
    INT16 offset_y = 0;
};

// Function protos for button system
BOOLEAN InitializeButtonImageManager();
void ShutdownButtonImageManager(void);
BOOLEAN InitButtonSystem(void);
void ShutdownButtonSystem(void);


// Now used by Wizardry -- DB
void SetButtonFastHelpText(const GUIButton& button, std::string_view Text);

GenericButtonImage LoadGenericButtonImages(std::string_view GrayName, std::string_view OffNormName, std::string_view OffHiliteName,
                              std::string_view OnNormName, std::string_view OnHiliteName, std::string_view BkGrndName,
                              INT16 Index, INT16 OffsetX, INT16 OffsetY);

BOOLEAN DisableButton(const GUIButton& button);
void HideButton(const GUIButton& button);
void ShowButton(const GUIButton& button);

void RenderButtons(void);
BOOLEAN DrawButton(const GUIButton& button);
void DrawButtonFromPtr(GUI_BUTTON* b);

//Base button types
// Text can be overlaid on generic and quick buttons.

extern BOOLEAN gfRenderHilights;
#define EnableHilightsAndHelpText() gfRenderHilights = TRUE;
#define DisableHilightsAndHelpText() gfRenderHilights = FALSE;

//Providing you have allocated your own image, this is a somewhat simplified function.
GUIButton QuickCreateButton(ButtonImage Image, INT16 xloc, INT16 yloc, INT32 Type, INT16 Priority,
                        GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback);

//A hybrid of QuickCreateButton.  Takes a lot less parameters, but makes more assumptions.  It self manages the
//loading, and deleting of the image.  The size of the image determines the size of the button.  It also uses
//the default move callback which emulates Win95.  Finally, it sets the priority to normal.  The function you
//choose also determines the type of button (toggle, notoggle, or newtoggle)
//Same as above, but accepts specify toggle type
//Same as above, but accepts priority specification.
GUIButton CreateTextButton(std::string_view string, Font& uiFont, INT16 sForeColor, INT16 sShadowColor,
                       GenericButtonImage GenImg, INT16 xloc, INT16 yloc, INT16 w, INT16 h, INT32 Type,
                       INT16 Priority, GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback);
//New functions
void SpecifyButtonText(const GUIButton& button, std::string_view string);
void SpecifyButtonMultiColorFont(const GUIButton& button, BOOLEAN fMultiColor);
void SpecifyButtonTextOffsets(const GUIButton& button, INT8 bTextXOffset, INT8 bTextYOffset,
                              BOOLEAN fShiftText);


void SetButtonPosition(const GUIButton& button, INT16 x, INT16 y);
void ResizeButton(const GUIButton& button, INT16 w, INT16 h);

void QuickButtonCallbackMMove(MOUSE_REGION* reg, INT32 reason);
void QuickButtonCallbackMButn(MOUSE_REGION* reg, INT32 reason);

void MSYS_SetBtnUserData(const GUIButton& button, INT32 index, INT32 userdata);
INT32 MSYS_GetBtnUserData(GUI_BUTTON* b, INT32 index);
void MarkButtonsDirty(
    void); // Function to mark buttons dirty ( all will redraw at next RenderButtons )
// DB 98-05-05
BOOLEAN GetButtonArea(const GUIButton& button, SDL_Rect* pRect);
// DB 99-01-13
INT32 GetButtonWidth(const GUIButton& button);
INT32 GetButtonHeight(const GUIButton& button);

// DB 99-08-27
INT32 GetButtonX(const GUIButton& button);
INT32 GetButtonY(const GUIButton& button);

void BtnGenericMouseMoveButtonCallback(GUI_BUTTON* btn, INT32 reason);
#define DEFAULT_MOVE_CALLBACK BtnGenericMouseMoveButtonCallback

#endif

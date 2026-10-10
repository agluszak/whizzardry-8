#include <string>
#include <string_view>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
//*****************************************************************************************************
//	Button System.h
//
//	by Kris Morness (originally created by Bret Rowden)
//*****************************************************************************************************

#ifndef __BUTTON_SYSTEM_H
#define __BUTTON_SYSTEM_H

#include "vobject.h"
#include "mousesystem.h"
#include "soundman.h"
#include <memory>


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
#define BUTTON_NO_SLOT -1

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

extern UINT32 ButtonDestBuffer;

// GUI_BUTTON callback function type
typedef void (*GUI_CALLBACK)(struct _GUI_BUTTON*, INT32);

// GUI_BUTTON structure definitions.
struct BUTTON_PICS;
struct GenericButtonImages;
using ButtonImage = std::shared_ptr<BUTTON_PICS>;
using GenericButtonImage = std::shared_ptr<GenericButtonImages>;

typedef struct _GUI_BUTTON : std::enable_shared_from_this<_GUI_BUTTON> {
    ~_GUI_BUTTON();
    _GUI_BUTTON() = default;
    _GUI_BUTTON(const _GUI_BUTTON&) = delete;
    _GUI_BUTTON& operator=(const _GUI_BUTTON&) = delete;

    INT32 IDNum;                // ID Number, contains it's own button number
    ButtonImage quick_image;
    GenericButtonImage generic_image;
    MOUSE_REGION Area;          // Mouse System's mouse region to use for this button
    GUI_CALLBACK ClickCallback; // Button Callback when button is clicked
    GUI_CALLBACK MoveCallback;  // Button Callback when mouse moved on this region
    INT16 Cursor;               // Cursor to use for this button
    UINT32 uiFlags;             // Button state flags etc.( 32-bit )
    UINT32 uiOldFlags;          // Old flags from previous render loop
    INT16 XLoc;                 // Coordinates where button is on the screen
    INT16 YLoc;
    INT32 UserData[4]; // Place holder for user data etc.
    INT16 Group;       // Group this button belongs to (see DOCs)
    INT8 bDefaultStatus;
    //Button disabled style
    INT8 bDisabledStyle;
    //For buttons with text
    std::string string; //the string
    UINT16 usFont;       //font for text
    BOOLEAN fMultiColor; //font is a multi-color font
    INT16 sForeColor;    //text colors if there is text
    INT16 sShadowColor;
    INT16 sForeColorDown; //text colors when button is down (optional)
    INT16 sShadowColorDown;
    INT16 sForeColorHilited; //text colors when button is down (optional)
    INT16 sShadowColorHilited;
    INT8 bJustification; // BUTTON_TEXT_LEFT, BUTTON_TEXT_CENTER, BUTTON_TEXT_RIGHT
    INT8 bTextXOffset;
    INT8 bTextYOffset;
    INT8 bTextXSubOffSet;
    INT8 bTextYSubOffSet;
    BOOLEAN fShiftText;
    INT16 sWrappedWidth;
    //For buttons with icons (don't confuse this with quickbuttons which have up to 5 states )
    INT32 iIconID;
    INT16 usIconIndex;
    INT8 bIconXOffset;   //-1 means horizontally centered
    INT8 bIconYOffset;   //-1 means vertically centered
    BOOLEAN fShiftImage; //if true, icon is shifted +1,+1 when button state is down.

    bool ubToggleButtonOldState; // Varibles for new toggle buttons that work
    bool ubToggleButtonActivated;

    INT32 BackRect; // Handle to a Background Rectangle
} GUI_BUTTON;


using GUIButton = std::shared_ptr<GUI_BUTTON>;

// The interface and screen own their buttons; only weak references are kept
// by the renderer and mouse dispatcher.
size_t RegisteredButtonCount();

// Struct definition for the QuickButton pictures.
struct BUTTON_PICS {
    std::unique_ptr<SGPVObject> ownedObject;
    HVOBJECT borrowedObject = nullptr;
    HVOBJECT object() const { return ownedObject ? ownedObject.get() : borrowedObject; }
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
    std::array<std::unique_ptr<SGPVObject>, 6> frames;
    UINT16 fill_color = 0;
    UINT16 background_frame = 0;
    INT16 offset_x = 0;
    INT16 offset_y = 0;
};

// Function protos for button system
BOOLEAN InitializeButtonImageManager(INT32 DefaultBuffer, INT32 DefaultPitch, INT32 DefaultBPP);
void ShutdownButtonImageManager(void);
BOOLEAN InitButtonSystem(void);
void ShutdownButtonSystem(void);


// Now used by Wizardry -- DB
void SetButtonFastHelpText(const GUIButton& button, std::string_view Text);

ButtonImage LoadButtonImage(std::string_view filename, INT32 Grayed, INT32 OffNormal, INT32 OffHilite,
                      INT32 OnNormal, INT32 OnHilite);
ButtonImage UseVObjAsButtonImage(HVOBJECT hVObject, INT32 Grayed, INT32 OffNormal, INT32 OffHilite,
                           INT32 OnNormal, INT32 OnHilite);
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
void DrawGenericButton(GUI_BUTTON* b);
void DrawQuickButton(GUI_BUTTON* b);
void DrawCheckBoxButton(GUI_BUTTON* b);
//Additional layers on buttons that can exist in any combination on generic or quick buttons
//To do so, use the new specify functions below.
void DrawIconOnButton(GUI_BUTTON* b);
void DrawTextOnButton(GUI_BUTTON* b);

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
GUIButton CreateTextButton(std::string_view string, UINT32 uiFont, INT16 sForeColor, INT16 sShadowColor,
                       GenericButtonImage GenImg, INT16 xloc, INT16 yloc, INT16 w, INT16 h, INT32 Type,
                       INT16 Priority, GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback);
//New functions
void SpecifyButtonText(const GUIButton& button, std::string_view string);
void SpecifyButtonMultiColorFont(const GUIButton& button, BOOLEAN fMultiColor);
void SpecifyButtonTextOffsets(const GUIButton& button, INT8 bTextXOffset, INT8 bTextYOffset,
                              BOOLEAN fShiftText);

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

//Note:  Text is always on top
//If fShiftImage is true, then the image will shift down one pixel and right one pixel
//just like the text does.

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

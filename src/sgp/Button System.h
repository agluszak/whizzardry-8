#ifndef __BUTTON_SYSTEM_H
#define __BUTTON_SYSTEM_H

#include <SDL3/SDL_rect.h>
#include <cstdint>
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
typedef void (*GUI_CALLBACK)(struct _GUI_BUTTON*, std::int32_t);

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
    std::int8_t bDefaultStatus = DEFAULT_STATUS_NONE;
    //Button disabled style
    std::int8_t bDisabledStyle = DISABLED_STYLE_DEFAULT;
    //For buttons with text
    std::string string; //the string
    Font* usFont = 0;       //font for text
    bool fMultiColor = false; //font is a multi-color font
    std::int16_t sForeColor = 0;    //text colors if there is text
    std::int16_t sShadowColor = -1;
    std::int16_t sForeColorDown = -1; //text colors when button is down (optional)
    std::int16_t sShadowColorDown = -1;
    std::int16_t sForeColorHilited = -1; //text colors when button is down (optional)
    std::int16_t sShadowColorHilited = -1;
    std::int8_t bJustification = BUTTON_TEXT_CENTER; // BUTTON_TEXT_LEFT, BUTTON_TEXT_CENTER, BUTTON_TEXT_RIGHT
    std::int8_t bTextXOffset = -1;
    std::int8_t bTextYOffset = -1;
    std::int8_t bTextXSubOffSet = 0;
    std::int8_t bTextYSubOffSet = 0;
    bool fShiftText = true;
    std::int16_t sWrappedWidth = -1;
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
    std::uint32_t uiFlags = BUTTON_DIRTY;
    std::uint32_t uiOldFlags = 0;
    void* callback_context = nullptr;
    std::int32_t UserData[4] = {};

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
    static ButtonImage Load(std::string_view filename, std::int32_t grayed, std::int32_t off_normal,
                            std::int32_t off_hilite, std::int32_t on_normal, std::int32_t on_hilite);
    SGPVObject& object() const { return *ownedObject; }
private:
    std::unique_ptr<SGPVObject> ownedObject;
public:
    std::int32_t Grayed;     // Index to use for a "Grayed-out" button
    std::int32_t OffNormal;  // Index to use when button is OFF
    std::int32_t OffHilite;  // Index to use when button is OFF w/ hilite on it
    std::int32_t OnNormal;   // Index to use when button is ON
    std::int32_t OnHilite;   // Index to use when button is ON w/ hilite on it
    std::uint32_t MaxWidth;  // Width of largest image in use
    std::uint32_t MaxHeight; // Height of largest image in use
};


struct GenericButtonImages {
    // Off-normal, on-normal, grayed, off-highlight, on-highlight, background.
    std::array<std::unique_ptr<SGPVObject>, 6> frames;
    std::uint16_t fill_color = 0;
    std::uint16_t background_frame = 0;
    std::int16_t offset_x = 0;
    std::int16_t offset_y = 0;
};

// Function protos for button system
bool InitializeButtonImageManager();
void ShutdownButtonImageManager(void);
bool InitButtonSystem(void);
void ShutdownButtonSystem(void);


// Now used by Wizardry -- DB
void SetButtonFastHelpText(const GUIButton& button, std::string_view Text);

GenericButtonImage LoadGenericButtonImages(std::string_view GrayName, std::string_view OffNormName, std::string_view OffHiliteName,
                              std::string_view OnNormName, std::string_view OnHiliteName, std::string_view BkGrndName,
                              std::int16_t Index, std::int16_t OffsetX, std::int16_t OffsetY);

bool DisableButton(const GUIButton& button);
void HideButton(const GUIButton& button);
void ShowButton(const GUIButton& button);

void RenderButtons(void);
bool DrawButton(const GUIButton& button);
void DrawButtonFromPtr(GUI_BUTTON* b);

//Base button types
// Text can be overlaid on generic and quick buttons.

extern bool gfRenderHilights;
#define EnableHilightsAndHelpText() gfRenderHilights = true;
#define DisableHilightsAndHelpText() gfRenderHilights = false;

//Providing you have allocated your own image, this is a somewhat simplified function.
GUIButton QuickCreateButton(ButtonImage Image, std::int16_t xloc, std::int16_t yloc, std::int32_t Type, std::int16_t Priority,
                        GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback);

//A hybrid of QuickCreateButton.  Takes a lot less parameters, but makes more assumptions.  It self manages the
//loading, and deleting of the image.  The size of the image determines the size of the button.  It also uses
//the default move callback which emulates Win95.  Finally, it sets the priority to normal.  The function you
//choose also determines the type of button (toggle, notoggle, or newtoggle)
//Same as above, but accepts specify toggle type
//Same as above, but accepts priority specification.
GUIButton CreateTextButton(std::string_view string, Font& uiFont, std::int16_t sForeColor, std::int16_t sShadowColor,
                       GenericButtonImage GenImg, std::int16_t xloc, std::int16_t yloc, std::int16_t w, std::int16_t h, std::int32_t Type,
                       std::int16_t Priority, GUI_CALLBACK MoveCallback, GUI_CALLBACK ClickCallback);
//New functions
void SpecifyButtonText(const GUIButton& button, std::string_view string);
void SpecifyButtonMultiColorFont(const GUIButton& button, bool fMultiColor);
void SpecifyButtonTextOffsets(const GUIButton& button, std::int8_t bTextXOffset, std::int8_t bTextYOffset,
                              bool fShiftText);


void SetButtonPosition(const GUIButton& button, std::int16_t x, std::int16_t y);
void ResizeButton(const GUIButton& button, std::int16_t w, std::int16_t h);

void QuickButtonCallbackMMove(MOUSE_REGION* reg, std::int32_t reason);
void QuickButtonCallbackMButn(MOUSE_REGION* reg, std::int32_t reason);

void MSYS_SetBtnUserData(const GUIButton& button, std::int32_t index, std::int32_t userdata);
std::int32_t MSYS_GetBtnUserData(GUI_BUTTON* b, std::int32_t index);
void MarkButtonsDirty(
    void); // Function to mark buttons dirty ( all will redraw at next RenderButtons )
// DB 98-05-05
bool GetButtonArea(const GUIButton& button, SDL_Rect* pRect);
// DB 99-01-13
std::int32_t GetButtonWidth(const GUIButton& button);
std::int32_t GetButtonHeight(const GUIButton& button);

// DB 99-08-27
std::int32_t GetButtonX(const GUIButton& button);
std::int32_t GetButtonY(const GUIButton& button);

void BtnGenericMouseMoveButtonCallback(GUI_BUTTON* btn, std::int32_t reason);
#define DEFAULT_MOVE_CALLBACK BtnGenericMouseMoveButtonCallback

#endif

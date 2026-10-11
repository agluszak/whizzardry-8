#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_rect.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

//	MouseSystem.c
//	Routines for handling prioritized mouse regions. The system as setup below allows the use of
//	callback functions for each region, as well as allowing a different cursor to be defined for
//	each region.
//	Written by Bret Rowdon, Jan 30 '97
//  Re-Written by Kris Morness, since...

#include <algorithm>
#include <string_view>
#include <cstdint>

#include "Types.h"
#include "input.h"
#include "Video2.h"
#include "compat/native.h"

// Include mouse system defs and macros
#include "mousesystem.h"
#include "Button System.h"

namespace {
// UI dispatch state for events already translated by the SDL input backend.
// Registrations themselves belong to each control's region.
struct MouseState {
    // GLOBAL: WIZ8 0x00650e80
    INT16 x = 0;
    // GLOBAL: WIZ8 0x00650e82
    INT16 y = 0;
    // GLOBAL: WIZ8 0x00650e84
    INT16 buttons = 0;
    // GLOBAL: WIZ8 0x00650e86
    INT16 action = MSYS_NO_ACTION;
    UINT8 clicks = 0;
    // GLOBAL: WIZ8 0x00650e88
    bool initialized = false;
    // GLOBAL: WIZ8 0x00650e8c
    MOUSE_REGION* grabbed = nullptr;
    // GLOBAL: WIZ8 0x006e4100
    MOUSE_REGION* clicked = nullptr;
    // GLOBAL: WIZ8 0x00650e98
    MOUSE_REGION* previous = nullptr;
    // GLOBAL: WIZ8 0x00650e9c
    MOUSE_REGION* current = nullptr;
    // GLOBAL: WIZ8 0x005ff7c8
    INT16 help_delay = 600;
    // GLOBAL: WIZ8 0x005ff7ca
    bool show_help = true;
    // GLOBAL: WIZ8 0x00650ea0
    bool refresh = false;
};
MouseState mouse;
}

// GLOBAL: WIZ8 0x00650e94
MOUSE_REGION* _MOUSE_REGION::first = nullptr;

//	MSYS_Init
//	Initialize the mouse system.
// FUNCTION: WIZ8 0x0040b290
INT32 MSYS_Init(void)
{

    MSYS_Shutdown();


    mouse.x = 0;
    mouse.y = 0;
    mouse.buttons = 0;
    mouse.action = MSYS_NO_ACTION;

    mouse.previous = nullptr;
    mouse.initialized = true;

    mouse.grabbed = nullptr;

    return (1);
}

//	MSYS_Shutdown
//	De-inits the "mousesystem" mouse region handling code.
// FUNCTION: WIZ8 0x0040b450
void MSYS_Shutdown(void)
{
    mouse.initialized = false;
    for (auto* region = MOUSE_REGION::first; region; region = region->next)
        region->reset();
}

namespace {
// Stack observers let destruction invalidate every suspended callback invocation.
// Nested events execute synchronously, with their own coordinates and actions.
struct MouseDispatch {
    MOUSE_REGION* current = nullptr;
    MOUSE_REGION* previous;
    INT16 action, x, y, buttons;
    UINT8 clicks;
    MouseDispatch* parent;
    unsigned long long generation;
    static MouseDispatch* active;
    static unsigned long long latest;

    MouseDispatch() : previous(mouse.previous), action(mouse.action),
        x(mouse.x), y(mouse.y), buttons(mouse.buttons), clicks(mouse.clicks),
        parent(active), generation(++latest) { active = this; }
    ~MouseDispatch() {
        // A nested event owns the final hover state if it ran during this event.
        if (generation == latest) {
            mouse.action = action;
            mouse.current = current;
            mouse.previous = previous;
        }
        active = parent;
    }
};
MouseDispatch* MouseDispatch::active = nullptr;
unsigned long long MouseDispatch::latest = 0;
}

//	DispatchMouseEvent
//	Hook to the SGP's mouse handler
// FUNCTION: WIZ8 0x0040b510
void DispatchMouseEvent(const InputAtom& input)
{
    // If the mouse system isn't initialized, get out o' here
    if (!mouse.initialized)
        return;

    const auto Type = input.usEvent;
    const auto Xcoord = GETXPOS(&input);
    const auto Ycoord = GETYPOS(&input);
    const bool LeftButton = (input.mouseButtons & SDL_BUTTON_LMASK) != 0;
    const bool RightButton = (input.mouseButtons & SDL_BUTTON_RMASK) != 0;
    mouse.buttons = (LeftButton ? MSYS_LEFT_BUTTON : 0) |
                    (RightButton ? MSYS_RIGHT_BUTTON : 0);
    mouse.clicks = input.clicks;
    mouse.action = MSYS_NO_ACTION;
    switch (Type) {
    case LEFT_BUTTON_DOWN:
    case LEFT_BUTTON_UP:
    case RIGHT_BUTTON_DOWN:
    case RIGHT_BUTTON_UP:
        //mouse.action|=MSYS_DO_BUTTONS;
        if (Type == LEFT_BUTTON_DOWN)
            mouse.action |= MSYS_DO_LBUTTON_DWN;
        else if (Type == LEFT_BUTTON_UP) {
            mouse.action |= MSYS_DO_LBUTTON_UP;
            //Kris:
            //Used only if applicable.  This is used for that special button that is locked with the
            //mouse press -- just like windows.  When you release the button, the previous state
            //of the button is restored if you released the mouse outside of it's boundaries.  If
            //you release inside of the button, the action is selected -- but later in the code.
            //NOTE:  It has to be here, because the mouse can be released anywhere regardless of
            //regions, buttons, etc.
        } else if (Type == RIGHT_BUTTON_DOWN)
            mouse.action |= MSYS_DO_RBUTTON_DWN;
        else if (Type == RIGHT_BUTTON_UP)
            mouse.action |= MSYS_DO_RBUTTON_UP;

        if ((Xcoord != mouse.x) || (Ycoord != mouse.y)) {
            mouse.action |= MSYS_DO_MOVE;
            mouse.x = Xcoord;
            mouse.y = Ycoord;
        }

        MSYS_UpdateMouseRegion();
        break;

    // ATE: Checks here for mouse button repeats.....
    // Call mouse region with new reason
    case LEFT_BUTTON_REPEAT:
    case RIGHT_BUTTON_REPEAT:

        if (Type == LEFT_BUTTON_REPEAT)
            mouse.action |= MSYS_DO_LBUTTON_REPEAT;
        else if (Type == RIGHT_BUTTON_REPEAT)
            mouse.action |= MSYS_DO_RBUTTON_REPEAT;

        if ((Xcoord != mouse.x) || (Ycoord != mouse.y)) {
            mouse.action |= MSYS_DO_MOVE;
            mouse.x = Xcoord;
            mouse.y = Ycoord;
        }

        MSYS_UpdateMouseRegion();
        break;

    case MOUSE_POS:
        if ((Xcoord != mouse.x) || (Ycoord != mouse.y) || mouse.refresh) {
            mouse.action |= MSYS_DO_MOVE;
            mouse.x = Xcoord;
            mouse.y = Ycoord;

            mouse.refresh = false;

            MSYS_UpdateMouseRegion();
        }
        break;

    default:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "ERROR -- MSYS 2 SGP Mouse Hook got bad type");
        break;
    }
}

void RefreshMouseRegions()
{
    SDL_Point point;
    SGPMouseGetPos(&point);
    InputAtom input{};
    input.usEvent = MOUSE_POS;
    input.uiParam = static_cast<UINT32>(point.x) | (static_cast<UINT32>(point.y) << 16);
    input.mouseButtons = (gfLeftButtonState ? SDL_BUTTON_LMASK : 0) |
                         (gfRightButtonState ? SDL_BUTTON_RMASK : 0);
    DispatchMouseEvent(input);
}

_MOUSE_REGION::_MOUSE_REGION()
{
    next = first;
    if (next) next->previous = this;
    first = this;
}

void _MOUSE_REGION::unlink()
{
    if (previous) previous->next = next;
    else first = next;
    if (next) next->previous = previous;
    previous = next = nullptr;
}

//	MSYS_UpdateMouseRegion
//	Searches the list for the highest priority region and updates it's info. It also dispatches
//	the callback functions
// FUNCTION: WIZ8 0x0040b900
void MSYS_UpdateMouseRegion(void)
{
    MouseDispatch event;
    INT32 found;
    UINT32 ButtonReason;
    found = FALSE;

    // Check previous region!
    if (mouse.grabbed) {
        event.current = mouse.grabbed;
        found = TRUE;
    }
    if (!found) {
        event.current = nullptr;
        const SDL_Point point{event.x, event.y};
        for (auto* candidate = MOUSE_REGION::first; candidate; candidate = candidate->next) {
            // SGP's bottom/right edges are inclusive; SDL_Rect's are exclusive.
            const SDL_Rect bounds{
                candidate->RegionTopLeftX, candidate->RegionTopLeftY,
                candidate->RegionBottomRightX - candidate->RegionTopLeftX + 1,
                candidate->RegionBottomRightY - candidate->RegionTopLeftY + 1};
            if ((candidate->uiFlags & (MSYS_REGION_ENABLED | MSYS_ALLOW_DISABLED_FASTHELP)) &&
                SDL_PointInRect(&point, &bounds) &&
                (!event.current || candidate->PriorityLevel > event.current->PriorityLevel))
                event.current = candidate;
        }
        found = event.current != nullptr;
    }

    mouse.current = event.current;

    if (event.previous) {
        event.previous->uiFlags &= (~MSYS_MOUSE_IN_AREA);

        if (event.previous != event.current) {
            // The previous region has left before its callback can nest.
            mouse.previous = nullptr;
            //Remove the help text for the previous region if one is currently being displayed.
            if (!event.previous->FastHelpText.empty()) {
                //ExecuteMouseHelpEndCallBack( event.previous );

                event.previous->uiFlags &= (~MSYS_GOT_BACKGROUND);
                event.previous->uiFlags &= (~MSYS_FASTHELP_RESET);

                // dirty buttons, need a re-render
                //DEF: Nov 30 98
                //				PausedMarkButtonsDirty( );

                //if( region->uiFlags & MSYS_REGION_ENABLED )
                //	region->uiFlags |= BUTTON_DIRTY;
                VideoRemoveToolTip();
            }

            if (event.current)
                event.current->FastHelpTimer = mouse.help_delay;

            // Force a callbacks to happen on previous region to indicate that
            // the mouse has left the old region
            if (event.previous->uiFlags & MSYS_MOVE_CALLBACK &&
                event.previous->uiFlags & MSYS_REGION_ENABLED)
                (*(event.previous->MovementCallback))(event.previous,
                                                       MSYS_CALLBACK_REASON_LOST_MOUSE);
            if (event.generation != MouseDispatch::latest) return;
        }
    }

    // If a region was found in the list, update it's data
    if (found && event.current) {
        mouse.previous = event.current;
        if (event.current != event.previous) {
            //Kris -- October 27, 1997
            //Implemented gain mouse region
            if (event.current->uiFlags & MSYS_MOVE_CALLBACK) {
                if (!event.current->FastHelpText.empty() &&
                    !(event.current->uiFlags & MSYS_FASTHELP_RESET)) {
                    //ExecuteMouseHelpEndCallBack( event.current );
                    event.current->FastHelpTimer = mouse.help_delay;
                    event.current->uiFlags &= (~MSYS_GOT_BACKGROUND);
                    event.current->uiFlags |= MSYS_FASTHELP_RESET;

                    VideoRemoveToolTip();

                    //if( b->uiFlags & BUTTON_ENABLED )
                    //	b->uiFlags |= BUTTON_DIRTY;
                }
                if (event.current->uiFlags & MSYS_REGION_ENABLED) {
                    (*(event.current->MovementCallback))(event.current,
                                                           MSYS_CALLBACK_REASON_GAIN_MOUSE);
                    if (!event.current || event.generation != MouseDispatch::latest)
                        return;
                }
            }

        }

        // OK, if we do not have a button down, any button is game!
        if (!mouse.clicked || mouse.clicked == event.current) {
            event.current->uiFlags |= MSYS_MOUSE_IN_AREA;

            event.current->MouseXPos = event.x;
            event.current->MouseYPos = event.y;
            event.current->RelativeXPos = event.x - event.current->RegionTopLeftX;
            event.current->RelativeYPos = event.y - event.current->RegionTopLeftY;

            event.current->ButtonState = event.buttons;

            if (event.current->uiFlags & MSYS_REGION_ENABLED &&
                event.current->uiFlags & MSYS_MOVE_CALLBACK && event.action & MSYS_DO_MOVE) {
                (*(event.current->MovementCallback))(event.current, MSYS_CALLBACK_REASON_MOVE);
                if (!event.current || event.generation != MouseDispatch::latest)
                    return;
            }

            //ExecuteMouseHelpEndCallBack( event.current );
            //event.current->FastHelpTimer = mouse.help_delay;

            event.action &= (~MSYS_DO_MOVE);

            if ((event.current->uiFlags & MSYS_BUTTON_CALLBACK) &&
                (event.action & MSYS_DO_BUTTONS)) {
                if (event.current->uiFlags & MSYS_REGION_ENABLED) {
                    ButtonReason = MSYS_CALLBACK_REASON_NONE;
                    if (event.action & MSYS_DO_LBUTTON_DWN) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_DWN;
                        // Capture this region
                        mouse.clicked = event.current;
                    }

                    if (event.action & MSYS_DO_LBUTTON_UP) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_UP;
                        mouse.clicked = nullptr;
                    }

                    if (event.action & MSYS_DO_RBUTTON_DWN) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_DWN;
                        // Capture this region
                        mouse.clicked = event.current;
                    }

                    if (event.action & MSYS_DO_RBUTTON_UP) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_UP;
                        mouse.clicked = nullptr;
                    }

                    // ATE: Added repeat resons....
                    if (event.action & MSYS_DO_LBUTTON_REPEAT) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_REPEAT;
                    }

                    if (event.action & MSYS_DO_RBUTTON_REPEAT) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_REPEAT;
                    }

                    if (ButtonReason != MSYS_CALLBACK_REASON_NONE) {
                        if (event.current->uiFlags & MSYS_FASTHELP) {
                            // Button was clicked so remove any FastHelp text
                            event.current->uiFlags &= (~MSYS_FASTHELP);
                            event.current->uiFlags &= (~MSYS_GOT_BACKGROUND);

                            //ExecuteMouseHelpEndCallBack( event.current );
                            event.current->FastHelpTimer = mouse.help_delay;
                            event.current->uiFlags &= (~MSYS_FASTHELP_RESET);

                            //if( b->uiFlags & BUTTON_ENABLED )
                            //	b->uiFlags |= BUTTON_DIRTY;
                            VideoRemoveToolTip();
                        }

                        if ((event.action & MSYS_DO_LBUTTON_DWN) && event.clicks != 0 &&
                            (event.clicks & 1) == 0)
                            ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_DOUBLECLICK;

                        (*(event.current->ButtonCallback))(event.current, ButtonReason);
                    }
                }
            }

            event.action &= (~MSYS_DO_BUTTONS);
        } else if (event.current->uiFlags & MSYS_REGION_ENABLED) {
            // OK here, if we have release a button, UNSET LOCK wherever you are....
            // Just don't give this button the message....
            if (event.action & MSYS_DO_RBUTTON_UP) {
                mouse.clicked = nullptr;
            }
            if (event.action & MSYS_DO_LBUTTON_UP) {
                mouse.clicked = nullptr;
            }

            // OK, you still want move messages however....
            event.current->uiFlags |= MSYS_MOUSE_IN_AREA;
            event.current->MouseXPos = event.x;
            event.current->MouseYPos = event.y;
            event.current->RelativeXPos = event.x - event.current->RegionTopLeftX;
            event.current->RelativeYPos = event.y - event.current->RegionTopLeftY;

            event.current->ButtonState = event.buttons;
            if ((event.current->uiFlags & MSYS_MOVE_CALLBACK) && (event.action & MSYS_DO_MOVE)) {
                (*(event.current->MovementCallback))(event.current, MSYS_CALLBACK_REASON_MOVE);
                if (!event.current || event.generation != MouseDispatch::latest)
                    return;
            }

            event.action &= (~MSYS_DO_MOVE);
        }
        event.previous = event.current;
    } else {
        event.previous = nullptr;
        // Releasing outside all controls ends click capture too.
        if (event.action & (MSYS_DO_LBUTTON_UP | MSYS_DO_RBUTTON_UP))
            mouse.clicked = nullptr;
        event.action = MSYS_NO_ACTION;
    }
}

// Configure the region owned by an interactive control.
// FUNCTION: WIZ8 0x0040be10
void _MOUSE_REGION::configure(UINT16 tlx, UINT16 tly, UINT16 brx, UINT16 bry,
                               INT8 priority, MOUSE_CALLBACK movecallback,
                               MOUSE_CALLBACK buttoncallback)
{
    reset();
    // Most recently configured wins among equally prioritized controls.
    unlink();
    next = first;
    if (next) next->previous = this;
    first = this;

    if (priority == MSYS_PRIORITY_AUTO)
        priority = MSYS_PRIORITY_BASE;
    else if (priority <= MSYS_PRIORITY_LOWEST)
        priority = MSYS_PRIORITY_LOWEST;
    else if (priority >= MSYS_PRIORITY_HIGHEST)
        priority = MSYS_PRIORITY_HIGHEST;

    PriorityLevel = priority;

    uiFlags = MSYS_NO_FLAGS;

    MovementCallback = movecallback;
    if (movecallback != MSYS_NO_CALLBACK)
        uiFlags |= MSYS_MOVE_CALLBACK;

    ButtonCallback = buttoncallback;
    if (buttoncallback != MSYS_NO_CALLBACK)
        uiFlags |= MSYS_BUTTON_CALLBACK;

    RegionTopLeftX = tlx;
    RegionTopLeftY = tly;
    RegionBottomRightX = brx;
    RegionBottomRightY = bry;

    MouseXPos = 0;
    MouseYPos = 0;
    RelativeXPos = 0;
    RelativeYPos = 0;
    ButtonState = 0;

    //Init fasthelp
    FastHelpText.clear();
    FastHelpTimer = 0;

    HelpDoneCallback = nullptr;

    // The region participates in hit testing while configured.
    uiFlags |= MSYS_REGION_ENABLED | MSYS_REGION_EXISTS;

    // Dirty our update flag
    mouse.refresh = true;
}

// Clear dispatch observers before reconfiguration or destruction.
// FUNCTION: WIZ8 0x0040bee0
void _MOUSE_REGION::reset()
{
    // Get rid of the FastHelp text (if applicable)
    if (!FastHelpText.empty()) {
        if (uiFlags & MSYS_FASTHELP)
            VideoRemoveToolTip();
        FastHelpText.clear();
    }

    if (mouse.grabbed == this) {
        mouse.grabbed = nullptr;
    }

    for (auto* event = MouseDispatch::active; event; event = event->parent) {
        if (event->current == this) event->current = nullptr;
        if (event->previous == this) event->previous = nullptr;
    }

    //if the previous region is the one that we are deleting, reset the previous region
    if (mouse.previous == this)
        mouse.previous = nullptr;
    //if the current region is the one that we are deleting, then clear it.
    if (mouse.current == this)
        mouse.current = nullptr;

    //dirty our update flag
    mouse.refresh = true;

    if (mouse.clicked == this) {
        mouse.clicked = nullptr;
    }

    // Mark unregistered before the owning control can be destroyed.
    uiFlags = 0;
    MovementCallback = ButtonCallback = nullptr;
    HelpDoneCallback = nullptr;
}

//	MSYS_EnableRegion
//	Enables a mouse region.
// FUNCTION: WIZ8 0x0040bf60
void MSYS_EnableRegion(MOUSE_REGION* region)
{
    region->uiFlags |= MSYS_REGION_ENABLED;
}

//	MSYS_DisableRegion
//	Disables a mouse region without removing it from the system list.
// FUNCTION: WIZ8 0x0040bf70
void MSYS_DisableRegion(MOUSE_REGION* region)
{
    region->uiFlags &= (~MSYS_REGION_ENABLED);
}

//	MSYS_SetRegionUserData
//	Sets one of the four user data entries in a mouse region
// FUNCTION: WIZ8 0x0040bf80
void MSYS_SetRegionUserData(MOUSE_REGION* region, INT32 index, INT32 userdata)
{
    if (index < 0 || index > 3) return;
    region->UserData[index] = userdata;
}

//	MSYS_GetRegionUserData
//	Retrieves one of the four user data entries in a mouse region
// FUNCTION: WIZ8 0x0040bfa0
INT32 MSYS_GetRegionUserData(MOUSE_REGION* region, INT32 index)
{
    if (index < 0 || index > 3) return 0;
    return (region->UserData[index]);
}

//	MSYS_GrabMouse
//	Assigns all mouse activity to a region, effectively blocking any other region from having
//	control.
// FUNCTION: WIZ8 0x0040bfc0
INT32 MSYS_GrabMouse(MOUSE_REGION* region)
{
    if (!region || !(region->uiFlags & MSYS_REGION_EXISTS))
        return (MSYS_REGION_NOT_IN_LIST);

    if (mouse.grabbed != nullptr)
        return (MSYS_ALREADY_GRABBED);

    mouse.grabbed = region;
    return (MSYS_GRABBED_OK);
}

//	MSYS_ReleaseMouse
//	Releases a previously grabbed mouse region
// FUNCTION: WIZ8 0x0040c010
void MSYS_ReleaseMouse(MOUSE_REGION* region)
{
    if (mouse.grabbed != region)
        return;

    if (mouse.grabbed != nullptr) {
        mouse.grabbed = nullptr;
        MSYS_UpdateMouseRegion();
    }
}

// FUNCTION: WIZ8 0x0040c040
void SetRegionFastHelpText(MOUSE_REGION* region, std::string_view szText)
{
    Assert(region);

    //	region->FastHelpTimer = 0;
    if (!(region->uiFlags & MSYS_REGION_EXISTS)) {
        region->FastHelpText.clear();
        return;
        //AssertMsg( 0, FormatText( "Attempting to set fast help text, \"%s\" to an inactive region.", szText ) );
    }

    if (szText.empty()) {
        region->FastHelpText.clear();
        return; //blank (or clear)
    }

    region->FastHelpText = szText;

    // ATE: We could be replacing already existing, active text
    // so let's remove the region so it be rebuilt...

    region->uiFlags &= (~MSYS_GOT_BACKGROUND);
    region->uiFlags &= (~MSYS_FASTHELP_RESET);

    //region->FastHelpTimer = mouse.help_delay;
}

// **********Wiz8 Versions**************************************************************************

void DisplayFastHelp(MOUSE_REGION* region)
{
    INT32 iX, iY, iW, iH;

    if (region->uiFlags & MSYS_FASTHELP) {
        VideoToolTip(region->FastHelpText);

        iW = VideoGetToolTipWidth();
        iH = VideoGetToolTipHeight();

        iX = (INT32)region->RegionTopLeftX + 10;

        if (iX < 0)
            iX = 0;

        if ((iX + iW) >= SCREEN_WIDTH)
            iX = (SCREEN_WIDTH - iW - 4);

        iY = (INT32)region->RegionTopLeftY - (iH * 3 / 4);
        if (iY < 0)
            iY = 0;

        if ((iY + iH) >= SCREEN_HEIGHT)
            iY = (SCREEN_HEIGHT - iH - 15);

        VideoPositionToolTip(iX, iY);
    }
}

// FUNCTION: WIZ8 0x0040c0b0
void RenderFastHelp()
{
    // GLOBAL: WIZ8 0x00650e68
    static int64_t iLastClock;
    int64_t iTimeDifferential, iCurrentClock;

    if (!gfRenderHilights)
        return;

    iCurrentClock = int64_t(w8_get_ticks());
    iTimeDifferential = iCurrentClock - iLastClock;
    iLastClock = iCurrentClock;

    if (mouse.current && !mouse.current->FastHelpText.empty() && mouse.show_help) {
        if (!mouse.current->FastHelpTimer) {
            if (mouse.current->uiFlags & (MSYS_ALLOW_DISABLED_FASTHELP | MSYS_REGION_ENABLED)) {
                if (mouse.current->uiFlags & MSYS_MOUSE_IN_AREA) {
                    mouse.current->uiFlags |= MSYS_FASTHELP;
                    DisplayFastHelp(mouse.current);
                } else {
                    mouse.current->uiFlags &= (~(MSYS_FASTHELP | MSYS_FASTHELP_RESET));
                    VideoRemoveToolTip();
                }
            }
        } else {
            if (mouse.current->uiFlags & (MSYS_ALLOW_DISABLED_FASTHELP | MSYS_REGION_ENABLED)) {
                if (mouse.current->uiFlags & MSYS_MOUSE_IN_AREA &&
                    !mouse.current->ButtonState) // & (MSYS_LEFT_BUTTON|MSYS_RIGHT_BUTTON)) )
                {
                    mouse.current->FastHelpTimer -= (INT16)std::max(iTimeDifferential, int64_t(0));

                    if (mouse.current->FastHelpTimer < 0) {
                        mouse.current->FastHelpTimer = 0;
                    }
                }
            }
        }
    }
}

// new stuff to allow mouse callbacks when help text finishes displaying

// FUNCTION: WIZ8 0x0040c1f0
void SetFastHelpDelay(INT16 sFastHelpDelay)
{
    mouse.help_delay = sFastHelpDelay;
}

// FUNCTION: WIZ8 0x0040c200
void EnableMouseFastHelp(void)
{
    mouse.show_help = true;
}

// FUNCTION: WIZ8 0x0040c210
void DisableMouseFastHelp(void)
{
    mouse.show_help = false;
}

// FUNCTION: WIZ8 0x0040c220
void ResetClickedMode(void)
{
    mouse.clicked = nullptr;
}

_MOUSE_REGION::~_MOUSE_REGION()
{
    reset();
    unlink();
}

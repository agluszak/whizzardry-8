#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

//	MouseSystem.c
//	Routines for handling prioritized mouse regions. The system as setup below allows the use of
//	callback functions for each region, as well as allowing a different cursor to be defined for
//	each region.
//	Written by Bret Rowdon, Jan 30 '97
//  Re-Written by Kris Morness, since...

#include "Types.h"
#include "compat/kernel32.h"
#include <stdio.h>
#include <memory.h>
#include <algorithm>
#include <vector>
#include "input.h"
#include "timer.h"
#include "line.h"
#include "Video2.h"
#define BASE_REGION_FLAGS MSYS_REGION_ENABLED
#include "english.h"
// Include mouse system defs and macros
#include "mousesystem.h"
#include "Button System.h"

//Kris:	Nov 31, 1999 -- Added support for double clicking
//Max double click delay (in milliseconds) to be considered a double click
#define MSYS_DOUBLECLICK_DELAY 400
//Records and stores the last place the user clicked.  These values are compared to the current
//click to determine if a double click event has been detected.
// GLOBAL: WIZ8 0x00650E6C
MOUSE_REGION* gpRegionLastLButtonDown = nullptr;
// GLOBAL: WIZ8 0x00650E70
MOUSE_REGION* gpRegionLastLButtonUp = nullptr;
// GLOBAL: WIZ8 0x00650E74
UINT32 guiRegionLastLButtonDownTime = 0;

// number of lines in height help text will be

// GLOBAL: WIZ8 0x00650e7c
INT32 MSYS_CurrentID = MSYS_ID_SYSTEM;

// GLOBAL: WIZ8 0x00650e80
INT16 MSYS_CurrentMX = 0;
// GLOBAL: WIZ8 0x00650e82
INT16 MSYS_CurrentMY = 0;
// GLOBAL: WIZ8 0x00650e84
INT16 MSYS_CurrentButtons = 0;
// GLOBAL: WIZ8 0x00650e86
INT16 MSYS_Action = 0;

// GLOBAL: WIZ8 0x00650e88
BOOLEAN MSYS_SystemInitialized = FALSE;
// GLOBAL: WIZ8 0x00650e89
BOOLEAN MSYS_UseMouseHandlerHook = FALSE;

// GLOBAL: WIZ8 0x00650e8a
BOOLEAN MSYS_Mouse_Grabbed = FALSE;
// GLOBAL: WIZ8 0x00650e8c
MOUSE_REGION* MSYS_GrabRegion = nullptr;

// GLOBAL: WIZ8 0x006e4100
UINT16 gusClickedIDNumber;
// GLOBAL: WIZ8 0x00650e90
BOOLEAN gfClickedModeOn = FALSE;

// GLOBAL: WIZ8 0x00650e94
static std::vector<MOUSE_REGION*> MSYS_RegList;

// GLOBAL: WIZ8 0x00650e98
MOUSE_REGION* MSYS_PrevRegion = nullptr;
// GLOBAL: WIZ8 0x00650e9c
MOUSE_REGION* MSYS_CurrRegion = nullptr;

//When set, the fast help text will be instantaneous, if consecutive regions with help text are
//hilighted.  It is set, whenever the timer for the first help button expires, and the mode is
//cleared as soon as the cursor moves into no region or a region with no helptext.
BOOLEAN gfPersistantFastHelpMode;

// GLOBAL: WIZ8 0x005ff7c8
INT16 gsFastHelpDelay = 600; // In timer ticks
// GLOBAL: WIZ8 0x005ff7ca
BOOLEAN gfShowFastHelp = TRUE;

// help text is done, now execute callback, if there is one

//Kris:
//NOTE:  This doesn't really need to be here, however, it is a good indication that
//when an error appears here, that you need to go below to the init code and initialize the
//values there as well.  That's the only reason why I left this here.
// GLOBAL: WIZ8 0x005ff7d0
MOUSE_REGION MSYS_SystemBaseRegion = {MSYS_ID_SYSTEM,
                                      MSYS_PRIORITY_SYSTEM,
                                      BASE_REGION_FLAGS,
                                      -32767,
                                      -32767,
                                      32767,
                                      32767,
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      MSYS_NO_CALLBACK,
                                      MSYS_NO_CALLBACK,
                                      {0, 0, 0, 0},
                                      0,
                                      {},
                                      -1,
                                      MSYS_NO_CALLBACK};

// GLOBAL: WIZ8 0x00650ea0
BOOLEAN gfRefreshUpdate = FALSE;

//Kris:  December 3, 1997
//Special internal debugging utilities that will ensure that you don't attempt to delete
//an already deleted region.  It will also ensure that you don't create an identical region
//that already exists.
//TO REMOVE ALL DEBUG FUNCTIONALITY:  simply comment out MOUSESYSTEM_DEBUGGING definition

#ifdef MOUSESYSTEM_DEBUGGING
BOOLEAN gfIgnoreShutdownAssertions;
#endif

//	MSYS_Init
//	Initialize the mouse system.
// FUNCTION: WIZ8 0x0040b290
INT32 MSYS_Init(void)
{

#ifdef MOUSESYSTEM_DEBUGGING
    gfIgnoreShutdownAssertions = FALSE;
#endif
    if (!MSYS_RegList.empty())
        MSYS_TrashRegList();

    MSYS_CurrentID = MSYS_ID_SYSTEM;

    MSYS_CurrentMX = 0;
    MSYS_CurrentMY = 0;
    MSYS_CurrentButtons = 0;
    MSYS_Action = MSYS_NO_ACTION;

    MSYS_PrevRegion = nullptr;
    MSYS_SystemInitialized = TRUE;
    MSYS_UseMouseHandlerHook = FALSE;

    MSYS_Mouse_Grabbed = FALSE;
    MSYS_GrabRegion = nullptr;

    // Setup the system's background region
    MSYS_SystemBaseRegion.IDNumber = MSYS_ID_SYSTEM;
    MSYS_SystemBaseRegion.PriorityLevel = MSYS_PRIORITY_SYSTEM;
    MSYS_SystemBaseRegion.uiFlags = BASE_REGION_FLAGS;
    MSYS_SystemBaseRegion.RegionTopLeftX = -32767;
    MSYS_SystemBaseRegion.RegionTopLeftY = -32767;
    MSYS_SystemBaseRegion.RegionBottomRightX = 32767;
    MSYS_SystemBaseRegion.RegionBottomRightY = 32767;
    MSYS_SystemBaseRegion.MouseXPos = 0;
    MSYS_SystemBaseRegion.MouseYPos = 0;
    MSYS_SystemBaseRegion.RelativeXPos = 0;
    MSYS_SystemBaseRegion.RelativeYPos = 0;
    MSYS_SystemBaseRegion.ButtonState = 0;
    MSYS_SystemBaseRegion.UserData[0] = 0;
    MSYS_SystemBaseRegion.UserData[1] = 0;
    MSYS_SystemBaseRegion.UserData[2] = 0;
    MSYS_SystemBaseRegion.UserData[3] = 0;
    MSYS_SystemBaseRegion.MovementCallback = MSYS_NO_CALLBACK;
    MSYS_SystemBaseRegion.ButtonCallback = MSYS_NO_CALLBACK;

    MSYS_SystemBaseRegion.FastHelpTimer = 0;
    MSYS_SystemBaseRegion.FastHelpText = 0;
    MSYS_SystemBaseRegion.FastHelpRect = -1;


    // Add the base region to the list
    MSYS_AddRegionToList(&MSYS_SystemBaseRegion);

#ifdef _MOUSE_SYSTEM_HOOK_
    MSYS_UseMouseHandlerHook = TRUE;
#endif

    return (1);
}

//	MSYS_Shutdown
//	De-inits the "mousesystem" mouse region handling code.
// FUNCTION: WIZ8 0x0040b450
void MSYS_Shutdown(void)
{
#ifdef MOUSESYSTEM_DEBUGGING
    gfIgnoreShutdownAssertions = TRUE;
#endif
    MSYS_SystemInitialized = FALSE;
    MSYS_UseMouseHandlerHook = FALSE;
    MSYS_TrashRegList();
}

//	MSYS_SGP_Mouse_Handler_Hook
//	Hook to the SGP's mouse handler
// FUNCTION: WIZ8 0x0040b510
void MSYS_SGP_Mouse_Handler_Hook(UINT16 Type, UINT16 Xcoord, UINT16 Ycoord, BOOLEAN LeftButton,
                                 BOOLEAN RightButton)
{
    // If the mouse system isn't initialized, get out o' here
    if (!MSYS_SystemInitialized)
        return;

    // If we're not using the handler stuff, ignore this call
    if (!MSYS_UseMouseHandlerHook)
        return;

    MSYS_Action = MSYS_NO_ACTION;
    switch (Type) {
    case LEFT_BUTTON_DOWN:
    case LEFT_BUTTON_UP:
    case RIGHT_BUTTON_DOWN:
    case RIGHT_BUTTON_UP:
        //MSYS_Action|=MSYS_DO_BUTTONS;
        if (Type == LEFT_BUTTON_DOWN)
            MSYS_Action |= MSYS_DO_LBUTTON_DWN;
        else if (Type == LEFT_BUTTON_UP) {
            MSYS_Action |= MSYS_DO_LBUTTON_UP;
            //Kris:
            //Used only if applicable.  This is used for that special button that is locked with the
            //mouse press -- just like windows.  When you release the button, the previous state
            //of the button is restored if you released the mouse outside of it's boundaries.  If
            //you release inside of the button, the action is selected -- but later in the code.
            //NOTE:  It has to be here, because the mouse can be released anywhere regardless of
            //regions, buttons, etc.
        } else if (Type == RIGHT_BUTTON_DOWN)
            MSYS_Action |= MSYS_DO_RBUTTON_DWN;
        else if (Type == RIGHT_BUTTON_UP)
            MSYS_Action |= MSYS_DO_RBUTTON_UP;

        if (LeftButton)
            MSYS_CurrentButtons |= MSYS_LEFT_BUTTON;
        else
            MSYS_CurrentButtons &= (~MSYS_LEFT_BUTTON);

        if (RightButton)
            MSYS_CurrentButtons |= MSYS_RIGHT_BUTTON;
        else
            MSYS_CurrentButtons &= (~MSYS_RIGHT_BUTTON);

        if ((Xcoord != MSYS_CurrentMX) || (Ycoord != MSYS_CurrentMY)) {
            MSYS_Action |= MSYS_DO_MOVE;
            MSYS_CurrentMX = Xcoord;
            MSYS_CurrentMY = Ycoord;
        }

        MSYS_UpdateMouseRegion();
        break;

    // ATE: Checks here for mouse button repeats.....
    // Call mouse region with new reason
    case LEFT_BUTTON_REPEAT:
    case RIGHT_BUTTON_REPEAT:

        if (Type == LEFT_BUTTON_REPEAT)
            MSYS_Action |= MSYS_DO_LBUTTON_REPEAT;
        else if (Type == RIGHT_BUTTON_REPEAT)
            MSYS_Action |= MSYS_DO_RBUTTON_REPEAT;

        if ((Xcoord != MSYS_CurrentMX) || (Ycoord != MSYS_CurrentMY)) {
            MSYS_Action |= MSYS_DO_MOVE;
            MSYS_CurrentMX = Xcoord;
            MSYS_CurrentMY = Ycoord;
        }

        MSYS_UpdateMouseRegion();
        break;

    case MOUSE_POS:
        if ((Xcoord != MSYS_CurrentMX) || (Ycoord != MSYS_CurrentMY) || gfRefreshUpdate) {
            MSYS_Action |= MSYS_DO_MOVE;
            MSYS_CurrentMX = Xcoord;
            MSYS_CurrentMY = Ycoord;

            gfRefreshUpdate = FALSE;

            MSYS_UpdateMouseRegion();
        }
        break;

    default:
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "ERROR -- MSYS 2 SGP Mouse Hook got bad type");
        break;
    }
}

//	MSYS_GetNewID
//	Returns a unique ID number for region nodes. If no new ID numbers can be found, the MAX value
//	is returned.
INT32 MSYS_GetNewID(void)
{
    for (UINT32 attempt = 0; attempt <= MSYS_ID_MAX; ++attempt) {
        const auto id = MSYS_CurrentID;
        MSYS_CurrentID = id == MSYS_ID_MAX ? MSYS_ID_BASE : id + 1;
        if (std::none_of(MSYS_RegList.begin(), MSYS_RegList.end(),
                         [id](const MOUSE_REGION* region) { return region->IDNumber == id; }))
            return id;
    }
    AssertMsg(FALSE, "Mouse region IDs exhausted");
    return MSYS_ID_MAX;
}

//	MSYS_TrashRegList
//	Deletes the entire region list.
void MSYS_TrashRegList(void)
{
    while (!MSYS_RegList.empty()) {
        auto* region = MSYS_RegList.back();
        if (region->uiFlags & MSYS_REGION_EXISTS)
            MSYS_RemoveRegion(region);
        else
            MSYS_RegList.pop_back();
    }
}

//	MSYS_AddRegionToList
//	Add a region struct to the current list. The list is sorted by priority levels. If two entries
//	have the same priority level, then the latest to enter the list gets the higher priority.
// FUNCTION: WIZ8 0x0040b720
void MSYS_AddRegionToList(MOUSE_REGION* region)
{
    if (MSYS_RegionInList(region))
        MSYS_DeleteRegionFromList(region);
    region->IDNumber = (UINT16)MSYS_GetNewID();
    const auto position = std::lower_bound(MSYS_RegList.begin(), MSYS_RegList.end(), region,
        [](const MOUSE_REGION* existing, const MOUSE_REGION* added) {
            return existing->PriorityLevel > added->PriorityLevel;
        });
    MSYS_RegList.insert(position, region);
}

//	MSYS_RegionInList
//	Scan region list for presence of a node with the same region ID number
INT32 MSYS_RegionInList(MOUSE_REGION* region)
{
    return std::find(MSYS_RegList.begin(), MSYS_RegList.end(), region) != MSYS_RegList.end();
}

//	MSYS_DeleteRegionFromList
//	Removes a region from the current list.
// FUNCTION: WIZ8 0x0040b830
void MSYS_DeleteRegionFromList(MOUSE_REGION* region)
{
    if (!std::erase(MSYS_RegList, region))
        return;

    // Did we delete a grabbed region?
    if (MSYS_Mouse_Grabbed) {
        if (MSYS_GrabRegion == region) {
            MSYS_Mouse_Grabbed = FALSE;
            MSYS_GrabRegion = nullptr;
        }
    }

    // Is only the system background region remaining?
    if (MSYS_RegList.size() == 1 && MSYS_RegList.front() == &MSYS_SystemBaseRegion) {
        // Yup, so let's reset the ID values!
        MSYS_CurrentID = MSYS_ID_BASE;
    } else if (MSYS_RegList.empty()) {
        // Ack, we actually emptied the list, so let's reset for re-init possibilities
        MSYS_CurrentID = MSYS_ID_SYSTEM;
    }
}

//	MSYS_UpdateMouseRegion
//	Searches the list for the highest priority region and updates it's info. It also dispatches
//	the callback functions
// FUNCTION: WIZ8 0x0040b900
void MSYS_UpdateMouseRegion(void)
{
    INT32 found;
    UINT32 ButtonReason;
    found = FALSE;

    // Check previous region!
    if (MSYS_Mouse_Grabbed) {
        MSYS_CurrRegion = MSYS_GrabRegion;
        found = TRUE;
    }
    if (!found) {
        const auto region = std::find_if(MSYS_RegList.begin(), MSYS_RegList.end(),
            [](const MOUSE_REGION* candidate) {
                return (candidate->uiFlags & (MSYS_REGION_ENABLED | MSYS_ALLOW_DISABLED_FASTHELP)) &&
                    candidate->RegionTopLeftX <= MSYS_CurrentMX &&
                    candidate->RegionTopLeftY <= MSYS_CurrentMY &&
                    candidate->RegionBottomRightX >= MSYS_CurrentMX &&
                    candidate->RegionBottomRightY >= MSYS_CurrentMY;
            });
        found = region != MSYS_RegList.end();
        MSYS_CurrRegion = found ? *region : nullptr;
    }

    if (MSYS_PrevRegion) {
        MSYS_PrevRegion->uiFlags &= (~MSYS_MOUSE_IN_AREA);

        if (MSYS_PrevRegion != MSYS_CurrRegion) {
            //Remove the help text for the previous region if one is currently being displayed.
            if (MSYS_PrevRegion->FastHelpText) {
                //ExecuteMouseHelpEndCallBack( MSYS_PrevRegion );

                MSYS_PrevRegion->uiFlags &= (~MSYS_GOT_BACKGROUND);
                MSYS_PrevRegion->uiFlags &= (~MSYS_FASTHELP_RESET);

                // dirty buttons, need a re-render
                //DEF: Nov 30 98
                //				PausedMarkButtonsDirty( );

                //if( region->uiFlags & MSYS_REGION_ENABLED )
                //	region->uiFlags |= BUTTON_DIRTY;
                VideoRemoveToolTip();
            }

            if (MSYS_CurrRegion)
                MSYS_CurrRegion->FastHelpTimer = gsFastHelpDelay;

            // Force a callbacks to happen on previous region to indicate that
            // the mouse has left the old region
            if (MSYS_PrevRegion->uiFlags & MSYS_MOVE_CALLBACK &&
                MSYS_PrevRegion->uiFlags & MSYS_REGION_ENABLED)
                (*(MSYS_PrevRegion->MovementCallback))(MSYS_PrevRegion,
                                                       MSYS_CALLBACK_REASON_LOST_MOUSE);
        }
    }

    // If a region was found in the list, update it's data
    if (found && MSYS_CurrRegion) {
        if (MSYS_CurrRegion != MSYS_PrevRegion) {
            //Kris -- October 27, 1997
            //Implemented gain mouse region
            if (MSYS_CurrRegion->uiFlags & MSYS_MOVE_CALLBACK) {
                if (MSYS_CurrRegion->FastHelpText &&
                    !(MSYS_CurrRegion->uiFlags & MSYS_FASTHELP_RESET)) {
                    //ExecuteMouseHelpEndCallBack( MSYS_CurrRegion );
                    MSYS_CurrRegion->FastHelpTimer = gsFastHelpDelay;
                    MSYS_CurrRegion->uiFlags &= (~MSYS_GOT_BACKGROUND);
                    MSYS_CurrRegion->uiFlags |= MSYS_FASTHELP_RESET;

                    VideoRemoveToolTip();

                    //if( b->uiFlags & BUTTON_ENABLED )
                    //	b->uiFlags |= BUTTON_DIRTY;
                }
                if (MSYS_CurrRegion->uiFlags & MSYS_REGION_ENABLED) {
                    (*(MSYS_CurrRegion->MovementCallback))(MSYS_CurrRegion,
                                                           MSYS_CALLBACK_REASON_GAIN_MOUSE);
                    if (!MSYS_CurrRegion)
                        return;
                }
            }

        }

        // OK, if we do not have a button down, any button is game!
        if (!gfClickedModeOn ||
            (gfClickedModeOn && gusClickedIDNumber == MSYS_CurrRegion->IDNumber)) {
            MSYS_CurrRegion->uiFlags |= MSYS_MOUSE_IN_AREA;

            MSYS_CurrRegion->MouseXPos = MSYS_CurrentMX;
            MSYS_CurrRegion->MouseYPos = MSYS_CurrentMY;
            MSYS_CurrRegion->RelativeXPos = MSYS_CurrentMX - MSYS_CurrRegion->RegionTopLeftX;
            MSYS_CurrRegion->RelativeYPos = MSYS_CurrentMY - MSYS_CurrRegion->RegionTopLeftY;

            MSYS_CurrRegion->ButtonState = MSYS_CurrentButtons;

            if (MSYS_CurrRegion->uiFlags & MSYS_REGION_ENABLED &&
                MSYS_CurrRegion->uiFlags & MSYS_MOVE_CALLBACK && MSYS_Action & MSYS_DO_MOVE) {
                (*(MSYS_CurrRegion->MovementCallback))(MSYS_CurrRegion, MSYS_CALLBACK_REASON_MOVE);
                if (!MSYS_CurrRegion)
                    return;
            }

            //ExecuteMouseHelpEndCallBack( MSYS_CurrRegion );
            //MSYS_CurrRegion->FastHelpTimer = gsFastHelpDelay;

            MSYS_Action &= (~MSYS_DO_MOVE);

            if ((MSYS_CurrRegion->uiFlags & MSYS_BUTTON_CALLBACK) &&
                (MSYS_Action & MSYS_DO_BUTTONS)) {
                if (MSYS_CurrRegion->uiFlags & MSYS_REGION_ENABLED) {
                    ButtonReason = MSYS_CALLBACK_REASON_NONE;
                    if (MSYS_Action & MSYS_DO_LBUTTON_DWN) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_DWN;
                        gfClickedModeOn = TRUE;
                        // Set global ID
                        gusClickedIDNumber = MSYS_CurrRegion->IDNumber;
                    }

                    if (MSYS_Action & MSYS_DO_LBUTTON_UP) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_UP;
                        gfClickedModeOn = FALSE;
                    }

                    if (MSYS_Action & MSYS_DO_RBUTTON_DWN) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_DWN;
                        gfClickedModeOn = TRUE;
                        // Set global ID
                        gusClickedIDNumber = MSYS_CurrRegion->IDNumber;
                    }

                    if (MSYS_Action & MSYS_DO_RBUTTON_UP) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_UP;
                        gfClickedModeOn = FALSE;
                    }

                    // ATE: Added repeat resons....
                    if (MSYS_Action & MSYS_DO_LBUTTON_REPEAT) {
                        ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_REPEAT;
                    }

                    if (MSYS_Action & MSYS_DO_RBUTTON_REPEAT) {
                        ButtonReason |= MSYS_CALLBACK_REASON_RBUTTON_REPEAT;
                    }

                    if (ButtonReason != MSYS_CALLBACK_REASON_NONE) {
                        if (MSYS_CurrRegion->uiFlags & MSYS_FASTHELP) {
                            // Button was clicked so remove any FastHelp text
                            MSYS_CurrRegion->uiFlags &= (~MSYS_FASTHELP);
                            MSYS_CurrRegion->uiFlags &= (~MSYS_GOT_BACKGROUND);

                            //ExecuteMouseHelpEndCallBack( MSYS_CurrRegion );
                            MSYS_CurrRegion->FastHelpTimer = gsFastHelpDelay;
                            MSYS_CurrRegion->uiFlags &= (~MSYS_FASTHELP_RESET);

                            //if( b->uiFlags & BUTTON_ENABLED )
                            //	b->uiFlags |= BUTTON_DIRTY;
                            VideoRemoveToolTip();
                        }

                        //Kris: Nov 31, 1999 -- Added support for double click events.
                        //This is where double clicks are checked and passed down.
                        if (ButtonReason == MSYS_CALLBACK_REASON_LBUTTON_DWN) {
                            UINT32 uiCurrTime = GetTickCount();
                            if (gpRegionLastLButtonDown == MSYS_CurrRegion &&
                                gpRegionLastLButtonUp == MSYS_CurrRegion &&
                                uiCurrTime <=
                                    guiRegionLastLButtonDownTime +
                                        MSYS_DOUBLECLICK_DELAY) { //Sequential left click on same button within the maximum time allowed for a double click
                                //Double click check succeeded, set flag and reset double click globals.
                                ButtonReason |= MSYS_CALLBACK_REASON_LBUTTON_DOUBLECLICK;
                                gpRegionLastLButtonDown = nullptr;
                                gpRegionLastLButtonUp = nullptr;
                                guiRegionLastLButtonDownTime = 0;
                            } else { //First click, record time and region pointer (to check if 2nd click detected later)
                                gpRegionLastLButtonDown = MSYS_CurrRegion;
                                guiRegionLastLButtonDownTime = GetTickCount();
                            }
                        } else if (ButtonReason == MSYS_CALLBACK_REASON_LBUTTON_UP) {
                            UINT32 uiCurrTime = GetTickCount();
                            if (gpRegionLastLButtonDown == MSYS_CurrRegion &&
                                uiCurrTime <=
                                    guiRegionLastLButtonDownTime +
                                        MSYS_DOUBLECLICK_DELAY) { //Double click is Left down, then left up, then left down.  We have just detected the left up here (step 2).
                                gpRegionLastLButtonUp = MSYS_CurrRegion;
                            } else { //User released mouse outside of current button, so kill any chance of a double click happening.
                                gpRegionLastLButtonDown = nullptr;
                                gpRegionLastLButtonUp = nullptr;
                                guiRegionLastLButtonDownTime = 0;
                            }
                        }

                        (*(MSYS_CurrRegion->ButtonCallback))(MSYS_CurrRegion, ButtonReason);
                    }
                }
            }

            MSYS_Action &= (~MSYS_DO_BUTTONS);
        } else if (MSYS_CurrRegion->uiFlags & MSYS_REGION_ENABLED) {
            // OK here, if we have release a button, UNSET LOCK wherever you are....
            // Just don't give this button the message....
            if (MSYS_Action & MSYS_DO_RBUTTON_UP) {
                gfClickedModeOn = FALSE;
            }
            if (MSYS_Action & MSYS_DO_LBUTTON_UP) {
                gfClickedModeOn = FALSE;
            }

            // OK, you still want move messages however....
            MSYS_CurrRegion->uiFlags |= MSYS_MOUSE_IN_AREA;
            MSYS_CurrRegion->MouseXPos = MSYS_CurrentMX;
            MSYS_CurrRegion->MouseYPos = MSYS_CurrentMY;
            MSYS_CurrRegion->RelativeXPos = MSYS_CurrentMX - MSYS_CurrRegion->RegionTopLeftX;
            MSYS_CurrRegion->RelativeYPos = MSYS_CurrentMY - MSYS_CurrRegion->RegionTopLeftY;

            if ((MSYS_CurrRegion->uiFlags & MSYS_MOVE_CALLBACK) && (MSYS_Action & MSYS_DO_MOVE)) {
                (*(MSYS_CurrRegion->MovementCallback))(MSYS_CurrRegion, MSYS_CALLBACK_REASON_MOVE);
                if (!MSYS_CurrRegion)
                    return;
            }

            MSYS_Action &= (~MSYS_DO_MOVE);
        }
        MSYS_PrevRegion = MSYS_CurrRegion;
    } else
        MSYS_PrevRegion = nullptr;
}

//	MSYS_DefineRegion
//	Inits a MOUSE_REGION structure for use with the mouse system
// FUNCTION: WIZ8 0x0040be10
void MSYS_DefineRegion(MOUSE_REGION* region, UINT16 tlx, UINT16 tly, UINT16 brx, UINT16 bry,
                       INT8 priority, MOUSE_CALLBACK movecallback,
                       MOUSE_CALLBACK buttoncallback)
{
#ifdef MOUSESYSTEM_DEBUGGING
    if (region->uiFlags & MSYS_REGION_EXISTS)
        AssertMsg(0, "Attempting to define a region that already exists.");
#endif

    region->IDNumber = MSYS_ID_BASE;

    if (priority == MSYS_PRIORITY_AUTO)
        priority = MSYS_PRIORITY_BASE;
    else if (priority <= MSYS_PRIORITY_LOWEST)
        priority = MSYS_PRIORITY_LOWEST;
    else if (priority >= MSYS_PRIORITY_HIGHEST)
        priority = MSYS_PRIORITY_HIGHEST;

    region->PriorityLevel = priority;

    region->uiFlags = MSYS_NO_FLAGS;

    region->MovementCallback = movecallback;
    if (movecallback != MSYS_NO_CALLBACK)
        region->uiFlags |= MSYS_MOVE_CALLBACK;

    region->ButtonCallback = buttoncallback;
    if (buttoncallback != MSYS_NO_CALLBACK)
        region->uiFlags |= MSYS_BUTTON_CALLBACK;

    region->RegionTopLeftX = tlx;
    region->RegionTopLeftY = tly;
    region->RegionBottomRightX = brx;
    region->RegionBottomRightY = bry;

    region->MouseXPos = 0;
    region->MouseYPos = 0;
    region->RelativeXPos = 0;
    region->RelativeYPos = 0;
    region->ButtonState = 0;

    //Init fasthelp
    region->FastHelpText = nullptr;
    region->FastHelpTimer = 0;

    region->HelpDoneCallback = nullptr;

    //Add region to system list
    MSYS_AddRegionToList(region);
    region->uiFlags |= MSYS_REGION_ENABLED | MSYS_REGION_EXISTS;

    // Dirty our update flag
    gfRefreshUpdate = TRUE;
}

//	MSYS_RemoveRegion
//	Removes a region from the list, disables it, then calls the callback functions for
//	de-initialization.
// FUNCTION: WIZ8 0x0040bee0
void MSYS_RemoveRegion(MOUSE_REGION* region)
{
    if (!region) {
#ifdef MOUSESYSTEM_DEBUGGING
        if (gfIgnoreShutdownAssertions)
#endif
            return;
        AssertMsg(0, "Attempting to remove a NULL region.");
    }
#ifdef MOUSESYSTEM_DEBUGGING
    if (!(region->uiFlags & MSYS_REGION_EXISTS))
        AssertMsg(0, "Attempting to remove an already removed region.");
#endif

    // Get rid of the FastHelp text (if applicable)
    if (region->FastHelpText) {
        if (region->uiFlags & MSYS_FASTHELP)
            VideoRemoveToolTip();
        region->FastHelpText.reset();
    }
    region->FastHelpText = nullptr;

    MSYS_DeleteRegionFromList(region);

    //if the previous region is the one that we are deleting, reset the previous region
    if (MSYS_PrevRegion == region)
        MSYS_PrevRegion = nullptr;
    //if the current region is the one that we are deleting, then clear it.
    if (MSYS_CurrRegion == region)
        MSYS_CurrRegion = nullptr;
    if (gpRegionLastLButtonDown == region || gpRegionLastLButtonUp == region) {
        gpRegionLastLButtonDown = gpRegionLastLButtonUp = nullptr;
        guiRegionLastLButtonDownTime = 0;
    }

    //dirty our update flag
    gfRefreshUpdate = TRUE;

    // Check if this is a locked region, and unlock if so
    if (gfClickedModeOn) {
        // Set global ID
        if (gusClickedIDNumber == region->IDNumber) {
            gfClickedModeOn = FALSE;
        }
    }

    //clear all internal values (including the region exists flag)
    *region = {};
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
    if (index < 0 || index > 3) {
        CHAR8 str[80];
#ifdef MOUSESYSTEM_DEBUGGING
        if (gfIgnoreShutdownAssertions)
#endif
            return;
        sprintf(str, "Attempting MSYS_SetRegionUserData() with out of range index %d.", index);
        AssertMsg(0, str);
    }
    region->UserData[index] = userdata;
}

//	MSYS_GetRegionUserData
//	Retrieves one of the four user data entries in a mouse region
// FUNCTION: WIZ8 0x0040bfa0
INT32 MSYS_GetRegionUserData(MOUSE_REGION* region, INT32 index)
{
    if (index < 0 || index > 3) {
        CHAR8 str[80];
#ifdef MOUSESYSTEM_DEBUGGING
        if (gfIgnoreShutdownAssertions)
#endif
            return 0;
        sprintf(str, "Attempting MSYS_GetRegionUserData() with out of range index %d", index);
        AssertMsg(0, str);
    }
    return (region->UserData[index]);
}

//	MSYS_GrabMouse
//	Assigns all mouse activity to a region, effectively blocking any other region from having
//	control.
// FUNCTION: WIZ8 0x0040bfc0
INT32 MSYS_GrabMouse(MOUSE_REGION* region)
{
    if (!MSYS_RegionInList(region))
        return (MSYS_REGION_NOT_IN_LIST);

    if (MSYS_Mouse_Grabbed == TRUE)
        return (MSYS_ALREADY_GRABBED);

    MSYS_Mouse_Grabbed = TRUE;
    MSYS_GrabRegion = region;
    return (MSYS_GRABBED_OK);
}

//	MSYS_ReleaseMouse
//	Releases a previously grabbed mouse region
// FUNCTION: WIZ8 0x0040c010
void MSYS_ReleaseMouse(MOUSE_REGION* region)
{
    if (MSYS_GrabRegion != region)
        return;

    if (MSYS_Mouse_Grabbed == TRUE) {
        MSYS_Mouse_Grabbed = FALSE;
        MSYS_GrabRegion = nullptr;
        MSYS_UpdateMouseRegion();
    }
}

/* ==================================================================================
   MSYS_MoveMouseRegionTo( MOUSE_REGION *region, INT16 sX, INT16 sY)

	 Moves a Mouse region to X Y on the screen

*/

/* ==================================================================================
   MSYS_MoveMouseRegionBy( MOUSE_REGION *region, INT16 sDeltaX, INT16 sDeltaY)

	 Moves a Mouse region by sDeltaX sDeltaY on the screen

*/

// FUNCTION: WIZ8 0x0040c040
void SetRegionFastHelpText(MOUSE_REGION* region, CHAR16* szText)
{
    Assert(region);

    //	region->FastHelpTimer = 0;
    if (!(region->uiFlags & MSYS_REGION_EXISTS)) {
        region->FastHelpText.reset();
        return;
        //AssertMsg( 0, FormatString( "Attempting to set fast help text, \"%S\" to an inactive region.", szText ) );
    }

    if (!szText || !wcslen(szText)) {
        region->FastHelpText.reset();
        return; //blank (or clear)
    }

    // Allocate memory for the button's FastHelp text string...
    auto text = std::make_unique<CHAR16[]>(wcslen(szText) + 1);
    wcscpy(text.get(), szText);
    region->FastHelpText = std::move(text);

    // ATE: We could be replacing already existing, active text
    // so let's remove the region so it be rebuilt...

    region->uiFlags &= (~MSYS_GOT_BACKGROUND);
    region->uiFlags &= (~MSYS_FASTHELP_RESET);

    //region->FastHelpTimer = gsFastHelpDelay;
}

// **********Wiz8 Versions**************************************************************************

void DisplayFastHelp(MOUSE_REGION* region)
{
    INT32 iX, iY, iW, iH;

    if (region->uiFlags & MSYS_FASTHELP) {
        VideoToolTip(region->FastHelpText.get());

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
    static INT32 iLastClock;
    INT32 iTimeDifferential, iCurrentClock;

    if (!gfRenderHilights)
        return;

    iCurrentClock = GetTickCount();
    iTimeDifferential = iCurrentClock - iLastClock;
    if (iTimeDifferential < 0)
        iTimeDifferential += 0x7fffffff;
    iLastClock = iCurrentClock;

    if (MSYS_CurrRegion && MSYS_CurrRegion->FastHelpText && gfShowFastHelp) {
        if (!MSYS_CurrRegion->FastHelpTimer) {
            if (MSYS_CurrRegion->uiFlags & (MSYS_ALLOW_DISABLED_FASTHELP | MSYS_REGION_ENABLED)) {
                if (MSYS_CurrRegion->uiFlags & MSYS_MOUSE_IN_AREA) {
                    MSYS_CurrRegion->uiFlags |= MSYS_FASTHELP;
                    DisplayFastHelp(MSYS_CurrRegion);
                } else {
                    MSYS_CurrRegion->uiFlags &= (~(MSYS_FASTHELP | MSYS_FASTHELP_RESET));
                    VideoRemoveToolTip();
                }
            }
        } else {
            if (MSYS_CurrRegion->uiFlags & (MSYS_ALLOW_DISABLED_FASTHELP | MSYS_REGION_ENABLED)) {
                if (MSYS_CurrRegion->uiFlags & MSYS_MOUSE_IN_AREA &&
                    !MSYS_CurrRegion->ButtonState) // & (MSYS_LEFT_BUTTON|MSYS_RIGHT_BUTTON)) )
                {
                    MSYS_CurrRegion->FastHelpTimer -= (INT16)max(iTimeDifferential, 0);

                    if (MSYS_CurrRegion->FastHelpTimer < 0) {
                        MSYS_CurrRegion->FastHelpTimer = 0;
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
    gsFastHelpDelay = sFastHelpDelay;
}

// FUNCTION: WIZ8 0x0040c200
void EnableMouseFastHelp(void)
{
    gfShowFastHelp = TRUE;
}

// FUNCTION: WIZ8 0x0040c210
void DisableMouseFastHelp(void)
{
    gfShowFastHelp = FALSE;
}

// FUNCTION: WIZ8 0x0040c220
void ResetClickedMode(void)
{
    gfClickedModeOn = FALSE;
}

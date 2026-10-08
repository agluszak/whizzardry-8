#include "native/input_events.h"
#include "input.h"

#include <string.h>

LRESULT NativeInputWindowProcedure(HWND, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        KeyDown(UINT32(wparam), UINT32(lparam));
        gfSGPInputReceived = TRUE;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        KeyUp(UINT32(wparam), UINT32(lparam));
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    {
        gusMouseXPos = LOWORD(lparam);
        gusMouseYPos = HIWORD(lparam);
        gfSGPInputReceived = TRUE;
        if (message == WM_LBUTTONDOWN)
        {
            gfLeftButtonState = TRUE;
            QueueEvent(LEFT_BUTTON_DOWN, 0, UINT32(lparam));
        }
        else if (message == WM_LBUTTONUP)
        {
            gfLeftButtonState = FALSE;
            QueueEvent(LEFT_BUTTON_UP, 0, UINT32(lparam));
        }
        else if (message == WM_RBUTTONDOWN)
        {
            gfRightButtonState = TRUE;
            QueueEvent(RIGHT_BUTTON_DOWN, 0, UINT32(lparam));
        }
        else if (message == WM_RBUTTONUP)
        {
            gfRightButtonState = FALSE;
            QueueEvent(RIGHT_BUTTON_UP, 0, UINT32(lparam));
        }
        else if (gfTrackMousePos)
            QueueEvent(MOUSE_POS, 0, UINT32(lparam));
        break;
    }
    case WM_MOUSEWHEEL:
        QueueEvent(MOUSE_WHEEL, UINT32(wparam), UINT32(lparam));
        break;
    case WM_ACTIVATEAPP:
        if (!wparam)
        {
            /* SDL reports focus loss separately from key-up/button-up. Clear
               held input so a missed release cannot drive the next frame. */
            memset(gfKeyState, 0, sizeof(gfKeyState));
            gfShiftState = gfCtrlState = gfAltState = 0;
            gfLeftButtonState = gfRightButtonState = FALSE;
            guiLeftButtonRepeatTimer = guiRightButtonRepeatTimer = 0;
        }
        break;
    }
    return 0;
}

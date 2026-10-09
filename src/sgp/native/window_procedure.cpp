#include "native/input_events.h"
#include "platform_events.h"
#include "sgp.h"
#include "wiz8/engine_code/GameData.h"

LRESULT WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    static bool restore_pending = false;
    if (gfIgnoreMessages)
        return 0;
    NativeInputWindowProcedure(window, message, wparam, lparam);
    switch (message)
    {
    case WM_CLOSE:
        gfProgramIsRunning = FALSE;
        w8_native::post_quit(0);
        break;
    case WM_SIZE:
        if (LOWORD(lparam) && HIWORD(lparam))
            VideoResizeWindow();
        break;
    case WM_ACTIVATEAPP:
        if (wparam)
        {
            if (restore_pending)
            {
                if (!VideoInspectorIsEnabled())
                {
                    RestoreVideoManager();
                    RestoreVideoSurfaces();
                }
                MoveTimer(TIMER_RESUME);
            }
            gfApplicationActive = TRUE;
        }
        else
        {
            if (!VideoInspectorIsEnabled())
                SuspendVideoManager();
            MoveTimer(TIMER_SUSPEND);
            gfApplicationActive = FALSE;
            restore_pending = true;
        }
        break;
    }
    return 0;
}

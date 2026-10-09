#pragma once
#include "compat/platform.h"

/* The application dispatches translated SDL messages here. QueueEvent,
   KeyDown/KeyUp, key translation, strings and repeats remain recovered code. */
LRESULT NativeInputWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

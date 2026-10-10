#pragma once

#include "Button System.h"
#include "compat/ptr32.h"

/* SurRender's button userdata slot is an INT32. Wizardry stores an object
   pointer in that slot, so these two adapters keep the pointer/integer crossing
   here instead of at every dialog call site. SurRender's own interface stays
   integer-typed. */
inline void SetButtonUserDataPointer(INT32 button, void* data)
{
    MSYS_SetBtnUserData(button, 0, static_cast<INT32>(w8_ptr32_detail::store(data)));
}

template <typename T> inline T* GetButtonUserDataPointer(GUI_BUTTON* button)
{
    return static_cast<T*>(w8_ptr32_detail::load(static_cast<UINT32>(MSYS_GetBtnUserData(button, 0))));
}

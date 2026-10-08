#pragma once

#include "Button System.h"
#include "compat/ptr32.h"

/* SurRender's button userdata slot is an INT32. Wizardry stores an object
   pointer in that slot, so these two adapters keep the pointer/integer crossing
   here instead of at every dialog call site. SurRender's own interface stays
   integer-typed. */
inline void SetButtonUserDataPointer(INT32 button, void* data)
{
#if defined(WIZ8_NATIVE)
    MSYS_SetBtnUserData(button, 0, static_cast<INT32>(w8_ptr32_detail::store(data)));
#else
    MSYS_SetBtnUserData(
        button, 0,
        reinterpret_cast<INT32>(data)); // reinterpret-ok: SGP userdata slot carries the pointer
#endif
}

template <typename T> inline T* GetButtonUserDataPointer(GUI_BUTTON* button)
{
#if defined(WIZ8_NATIVE)
    return static_cast<T*>(w8_ptr32_detail::load(static_cast<UINT32>(MSYS_GetBtnUserData(button, 0))));
#else
    return reinterpret_cast<T*>(
        MSYS_GetBtnUserData(button, 0)); // reinterpret-ok: SGP userdata slot carries the pointer
#endif
}

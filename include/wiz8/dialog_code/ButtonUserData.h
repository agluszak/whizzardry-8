#pragma once

#include "Button System.h"

// Callback context is borrowed from the UI owner; dispatch does not retain it.
inline void SetButtonUserDataPointer(const GUIButton& button, void* data)
{
    if (button) button->callback_context = data;
}

template <typename T> inline T* GetButtonUserDataPointer(GUI_BUTTON* button)
{
    return button ? static_cast<T*>(button->callback_context) : nullptr;
}

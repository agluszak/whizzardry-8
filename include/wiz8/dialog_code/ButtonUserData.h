#pragma once

#include "Button System.h"

// The UI owner outlives its button; dispatch retains only the SGP button.
inline void SetButtonUserDataPointer(const GUIButton& button, void* data)
{
    if (button) button->callback_context = data;
}

template <typename T> inline T* GetButtonUserDataPointer(GUI_BUTTON* button)
{
    return button ? static_cast<T*>(button->callback_context) : nullptr;
}

#include "wiz8/dialog_code/ButtonUserData.h"

#include <stdio.h>

// Exercise the adapter against the same signed 32-bit slot SGP exposes.
// The full button implementation also depends on the platform/video shell.
static GUI_BUTTON button;
void MSYS_SetBtnUserData(INT32 number, INT32 index, INT32 data)
{
    button.UserData[index] = data;
}
INT32 MSYS_GetBtnUserData(GUI_BUTTON* source, INT32 index)
{
    return source->UserData[index];
}

int main()
{
    static_assert(sizeof(W8_PTR32(int)) == 4, "raw pointer slots retain their disk width");
    int value = 42;
    if (reinterpret_cast<uintptr_t>(&value) <= UINT32_MAX) {
        fprintf(stderr, "test needs an address above 4 GiB\n");
        return 1;
    }
    button.UserData[3] = -1;
    SetButtonUserDataPointer(0, &value);
    const INT32 handle = button.UserData[0];
    if (GetButtonUserDataPointer<int>(&button) != &value || button.UserData[3] != -1) {
        return 2;
    }
    SetButtonUserDataPointer(0, &value);
    if (button.UserData[0] != handle) {
        return 3;
    }
    W8_PTR32(int) slot;
    slot = &value;
    if (static_cast<int*>(slot) != &value || *static_cast<int*>(slot) != 42) {
        return 4;
    }
    SetButtonUserDataPointer(0, nullptr);
    slot = nullptr;
    if (button.UserData[0] != 0 || GetButtonUserDataPointer<int>(&button) != nullptr ||
        static_cast<int*>(slot) != nullptr) {
        return 5;
    }
    printf("pointer slots and button userdata round-trip full-width addresses\n");
    return 0;
}

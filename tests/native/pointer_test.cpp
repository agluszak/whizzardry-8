#include <catch2/catch_test_macros.hpp>
#include "wiz8/dialog_code/ButtonUserData.h"

#include <stdio.h>

// Exercise the adapter against the same signed 32-bit slot SGP exposes.
// The full button implementation also depends on the platform/video shell.
// This isolated stub never registers a mouse region. Actual registration and
// destruction are exercised by owner_test, which links the game core.
_MOUSE_REGION::~_MOUSE_REGION() = default;
static GUI_BUTTON button;
void MSYS_SetBtnUserData(INT32 number, INT32 index, INT32 data)
{
    button.UserData[index] = data;
}
INT32 MSYS_GetBtnUserData(GUI_BUTTON* source, INT32 index)
{
    return source->UserData[index];
}

TEST_CASE("pointer_test")
{    static_assert(sizeof(W8_PTR32(int)) == 4, "raw pointer slots retain their disk width");
    int value = 42;
    REQUIRE(reinterpret_cast<uintptr_t>(&value) > UINT32_MAX);
    button.UserData[3] = -1;
    SetButtonUserDataPointer(0, &value);
    const INT32 handle = button.UserData[0];
    REQUIRE(GetButtonUserDataPointer<int>(&button) == &value);
    REQUIRE(button.UserData[3] == -1);
    SetButtonUserDataPointer(0, &value);
    REQUIRE(button.UserData[0] == handle);
    W8_PTR32(int) slot;
    slot = &value;
    REQUIRE(static_cast<int*>(slot) == &value);
    REQUIRE(*static_cast<int*>(slot) == 42);
    SetButtonUserDataPointer(0, nullptr);
    slot = nullptr;
    REQUIRE(button.UserData[0] == 0);
    REQUIRE(GetButtonUserDataPointer<int>(&button) == nullptr);
    REQUIRE(static_cast<int*>(slot) == nullptr);
}

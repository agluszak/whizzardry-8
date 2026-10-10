#include <catch2/catch_test_macros.hpp>
#include "wiz8/dialog_code/ButtonUserData.h"

#include <stdio.h>

// Exercise the adapter against the same signed 32-bit slot SGP exposes.
// The full button implementation also depends on the platform/video shell.
GUI_BUTTON::~_GUI_BUTTON() = default;
static GUIButton button = std::make_shared<GUI_BUTTON>();
void MSYS_SetBtnUserData(const GUIButton& target, INT32 index, INT32 data)
{
    target->UserData[index] = data;
}
INT32 MSYS_GetBtnUserData(GUI_BUTTON* source, INT32 index)
{
    return source->UserData[index];
}

TEST_CASE("pointer_test")
{    static_assert(sizeof(W8_PTR32(int)) == 4, "raw pointer slots retain their disk width");
    int value = 42;
    REQUIRE(reinterpret_cast<uintptr_t>(&value) > UINT32_MAX);
    button->UserData[3] = -1;
    SetButtonUserDataPointer(button, &value);
    const INT32 handle = button->UserData[0];
    REQUIRE(GetButtonUserDataPointer<int>(button.get()) == &value);
    REQUIRE(button->UserData[3] == -1);
    SetButtonUserDataPointer(button, &value);
    REQUIRE(button->UserData[0] == handle);
    W8_PTR32(int) slot;
    slot = &value;
    REQUIRE(static_cast<int*>(slot) == &value);
    REQUIRE(*static_cast<int*>(slot) == 42);
    SetButtonUserDataPointer(button, nullptr);
    slot = nullptr;
    REQUIRE(button->UserData[0] == 0);
    REQUIRE(GetButtonUserDataPointer<int>(button.get()) == nullptr);
    REQUIRE(static_cast<int*>(slot) == nullptr);
}

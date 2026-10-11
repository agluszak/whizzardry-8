#include <catch2/catch_test_macros.hpp>
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "compat/ptr32.h"
#include "Font.h"
#include "vobject.h"
#include "mousesystem.h"

TEST_CASE("pointer_test")
{
    REQUIRE(MSYS_Init());
    {
        W8DialogBase first_owner, second_owner;
        W8DialogButton button;
        Font font{SGPVObject{}};
        REQUIRE(button.ConfigureTextButton({}, font, 0, 0, 0, 0, 10, 10, nullptr, 123));
        button.m_owner = &first_owner;
        REQUIRE(button.m_owner == &first_owner);
        button.m_owner = &second_owner;
        REQUIRE(button.m_owner == &second_owner);
        REQUIRE(button.GetUserData() == 123);
        button.Reset();
        REQUIRE(button.m_owner == nullptr);
        REQUIRE(button.GetUserData() == 0);
    }
    MSYS_Shutdown();

    // Disk pointer representations remain independent of UI callback storage.
    static_assert(sizeof(W8_PTR32(int)) == 4);
    int first = 42;
    W8_PTR32(int) slot;
    slot = &first;
    REQUIRE(static_cast<int*>(slot) == &first);
    slot = nullptr;
    REQUIRE(static_cast<int*>(slot) == nullptr);
}

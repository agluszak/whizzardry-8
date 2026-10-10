#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "wiz8/dialog_code/ButtonUserData.h"
#include "compat/ptr32.h"
#include "Button System.h"

// This test links only the callback adapter, not the platform/video shell.
// These stubs never register a mouse region; owner_test covers real teardown.
_MOUSE_REGION::~_MOUSE_REGION() = default;
GUI_BUTTON::~_GUI_BUTTON() = default;

TEST_CASE("pointer_test")
{
    auto button = std::make_shared<GUI_BUTTON>();
    int first = 42, second = 7;
    button->UserData[0] = 123;
    SetButtonUserDataPointer(button, &first);
    REQUIRE(GetButtonUserDataPointer<int>(button.get()) == &first);
    SetButtonUserDataPointer(button, &second);
    REQUIRE(GetButtonUserDataPointer<int>(button.get()) == &second);
    REQUIRE(button->UserData[0] == 123);
    SetButtonUserDataPointer(button, nullptr);
    REQUIRE(GetButtonUserDataPointer<int>(button.get()) == nullptr);
    REQUIRE(GetButtonUserDataPointer<int>(nullptr) == nullptr);
    SetButtonUserDataPointer({}, &first);

    // Disk pointer representations remain independent of UI callback storage.
    static_assert(sizeof(W8_PTR32(int)) == 4);
    W8_PTR32(int) slot;
    slot = &first;
    REQUIRE(static_cast<int*>(slot) == &first);
    slot = nullptr;
    REQUIRE(static_cast<int*>(slot) == nullptr);
}

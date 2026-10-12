#include <cstdint>
#include <fmt/format.h>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>

#include "wiz8/utility.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/xstatus.h"
#include "wiz8/layouts/character.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/fonts.h"
#include "wiz8/sr_api.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "input.h"
#include "wiz8/regions.h"
#include "random.h"
#include "wiz8/layouts/game_status.h"
#include "Font.h"
#include "compat/native.h"
#include "conditions.h"
#include "dice.h"

struct MessageBoxState {
    W8ButtonWidget background;
    W8ButtonWidget accept;
    W8ButtonWidget cancel;
    std::optional<ExclusiveRegionSet> input_scope;

    std::int32_t state = 0;
    void (*callback)(void) = nullptr;
    Font* font = nullptr;
    std::uint32_t shade = 0;
    bool accepted = false;
    bool has_accept = false;
    bool has_cancel = false;
};

static std::optional<MessageBoxState> g_message_box;
// GLOBAL: WIZ8 0x0068BFD0
// GLOBAL: WIZ8 0x00689FD0

static SDL_Rect MessageBoxBounds(const MessageBoxState& message_box)
{
    auto bounds = message_box.background.GetBounds();
    const auto include = [&bounds](SDL_Rect control) {
        const std::int32_t left = std::min(bounds.x, control.x);
        const std::int32_t top = std::min(bounds.y, control.y);
        const std::int32_t right = std::max(bounds.x + bounds.w, control.x + control.w);
        const std::int32_t bottom = std::max(bounds.y + bounds.h, control.y + control.h);
        bounds = {left, top, right - left, bottom - top};
    };

    if (message_box.accept.IsConfigured()) include(message_box.accept.GetBounds());
    if (message_box.cancel.IsConfigured()) include(message_box.cancel.GetBounds());
    return bounds;
}

static void SetMessageBoxResult(bool accepted)
{
    if (!g_message_box) return;
    g_message_box->state = 1;
    g_message_box->accepted = accepted;
}

// FUNCTION: WIZ8 0x00517950
void SetDice(W8Dice* dice, std::uint8_t count, std::uint8_t sides, std::int16_t base)
{
    dice->count = count;
    dice->sides = sides;
    dice->base = base;
}

// FUNCTION: WIZ8 0x00517970
std::int32_t RollDice(const W8Dice* dice)
{
    std::uint32_t roll;
    std::int32_t result = dice->base;

    for (roll = 0; roll < dice->count; ++roll) {
        result += Random(dice->sides) + 1;
    }
    return result;
}

std::int32_t RollDice(const W8Dice* dice, std::uint32_t rolls)
{
    std::int32_t result = 0;
    while (rolls != 0) {
        result += RollDice(dice);
        --rolls;
    }
    return result;
}

// FUNCTION: WIZ8 0x005179b0
std::int32_t IntegerPower(std::int32_t base, std::uint32_t exponent)
{
    std::int32_t result;

    for (result = 1; exponent > 0; --exponent) {
        result *= base;
    }
    return result;
}

// FUNCTION: WIZ8 0x005179d0
void ClampInteger(std::int32_t* value, std::int32_t minimum, std::int32_t maximum)
{
    std::int32_t current = *value;

    if (current > maximum) {
        *value = maximum;
    } else if (current < minimum) {
        *value = minimum;
    }
}

// FUNCTION: WIZ8 0x005179f0
void ClampUnsignedInteger(std::uint32_t* value, std::uint32_t minimum, std::uint32_t maximum)
{
    if (*value > maximum) {
        *value = maximum;
    } else if (*value < minimum) {
        *value = minimum;
    }
}

// FUNCTION: WIZ8 0x00517a10
std::int32_t CompareUnsignedDescending(const void* first, const void* second)
{
    std::uint32_t left = *static_cast<const std::uint32_t*>(first);
    std::uint32_t right = *static_cast<const std::uint32_t*>(second);

    if (left > right) {
        return -1;
    }
    if (left < right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517a30
std::int32_t CompareSignedAscending(const void* first, const void* second)
{
    std::int32_t left = *static_cast<const std::int32_t*>(first);
    std::int32_t right = *static_cast<const std::int32_t*>(second);

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517a50
std::int32_t CompareSignedDescending(const void* first, const void* second)
{
    std::int32_t left = *static_cast<const std::int32_t*>(first);
    std::int32_t right = *static_cast<const std::int32_t*>(second);

    if (left > right) {
        return -1;
    }
    if (left < right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517af0
std::string FormatUnsignedIntegerWithCommas(std::uint32_t value)
{
    auto text = std::to_string(value);
    for (std::size_t i = text.size(); i > 3; ) { i -= 3; text.insert(i, ","); }
    return text;
}

// FUNCTION: WIZ8 0x00517bd0
char* TitleCaseString(char* string)
{
    bool capitalize = true;
    char* cursor = string;

    while (*cursor != '\0') {
        if (capitalize) {
            *cursor = static_cast<char>(toupper(*cursor));
        } else {
            *cursor = static_cast<char>(tolower(*cursor));
        }

        switch (*cursor) {
        case ' ':
        case '&':
        case '\'':
        case '(':
        case '*':
        case '-':
        case '.':
        case '2':
        case '?':
            capitalize = true;
            break;
        default:
            capitalize = false;
            break;
        }
        ++cursor;
    }
    return string;
}

// FUNCTION: WIZ8 0x00517c60
float NormalizeAngle(float angle)
{
    if (!_finite(angle)) {
        srAssertFail("_finite(flAngle)",
                     "C:\\Projects\\Wizardry 8\\Local Code\\UtilityFunctions.cpp", 0x13b, 0);
    }

    angle += 6.2831852;
    while (angle < 0.0f) {
        angle += 6.2831852f;
    }
    while (angle >= 6.2831852f) {
        angle -= 6.2831852f;
    }
    return angle;
}

// FUNCTION: WIZ8 0x00517ce0
float ShortestAngleDistance(float first, float second)
{
    float forward = NormalizeAngle(first - second);
    float backward = NormalizeAngle(second - first);

    return forward < backward ? forward : backward;
}

// FUNCTION: WIZ8 0x00517e20
W8ScreenRect UnionScreenRects(const W8ScreenRect& first, const W8ScreenRect& second)
{
    return {std::min(first.left, second.left), std::min(first.top, second.top),
            std::max(first.right, second.right), std::max(first.bottom, second.bottom)};
}

// FUNCTION: WIZ8 0x00517e70
bool ScreenPointInRect(const W8ScreenRect* rect, const SDL_Point* point)
{
    if (rect == nullptr || point == nullptr)
        return false;
    const SDL_Rect bounds{rect->left, rect->top, rect->right - rect->left,
                        rect->bottom - rect->top};
    return SDL_PointInRect(point, &bounds);
}

// FUNCTION: WIZ8 0x00517ea0
void StripMonsterNameSuffix(std::string& name)
{
    if (const auto suffix = name.find('#'); suffix != std::string::npos) name.resize(suffix);
}

// FUNCTION: WIZ8 0x00517ec0
std::uint32_t CharacterPointerToPartySlot(const W8Character* character)
{
    std::uint32_t slot;
    const W8Character* party_character;

    if (!character->fInParty) {
        srAssertFail("pPC->fInParty", "C:\\Projects\\Wizardry 8\\Local Code\\UtilityFunctions.cpp",
                     0x1c8, "PCPtrToPCSlot: ERROR - called for non-party character");
    }

    party_character = g_status.buffers.Char;
    for (slot = 0; slot < 8; ++slot, ++party_character) {
        if (character == party_character) {
            return slot;
        }
    }

    srAssertFail("FALSE", "C:\\Projects\\Wizardry 8\\Local Code\\UtilityFunctions.cpp", 0x1d1,
                 fmt::format("PCPtrToPCSlot: ERROR - no match on ptr {}",
                             static_cast<const void*>(character)));
    return 0;
}

// FUNCTION: WIZ8 0x00517f30
bool IsPartyCharacterPointer(const W8Character* character)
{
    W8Character* party_character = g_status.buffers.Char;
    std::uint32_t slot;

    for (slot = 0; slot < 8; ++slot, ++party_character) {
        if (character == party_character) {
            return true;
        }
    }

    return false;
}

// FUNCTION: WIZ8 0x00517f60
void AdjustByteByPercent(std::uint8_t* value, std::uint32_t percent)
{
    *value = static_cast<std::uint8_t>(((percent + 100) * *value + 50) / 100);
}

// FUNCTION: WIZ8 0x00517f90
void AdjustIntegerByPercent(std::uint32_t* value, std::uint32_t percent)
{
    *value += *value * percent / 100;
}

// FUNCTION: WIZ8 0x00517FB0
std::uint32_t GetRandomPartySlots(std::int32_t require_primary, std::int32_t require_secondary,
                                 std::uint32_t excluded_slot, std::uint32_t* selected,
                                 std::uint32_t count, bool skip_first_two)
{
    std::int32_t claimed[8];
    std::uint32_t eligible[8];
    std::uint32_t found = 0;
    std::uint32_t returned = 0;
    std::uint32_t slot;
    bool relaxed;
    std::int32_t index;

    for (index = 0; index < 8; ++index) {
        claimed[index] = 0;
    }
    if (count != 0) {
        for (relaxed = false; !relaxed;) {
            for (slot = skip_first_two ? 2u : 0u; slot < 8; ++slot) {
                W8Character* character = &g_status.buffers.Char[slot];
                if (g_status.buffers.XChar[slot].fOccupied && slot != excluded_slot &&
                    claimed[slot] == 0 && (character->hp_current != 0 || require_primary == 2) &&
                    (character->highest_condition < W8_CONDITION_DEAD || require_secondary == 2)) {
                    eligible[found] = slot;
                    ++found;
                    claimed[slot] = 1;
                }
            }
            if (found >= count) {
                break;
            }
            if (require_secondary == 1) {
                require_secondary = 2;
            } else if (require_primary == 1) {
                require_primary = 2;
            } else {
                relaxed = true;
            }
        }
        if (found > count) {
            for (index = 0; index < 8; ++index) {
                claimed[index] = 0;
            }
            while (returned < count && found != 0) {
                std::uint32_t pick = Random(found);
                while (pick < 8 && claimed[pick] != 0) {
                    ++pick;
                }
                if (pick >= 8) {
                    break;
                }
                selected[returned] = eligible[pick];
                ++returned;
                claimed[pick] = 1;
                --found;
            }
            return returned;
        }
    }
    memcpy(selected, eligible, found * 4);
    return found;
}

// FUNCTION: WIZ8 0x00518150
std::int32_t GetRandomCharacter(std::int32_t require_primary, std::int32_t require_secondary, std::int32_t excluded_slot,
                       std::int8_t excluded_gender)
{
    std::int32_t skip;
    std::uint32_t slot;
    std::uint32_t scanned;
    bool matched;
    W8Character* character;

retry:
    skip = Random(8);
    scanned = 0;
    slot = 0;
    do {
        matched = false;
        if (g_status.buffers.XChar[slot].fOccupied && static_cast<std::int32_t>(slot) != excluded_slot) {
            character = &g_status.buffers.Char[slot];
            if ((character->hp_current > 0 && character->highest_condition < W8_CONDITION_DEAD) ||
                require_primary == 2) {
                if (excluded_gender == -1 || excluded_gender != character->gender) {
                    if (character->highest_condition < W8_CONDITION_ASLEEP ||
                        require_secondary == 2) {
                        matched = true;
                        if (skip == 0) {
                            return slot;
                        }
                        skip--;
                        scanned = 0;
                    }
                }
            }
        }
        slot++;
        if (slot == 8) {
            slot = 0;
        }
        scanned++;
    } while (scanned <= 8);

    if (matched) {
        return slot;
    }
    if (require_secondary == 1) {
        require_secondary = 2;
        goto retry;
    }
    if (require_primary == 1) {
        require_primary = 2;
        goto retry;
    }
    return -1;
}

// FUNCTION: WIZ8 0x00518230
std::int32_t GetNextCharacter(std::int32_t require_primary, std::int32_t require_secondary, std::int32_t previous_slot)
{
    std::int32_t start_slot = (previous_slot + 1) % 8;
    W8Character* characters = g_status.buffers.Char;
    W8PartySlotRow* rows = g_status.buffers.XChar;
    std::int32_t slot;
    std::uint32_t scanned;

retry:
    slot = start_slot;
    scanned = 0;

    do {
        if (rows[slot].fOccupied) {
            W8Character* character = &characters[slot];

            if ((character->hp_current > 0 && character->highest_condition < W8_CONDITION_DEAD) ||
                require_primary == 2) {
                if (character->highest_condition < W8_CONDITION_ASLEEP || require_secondary == 2) {
                    return slot;
                }
            }
        }
        ++slot;
        if (slot == 8) {
            slot = 0;
        }
        ++scanned;
    } while (scanned <= 8);

    if (require_secondary == 1) {
        require_secondary = 2;
        goto retry;
    }
    if (require_primary == 1) {
        require_primary = 2;
        goto retry;
    }
    return -1;
}

// FUNCTION: WIZ8 0x005182e0
void DebugMessage(std::int32_t channel, std::string_view message)
{
    (void)channel; (void)message; // The recovered diagnostic sink is inactive.
}

// FUNCTION: WIZ8 0x00518310
std::int32_t RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc)
{
    std::int32_t slot = 0;

    for (const W8MonsterManagerEntry* current = gXStatus.monster_manager_entries; current < &gXStatus.monster_manager_entries[8]; ++current) {
        if (rpc == current) {
            return slot;
        }
        ++slot;
    }
    srAssertFail(
        "FALSE", "C:\\Projects\\Wizardry 8\\Local Code\\UtilityFunctions.cpp", 0x385,
        fmt::format("RPCPtrToPCSlot: ERROR - no match on ptr {}", static_cast<const void*>(rpc)));
    return 0;
}

// FUNCTION: WIZ8 0x005184b0
void FreeStringTable(void)
{
    ReleaseLocalizedStrings();
}

// FUNCTION: WIZ8 0x00518510
bool CreateMessageBox(std::string_view text, Font& font, std::uint32_t shade, bool has_accept, bool has_cancel,
                      void (*callback)(void))
{
    if (g_message_box) return false;

    auto& message_box = g_message_box.emplace();
    std::uint16_t height = 0x58;
    if (has_accept || has_cancel) {
        height = 0x6c;
    }
    std::uint16_t width = font.textWidth(text) + 10;
    if (width > 0x258) {
        width = 0x258;
    } else if (width < 0x64) {
        width = 0x78;
    }
    const std::int32_t y = (0x1e0 - height) / 2;
    const auto background_image = LoadGenericButtonImages(
        {}, DEFAULT_GENERIC_BUTTON_OFF, {}, DEFAULT_GENERIC_BUTTON_ON, {},
        "Data\\Dialogs\\DialogBackground.STI", 0, 0, 0);
    const auto fail_creation = [] {
        g_message_box.reset();
        return false;
    };

    if (!message_box.background.ConfigureTextButton(
            text, font, 0xff, 0, (0x280 - width) / 2, y, width, height,
            [](W8ButtonWidget*) { SetMessageBoxResult(true); },
            0, background_image, REGION_PRIORITY_BACKGROUND)) {
        return fail_creation();
    }
    message_box.background.SetMultiColor(true);

    message_box.has_accept = has_accept;
    message_box.has_cancel = has_cancel;
    if (has_accept) {
        if (!message_box.accept.Configure(
                "Data\\Message Box\\Ok.sti", 0, 1, 2, 3, 4,
                [](W8ButtonWidget*) { SetMessageBoxResult(true); },
                nullptr, false, REGION_PRIORITY_HIGHEST - 1,
                W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr)) {
            return fail_creation();
        }
        message_box.accept.SetPosition(0x131 - (has_cancel ? 0x1e : 0), y + 0x46);
    }
    if (has_cancel) {
        if (!message_box.cancel.Configure(
                "Data\\Message Box\\Cancel.sti", 0, 1, 2, 3, 4,
                [](W8ButtonWidget*) { SetMessageBoxResult(false); },
                nullptr, false, REGION_PRIORITY_HIGHEST - 1,
                W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr)) {
            return fail_creation();
        }
        message_box.cancel.SetPosition(0x131 + (has_accept ? 0x1e : 0), y + 0x46);
    }
    if (has_accept || has_cancel) {
        message_box.background.SetEnabled(false);
    }
    message_box.font = &font;
    message_box.shade = shade;
    message_box.state = 2;
    message_box.callback = callback;
    message_box.input_scope.emplace(ControlRegionSetIndex());
    const auto bounds = MessageBoxBounds(message_box);
    InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h, 0x11);
    return true;
}

static void ReleaseMessageBoxControls()
{
    if (!g_message_box) return;

    auto& message_box = *g_message_box;
    message_box.input_scope.reset();
    const auto bounds = MessageBoxBounds(message_box);
    message_box.accept.Reset();
    message_box.cancel.Reset();
    message_box.background.Reset();
    if (bounds.w > 0 && bounds.h > 0) {
        ClearSurfaceRect(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
        InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h, 1);
    }
}

/* Force-dismiss an open message box (state 2 -> 3): remove the buttons and
   images, clear and invalidate the background rect, and drop the accepted
   latch so RenderMessageBox never fires the callback. */
// FUNCTION: WIZ8 0x005187E0
void CloseMessageBox(void)
{
    if (g_message_box && g_message_box->state == 2) {
        ReleaseMessageBoxControls();
        g_message_box->state = 3;
        g_message_box->accepted = false;
    }
}

// FUNCTION: WIZ8 0x00518b20
bool IsMessageBoxActive(void)
{
    return g_message_box.has_value();
}

// FUNCTION: WIZ8 0x005189b0
void RenderMessageBox(void)
{
    if (!g_message_box) return;

    auto& message_box = *g_message_box;
    if (message_box.state == 1) {
        ReleaseMessageBoxControls();
        message_box.state = 3;
    } else if (message_box.state == 2) {
        Font& shade_font = message_box.font == &g_large_font ? g_large_font
            : message_box.font == &g_small_font ? g_small_font
            : message_box.font == &g_small_font_secondary ? g_small_font_secondary
            : message_box.font == &g_wiz_text_font ? g_wiz_text_font
            : g_wiz_text_font_secondary;
        shade_font.setShade(message_box.shade);
        message_box.background.Draw();
        if (message_box.has_accept) message_box.accept.Draw();
        if (message_box.has_cancel) message_box.cancel.Draw();
    } else if (message_box.state == 3) {
        const bool accepted = message_box.accepted;
        const auto callback = message_box.callback;
        g_message_box.reset();
        if (accepted && callback != nullptr) {
            callback();
        }
    }
}

// FUNCTION: WIZ8 0x00518b30
void ProcessMessageBoxInput(void)
{
    if (!g_message_box || g_message_box->state != 2) return;

    W8DialogBase::DrainModalInput([](const InputEvent& input) {
        switch (input.type) {
        case InputEventType::LeftButtonDown:
        case InputEventType::LeftButtonUp:
        case InputEventType::LeftButtonRepeat:
        case InputEventType::RightButtonDown:
        case InputEventType::RightButtonUp:
        case InputEventType::RightButtonRepeat:
            DispatchRegionInput(&input);
            break;
        case InputEventType::KeyDown:
            switch (toupper(input.key)) {
            case '\r':
            case ' ':
            case 'Y':
                g_message_box->state = 1;
                g_message_box->accepted = true;
                break;
            case 27:
            case 'N':
                g_message_box->accepted = !g_message_box->has_cancel;
                g_message_box->state = 1;
                break;
            }
            break;
        default:
            break;
        }
        return true;
    });
}

#include "wiz8/local_code/UtilityFunctions.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>
#include <fmt/format.h>

#include "compat/native.h"
#include "conditions.h"
#include "dice.h"
#include "random.h"
#include "wiz8/design_rect.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/sr_api.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

// FUNCTION: WIZ8 0x00517950
void SetDice(W8Dice* dice, unsigned char count, unsigned char sides, short base)
{
    dice->count = count;
    dice->sides = sides;
    dice->base = base;
}

// FUNCTION: WIZ8 0x00517970
int RollDice(const W8Dice* dice)
{
    unsigned int roll;
    int result = dice->base;

    for (roll = 0; roll < dice->count; ++roll) {
        result += Random(dice->sides) + 1;
    }
    return result;
}

int RollDice(const W8Dice* dice, unsigned int rolls)
{
    int result = 0;
    while (rolls != 0) {
        result += RollDice(dice);
        --rolls;
    }
    return result;
}

// FUNCTION: WIZ8 0x005179b0
int IntegerPower(int base, unsigned int exponent)
{
    int result;

    for (result = 1; exponent > 0; --exponent) {
        result *= base;
    }
    return result;
}

// FUNCTION: WIZ8 0x005179d0
void ClampInteger(int* value, int minimum, int maximum)
{
    int current = *value;

    if (current > maximum) {
        *value = maximum;
    } else if (current < minimum) {
        *value = minimum;
    }
}

// FUNCTION: WIZ8 0x005179f0
void ClampUnsignedInteger(unsigned int* value, unsigned int minimum, unsigned int maximum)
{
    if (*value > maximum) {
        *value = maximum;
    } else if (*value < minimum) {
        *value = minimum;
    }
}

// FUNCTION: WIZ8 0x00517a10
int CompareUnsignedDescending(const void* first, const void* second)
{
    unsigned int left = *static_cast<const unsigned int*>(first);
    unsigned int right = *static_cast<const unsigned int*>(second);

    if (left > right) {
        return -1;
    }
    if (left < right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517a30
int CompareSignedAscending(const void* first, const void* second)
{
    int left = *static_cast<const int*>(first);
    int right = *static_cast<const int*>(second);

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517a50
int CompareSignedDescending(const void* first, const void* second)
{
    int left = *static_cast<const int*>(first);
    int right = *static_cast<const int*>(second);

    if (left > right) {
        return -1;
    }
    if (left < right) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00517af0
std::string FormatUnsignedIntegerWithCommas(unsigned int value)
{
    auto text = std::to_string(value);
    for (std::size_t i = text.size(); i > 3;) {
        i -= 3;
        text.insert(i, ",");
    }
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
DesignRect UnionScreenRects(const DesignRect& first, const DesignRect& second)
{
    return {std::min(first.left, second.left), std::min(first.top, second.top),
            std::max(first.right, second.right), std::max(first.bottom, second.bottom)};
}

// FUNCTION: WIZ8 0x00517e70
bool ScreenPointInRect(const DesignRect* rect, const SDL_Point* point)
{
    if (rect == nullptr || point == nullptr) {
        return false;
    }
    const SDL_Rect bounds{rect->left, rect->top, rect->right - rect->left,
                          rect->bottom - rect->top};
    return SDL_PointInRect(point, &bounds);
}

// FUNCTION: WIZ8 0x00517ea0
void StripMonsterNameSuffix(std::string& name)
{
    if (const auto suffix = name.find('#'); suffix != std::string::npos) {
        name.resize(suffix);
    }
}

// FUNCTION: WIZ8 0x00517ec0
unsigned int CharacterPointerToPartySlot(const W8Character* character)
{
    unsigned int slot;
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
    unsigned int slot;

    for (slot = 0; slot < 8; ++slot, ++party_character) {
        if (character == party_character) {
            return true;
        }
    }

    return false;
}

// FUNCTION: WIZ8 0x00517f60
void AdjustByteByPercent(unsigned char* value, unsigned int percent)
{
    *value = static_cast<unsigned char>(((percent + 100) * *value + 50) / 100);
}

// FUNCTION: WIZ8 0x00517f90
void AdjustIntegerByPercent(unsigned int* value, unsigned int percent)
{
    *value += *value * percent / 100;
}

// FUNCTION: WIZ8 0x00517FB0
unsigned int GetRandomPartySlots(int require_primary, int require_secondary,
                                 unsigned int excluded_slot, unsigned int* selected,
                                 unsigned int count, bool skip_first_two)
{
    int claimed[8];
    unsigned int eligible[8];
    unsigned int found = 0;
    unsigned int returned = 0;
    unsigned int slot;
    bool relaxed;
    int index;

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
                unsigned int pick = Random(found);
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
int GetRandomCharacter(int require_primary, int require_secondary, int excluded_slot,
                       signed char excluded_gender)
{
    int skip;
    unsigned int slot;
    unsigned int scanned;
    bool matched;
    W8Character* character;

retry:
    skip = Random(8);
    scanned = 0;
    slot = 0;
    do {
        matched = false;
        if (g_status.buffers.XChar[slot].fOccupied && static_cast<int>(slot) != excluded_slot) {
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
int GetNextCharacter(int require_primary, int require_secondary, int previous_slot)
{
    int start_slot = (previous_slot + 1) % 8;
    W8Character* characters = g_status.buffers.Char;
    W8PartySlotRow* rows = g_status.buffers.XChar;
    int slot;
    unsigned int scanned;

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
void DebugMessage(int channel, std::string_view message)
{
    (void)channel;
    (void)message; // The recovered diagnostic sink is inactive.
}

// FUNCTION: WIZ8 0x00518310
int RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc)
{
    int slot = 0;

    for (const W8MonsterManagerEntry* current = gXStatus.monster_manager_entries;
         current < &gXStatus.monster_manager_entries[8]; ++current) {
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

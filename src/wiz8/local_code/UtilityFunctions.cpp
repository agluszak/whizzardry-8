#include "wiz8/local_code/UtilityFunctions.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>

#include "wiz8/combat/combat.h"
#include "wiz8/data/dice.h"
#include "wiz8/design_rect.h"
#include "wiz8/game.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/random.h"
#include "wiz8/utility.h"

void SetDice(W8Dice* dice, unsigned char count, unsigned char sides, short base)
{
    dice->count = count;
    dice->sides = sides;
    dice->base = base;
}

int RollDice(const W8Dice* dice)
{
    return RollDice(CurrentGame().rng, *dice);
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

int IntegerPower(int base, unsigned int exponent)
{
    int result;

    for (result = 1; exponent > 0; --exponent) {
        result *= base;
    }
    return result;
}

std::string FormatUnsignedIntegerWithCommas(unsigned int value)
{
    auto text = std::to_string(value);
    for (std::size_t i = text.size(); i > 3;) {
        i -= 3;
        text.insert(i, ",");
    }
    return text;
}

char* TitleCaseString(char* string)
{
    bool capitalize = true;
    char* cursor = string;

    while (*cursor != '\0') {
        if (capitalize) {
            *cursor = static_cast<char>(toupper(static_cast<unsigned char>(*cursor)));
        } else {
            *cursor = static_cast<char>(tolower(static_cast<unsigned char>(*cursor)));
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

float NormalizeAngle(float angle)
{
    assert(std::isfinite(angle));

    angle += 6.2831852;
    while (angle < 0.0f) {
        angle += 6.2831852f;
    }
    while (angle >= 6.2831852f) {
        angle -= 6.2831852f;
    }
    return angle;
}

float ShortestAngleDistance(float first, float second)
{
    float forward = NormalizeAngle(first - second);
    float backward = NormalizeAngle(second - first);

    return forward < backward ? forward : backward;
}

DesignRect UnionScreenRects(const DesignRect& first, const DesignRect& second)
{
    return {std::min(first.left, second.left), std::min(first.top, second.top),
            std::max(first.right, second.right), std::max(first.bottom, second.bottom)};
}

bool ScreenPointInRect(const DesignRect* rect, const SDL_Point* point)
{
    if (rect == nullptr || point == nullptr) return false;
    const SDL_Rect bounds{rect->left, rect->top, rect->right - rect->left,
                          rect->bottom - rect->top};
    return SDL_PointInRect(point, &bounds);
}

unsigned int CharacterPointerToPartySlot(const W8Character* character)
{
    unsigned int slot;
    const W8Character* party_character;

    assert(character->fInParty);

    party_character = CurrentGame().party.characters;
    for (slot = 0; slot < 8; ++slot, ++party_character) {
        if (character == party_character) {
            return slot;
        }
    }

    assert(false && "character is not in the party");
    return 0;
}

bool IsPartyCharacterPointer(const W8Character* character)
{
    W8Character* party_character = CurrentGame().party.characters;
    unsigned int slot;

    for (slot = 0; slot < 8; ++slot, ++party_character) {
        if (character == party_character) {
            return true;
        }
    }

    return false;
}

void AdjustByteByPercent(unsigned char* value, unsigned int percent)
{
    *value = static_cast<unsigned char>(((percent + 100) * *value + 50) / 100);
}

void AdjustIntegerByPercent(unsigned int* value, unsigned int percent)
{
    *value += *value * percent / 100;
}

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
                W8Character* character = &CurrentGame().party.characters[slot];
                if (CurrentGame().party.members[slot].has_value() && slot != excluded_slot &&
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
                unsigned int pick = CurrentGame().rng.below(found);
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
    std::copy_n(eligible, found, selected);
    return found;
}

int GetRandomCharacter(int require_primary, int require_secondary, int excluded_slot,
                       signed char excluded_gender)
{
    int skip;
    unsigned int slot;
    unsigned int scanned;
    bool matched;
    W8Character* character;

retry:
    skip = CurrentGame().rng.below(8);
    scanned = 0;
    slot = 0;
    do {
        matched = false;
        if (CurrentGame().party.members[slot].has_value() &&
            static_cast<int>(slot) != excluded_slot) {
            character = &CurrentGame().party.characters[slot];
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

int GetNextCharacter(int require_primary, int require_secondary, int previous_slot)
{
    int start_slot = (previous_slot + 1) % 8;
    W8Character* characters = CurrentGame().party.characters;
    int slot;
    unsigned int scanned;

retry:
    slot = start_slot;
    scanned = 0;

    do {
        if (CurrentGame().party.members[slot].has_value()) {
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

void DebugMessage(int channel, std::string_view message)
{
    (void)channel;
    (void)message; // The recovered diagnostic sink is inactive.
}

int RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc)
{
    int slot = 0;

    for (const W8MonsterManagerEntry* current = CurrentGame().monster_manager_entries;
         current < &CurrentGame().monster_manager_entries[8]; ++current) {
        if (rpc == current) {
            return slot;
        }
        ++slot;
    }
    assert(false && "entry is not a monster manager slot");
    return 0;
}

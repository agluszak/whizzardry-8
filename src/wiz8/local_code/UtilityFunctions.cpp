#include "wiz8/local_code/UtilityFunctions.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>

#include <SDL3/SDL_rect.h>

#include "wiz8/combat/combat.h"
#include "wiz8/data/dice.h"
#include "wiz8/design_rect.h"
#include "wiz8/game.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/rules/character.h"
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

void AdjustIntegerByPercent(unsigned int* value, unsigned int percent)
{
    *value += *value * percent / 100;
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

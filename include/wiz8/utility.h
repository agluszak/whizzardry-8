#ifndef WIZ8_UTILITY_H
#define WIZ8_UTILITY_H

#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>

#include "wiz8/application.h"
#include "wiz8/data/dice.h"
#include "wiz8/design_rect.h"
#include "wiz8/local_code/ControlsRect.h"

void SetDice(W8Dice* dice, unsigned char count, unsigned char sides, short base);
int RollDice(const W8Dice* dice);
int RollDice(const W8Dice* dice, unsigned int rolls);
int IntegerPower(int base, unsigned int exponent);
std::string FormatUnsignedIntegerWithCommas(unsigned int value);
std::string TitleCaseString(std::string_view text);
float ShortestAngleDistance(float first, float second);
DesignRect UnionScreenRects(const DesignRect& first, const DesignRect& second);
bool ScreenPointInRect(const DesignRect* rect, const SDL_Point* point);
void AdjustIntegerByPercent(unsigned int* value, unsigned int percent);
float NormalizeAngle(float angle);

int GetRandomCharacter(int require_primary, int require_secondary, int excluded_slot,
                       signed char excluded_gender);
/* 0x00517FB0: fill `selected` with up to `count` distinct party slots that pass
   the same eligibility gate as GetRandomCharacter, skipping `excluded_slot` and
   starting the scan at slot two when `skip_first_two` is set. More eligible
   slots than asked for are sampled at random; too few relaxes the requirements
   once each before giving up. Returns how many were written. */
unsigned int GetRandomPartySlots(int require_primary, int require_secondary,
                                 unsigned int excluded_slot, unsigned int* selected,
                                 unsigned int count, bool skip_first_two);

#endif

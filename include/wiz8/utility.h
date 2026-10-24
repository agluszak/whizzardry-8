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
char* TitleCaseString(char* string);
float ShortestAngleDistance(float first, float second);
DesignRect UnionScreenRects(const DesignRect& first, const DesignRect& second);
bool ScreenPointInRect(const DesignRect* rect, const SDL_Point* point);
void AdjustIntegerByPercent(unsigned int* value, unsigned int percent);
float NormalizeAngle(float angle);

#endif

#include <string_view>
#ifndef WIZ8_UTILITY_H
#define WIZ8_UTILITY_H

#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/application.h"

#include "wiz8/dice.h"
#include "wiz8/text_format.h"

bool IsMessageBoxActive(void);
void RenderMessageBox(void);
void ProcessMessageBoxInput(void);

template <class T> class srVector3T;

struct W8ScreenRect {
    int left;
    int top;
    int right;
    int bottom;
};

void SetDice(W8Dice* dice, unsigned char count, unsigned char sides, short base);
int RollDice(const W8Dice* dice);
int RollDice(const W8Dice* dice, unsigned int rolls);
int IntegerPower(int base, unsigned int exponent);
void ClampInteger(int* value, int minimum, int maximum);
void ClampUnsignedInteger(unsigned int* value, unsigned int minimum, unsigned int maximum);
int CompareUnsignedDescending(const void* first, const void* second);
int CompareSignedAscending(const void* first, const void* second);
int CompareSignedDescending(const void* first, const void* second);
template<class... Args>
std::string FormatText(std::string_view pattern, const Args&... args)
{
    return wiz8::text::format(pattern, args...);
}
std::string FormatUnsignedIntegerWithCommas(unsigned int value);
char* TitleCaseString(char* string);
float ShortestAngleDistance(float first, float second);
void UnionScreenRects(const W8ScreenRect* first, const W8ScreenRect* second, W8ScreenRect* result);
bool ScreenPointInRect(const W8ScreenRect* rect, const SDL_Point* point);
void AdjustByteByPercent(unsigned char* value, unsigned int percent);
void AdjustIntegerByPercent(unsigned int* value, unsigned int percent);
float NormalizeAngle(float angle);

void DebugMessage(int channel, std::string_view message);
template<class... Args> void FormatDebugMessage(int channel, std::string_view pattern, const Args&... args)
{ DebugMessage(channel, wiz8::text::format(pattern, args...)); }
/* The plain message reporter. ReadLevel.cpp uses it for load failures and
   GrCycle.cpp for a cycle with no usable LOD, so its name stays neutral. */
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

/* Shared "%d/%d" format literal; the definition is the GLOBAL in
   CGSStatsPage.cpp. */
/* Shared "%.1f" format literal; the definition is the GLOBAL in
   Dialog Code\AssayDialog.cpp. */

#endif

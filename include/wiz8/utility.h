#ifndef WIZ8_UTILITY_H
#define WIZ8_UTILITY_H

#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <string>
#include <string_view>

#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/application.h"

#include "wiz8/dice.h"

bool IsMessageBoxActive(void);
void RenderMessageBox(void);
void ProcessMessageBoxInput(void);

template <class T> class srVector3T;

struct W8ScreenRect {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

void SetDice(W8Dice* dice, std::uint8_t count, std::uint8_t sides, std::int16_t base);
std::int32_t RollDice(const W8Dice* dice);
std::int32_t RollDice(const W8Dice* dice, std::uint32_t rolls);
std::int32_t IntegerPower(std::int32_t base, std::uint32_t exponent);
void ClampInteger(std::int32_t* value, std::int32_t minimum, std::int32_t maximum);
void ClampUnsignedInteger(std::uint32_t* value, std::uint32_t minimum, std::uint32_t maximum);
std::int32_t CompareUnsignedDescending(const void* first, const void* second);
std::int32_t CompareSignedAscending(const void* first, const void* second);
std::int32_t CompareSignedDescending(const void* first, const void* second);
std::string FormatUnsignedIntegerWithCommas(std::uint32_t value);
char* TitleCaseString(char* string);
float ShortestAngleDistance(float first, float second);
W8ScreenRect UnionScreenRects(const W8ScreenRect& first, const W8ScreenRect& second);
bool ScreenPointInRect(const W8ScreenRect* rect, const SDL_Point* point);
void AdjustByteByPercent(std::uint8_t* value, std::uint32_t percent);
void AdjustIntegerByPercent(std::uint32_t* value, std::uint32_t percent);
float NormalizeAngle(float angle);

void DebugMessage(std::int32_t channel, std::string_view message);
/* The plain message reporter. ReadLevel.cpp uses it for load failures and
   GrCycle.cpp for a cycle with no usable LOD, so its name stays neutral. */
std::int32_t GetRandomCharacter(std::int32_t require_primary, std::int32_t require_secondary, std::int32_t excluded_slot,
                       std::int8_t excluded_gender);
/* 0x00517FB0: fill `selected` with up to `count` distinct party slots that pass
   the same eligibility gate as GetRandomCharacter, skipping `excluded_slot` and
   starting the scan at slot two when `skip_first_two` is set. More eligible
   slots than asked for are sampled at random; too few relaxes the requirements
   once each before giving up. Returns how many were written. */
std::uint32_t GetRandomPartySlots(std::int32_t require_primary, std::int32_t require_secondary,
                                 std::uint32_t excluded_slot, std::uint32_t* selected,
                                 std::uint32_t count, bool skip_first_two);


#endif

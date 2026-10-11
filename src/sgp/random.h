/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __RANDOM_
#define __RANDOM_

#include <cstdint>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void InitializeRandom(void);
extern std::uint32_t Random(std::uint32_t uiRange);

//Chance( 74 ) returns TRUE 74% of the time.  If uiChance >= 100, then it will always return TRUE.
extern bool Chance(std::uint32_t uiChance);

//Wizardry can use it too, but I'm saving them a K in the meantime...
//If Wizardry wants it, then removing the #ifdef JA2 will make it work.

#ifdef PRERANDOM_GENERATOR
//Returns a pregenerated random number.
//Used to deter Ian's tactic of shoot, miss, restore saved game :)
extern std::uint32_t PreRandom(std::uint32_t uiRange);
extern bool PreChance(std::uint32_t uiChance);

//IMPORTANT:  Changing this define will invalidate the JA2 save.  If this
//						is necessary, please ifdef your own value.
#define MAX_PREGENERATED_NUMS 256
extern std::uint32_t guiPreRandomIndex;
extern std::uint32_t guiPreRandomNums[MAX_PREGENERATED_NUMS];
#endif

#ifdef __cplusplus
}
#endif

#endif

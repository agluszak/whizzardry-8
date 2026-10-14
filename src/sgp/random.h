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

#ifdef __cplusplus
}
#endif

#endif

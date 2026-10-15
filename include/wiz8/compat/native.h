#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's one monotonic clock; runtime tests can substitute a virtual one. */
uint64_t w8_clock_us(void);
/* Milliseconds on the same clock, 64-bit so deadlines never wrap. */
uint64_t w8_get_ticks(void);

#ifdef __cplusplus
}
#endif

#ifndef WIZ8_LOCAL_CODE_NOISE_H
#include "glm/ext/vector_float3.hpp"
#define WIZ8_LOCAL_CODE_NOISE_H

/* Local Code\Noise.cpp. */
void AlertMonsterGroupsToNoise(const glm::vec3* position, int radius, int flag);
void AlertWorldNoise(void);
void AlertCombatNoise(bool large_radius);
/* radius - 25000 * hops - cost: whether a heard noise is still loud enough to
   be worth walking to once the path cost and hop count are paid for. */
int NoiseHearingMargin(int radius, int range, int hops);

#endif

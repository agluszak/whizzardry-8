#ifndef WIZ8_LOCAL_CODE_NOISE_H
#define WIZ8_LOCAL_CODE_NOISE_H


#include <cstdint>
template <class T> class srVector3T;

/* Local Code\Noise.cpp. */
void AlertMonsterGroupsToNoise(const srVector3T<float>* position, std::int32_t radius, std::int32_t flag);
void AlertWorldNoise(void);
void AlertCombatNoise(bool large_radius);
/* radius - 25000 * hops - cost: whether a heard noise is still loud enough to
   be worth walking to once the path cost and hop count are paid for. */
std::int32_t NoiseHearingMargin(std::int32_t radius, std::int32_t range, std::int32_t hops);

#endif

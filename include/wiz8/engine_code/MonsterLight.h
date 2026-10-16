#pragma once

#include "compat/compiler.h"
#include "surrender/srLight.h"
#include "surrender/srMath.h"

class srNode;

/* Monster's light: srLight plus colour-cycle/fade state. */
class MonsterLight : public srLight {
public:
    MonsterLight(srNode* parent, bool cycle_color, float range,
                 const srVector3T<float>* first_color,
                 const srVector3T<float>* second_color); /* 0x0049D500 */
    MonsterLight(const MonsterLight& other);             /* 0x0049D660 */
    void SetVisible(bool visible);
    void SetRange(float range);
    void Update(const srVector3T<float>* position);
    void StartFadeOut();

public:
    float m_vertical_offset;          /* 0x228 */
    srVector3T<float> m_color_first;  /* 0x22c */
    srVector3T<float> m_color_second; /* 0x238 */
    float m_start_time;               /* 0x244 */
    bool m_cycle_color;               /* 0x248 */
    bool m_fade_out;                  /* 0x249 */
    unsigned char m_padding_24a[6];
};

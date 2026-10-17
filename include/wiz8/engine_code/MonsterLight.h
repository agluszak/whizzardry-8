#pragma once

#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srLight.h"

class srNode;

/* Monster's light: srLight plus colour-cycle/fade state. */
class MonsterLight : public srLight {
public:
    MonsterLight(srNode* parent, bool cycle_color, float range, const glm::vec3* first_color,
                 const glm::vec3* second_color); /* 0x0049D500 */
    MonsterLight(const MonsterLight& other);             /* 0x0049D660 */
    void SetVisible(bool visible);
    void SetRange(float range);
    void Update(const glm::vec3* position);
    void StartFadeOut();

public:
    float m_vertical_offset;
    glm::vec3 m_color_first;
    glm::vec3 m_color_second;
    std::optional<float> m_start_time;
    bool m_cycle_color;
    bool m_fade_out;
};

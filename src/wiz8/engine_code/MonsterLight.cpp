#include "wiz8/engine_code/MonsterLight.h"

#include <cmath>
#include <string>

#include "glm/common.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srLight.h"
#include "surrender/srNode.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/gameplay_constants.h"

const float g_monster_light_cycle_rate = 0.025f;

/* Monster's fixed light is a regular srLight specialization.  Its two colours
   are retained for the optional cycle, while the first colour is also the
   initial renderer colour. Construction needs no gameplay clock; colour cycling
   starts when the light first participates in a live update. */
MonsterLight::MonsterLight(srNode* parent, bool cycle_color, float range,
                           const glm::vec3* first_color, const glm::vec3* second_color)
    : srLight(parent, srLight::PRESET_POINT), m_vertical_offset(0.0f), m_color_first(*first_color),
      m_color_second(*second_color), m_start_time(), m_cycle_color(cycle_color), m_fade_out(0)
{
    setName("MonFixedLight");
    attenuation_model = srLight::ATTENUATION_3DSTUDIO_MAX;
    enable_flags |= (1UL << srLight::ENABLE_RANGE_FAR);
    enable_flags |= (1UL << srLight::ENABLE_BOUNDING_SPHERE);
    far_end = range;
    near_start = 0.0;
    near_end = 0.0;
    far_start = 0.0;
    safe_range = 5000.0f;
    setLinearAttenuation(range, 0.0019569471f);
    specular = glm::vec3(0);
    diffuse = (*first_color);
    setFlag(srNode::FLAG_DISABLE);
}

/* A copied monster light preserves the authored light configuration and
   colour-cycle settings. The owning monster attaches it to its target scene
   and the first live update starts its interval. */
MonsterLight::MonsterLight(const MonsterLight& other)
    : srLight(other), m_vertical_offset(other.m_vertical_offset),
      m_color_first(other.m_color_first), m_color_second(other.m_color_second), m_start_time(),
      m_cycle_color(other.m_cycle_color), m_fade_out(false)
{
    intensity = 1.0f;
    setFlag(srNode::FLAG_DISABLE);
}

void MonsterLight::SetRange(float range)
{
    far_start = 0.0;
    far_end = range;
    setLinearAttenuation(range, 0.0019569471f);
}

void MonsterLight::SetVisible(bool visible)
{
    if (visible) {
        clearFlag(srNode::FLAG_DISABLE);
    } else {
        setFlag(srNode::FLAG_DISABLE);
    }
}

void MonsterLight::Update(const glm::vec3* position)
{
    const float now = GameTimeAccumulator().GetElapsed();
    if (!m_start_time) m_start_time = now;
    const float elapsed = now - *m_start_time;

    if (m_fade_out) {
        float fade = elapsed * 0.1f;
        if (fade > 1.0f) {
            fade = 1.0f;
        }
        intensity = 1.0f - fade;
    } else if (m_cycle_color) {
        float cycle = elapsed * g_monster_light_cycle_rate;
        double whole = floor(cycle);
        float first_weight =
            static_cast<float>(sin((cycle - whole) * kMotionFullTurnRadians) + 1.0f) * 0.5f;
        diffuse = glm::mix(m_color_second, m_color_first, first_weight);
    }

    glm::dvec3 location;
    location = glm::dvec3(position->x, position->y + m_vertical_offset, position->z);
    setLocation(location);
}

void MonsterLight::StartFadeOut()
{
    m_fade_out = true;
    m_start_time = GameTimeAccumulator().GetElapsed();
}

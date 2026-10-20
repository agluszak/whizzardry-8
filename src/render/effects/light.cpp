#include "wiz8/render/light.h"

#include <cassert>
#include <cmath>
#include <variant>

#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/matrix.hpp"
#include "wiz8/math.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vsg_scene.h"

Light::Light(WorldScene* scene, e_preset preset)
{
    m_save_marked = false;
    m_prop = nullptr;
    m_direction = 1;
    m_path_index = 0;
    m_path_direction = 1;
    m_position = glm::vec3(0);
    m_level = 0;
    m_level_time = m_path_time = MonotonicMilliseconds() * .001f;

    enable_flags = 0;
    enable_flags = 0;
    ambient = glm::vec3(0.0f);
    diffuse = glm::vec3(1.0f);
    specular = glm::vec3(1.0f);
    spot_direction = glm::vec3(0, 0, 1);
    spot_exponent = 1.0f;
    intensity = 1.0f;
    spot_angle = (float)(3.141592653589793 * 0.5);
    attenuation_model = ATTENUATION_OPENGL;
    opengl_attenuation = glm::vec3(1, 0, 0);
    near_start = 0.0;
    near_end = 0.0;
    far_start = 0.0;
    far_end = 1000.0;
    enable_flags |= (1UL << Light::ENABLE_RANGE_FAR);
    if (preset == PRESET_DIRECTIONAL) {
        enable_flags |= ((1UL << Light::ENABLE_DIRECTIONAL) | (1UL << Light::ENABLE_RANGE_FAR));
    } else if (preset == PRESET_SPOT) {
        enable_flags |= (1UL << Light::ENABLE_SPOT);
    }
    setScene(scene);
}

Light& Light::operator=(const Light& other)
{
    if (this != &other) {
        transform = other.transform;
        name = other.name;
        visible = other.visible;
        bake_into_mesh = other.bake_into_mesh;
        group_mask = other.group_mask;
        animator = other.animator;
        m_position = other.m_position;
        m_direction = other.m_direction;
        path_ai = other.path_ai;
        if (path_ai) path_ai->timed = false;
        m_path_index = other.m_path_index;
        m_path_direction = other.m_path_direction;
        m_level = other.m_level;
        m_level_time = m_path_time = MonotonicMilliseconds() * .001f;
        m_prop = other.m_prop;
        m_save_marked = other.m_save_marked;
        attenuation_model = other.attenuation_model;
        enable_flags = other.enable_flags;
        ambient = other.ambient;
        diffuse = other.diffuse;
        specular = other.specular;
        opengl_attenuation = other.opengl_attenuation;
        spot_direction = other.spot_direction;
        spot_angle = other.spot_angle;
        spot_exponent = other.spot_exponent;
        intensity = other.intensity;
        near_start = other.near_start;
        near_end = other.near_end;
        far_start = other.far_start;
        far_end = other.far_end;
    }
    return *this;
}

void Light::setLinearAttenuation(float range, float attenuation)
{
    if (attenuation <= 1e-06f) {
        attenuation = 1e-06f;
    }
    if (range <= 1e-06f) {
        range = 1e-06f;
    }
    opengl_attenuation.x = 1.0f;
    opengl_attenuation.z = 0.0f;
    opengl_attenuation.y = (1.0f - attenuation) / (range * attenuation);
}

void Light::setAmbient(const glm::vec3& ambient)
{
    this->ambient = ambient;
}

glm::vec3 Light::getAmbient() const
{
    return ambient;
}

void Light::setSpotDirection(const glm::vec3& direction)
{
    spot_direction = normalizedOrZero(direction);
}

glm::vec3 Light::getSpotDirection() const
{
    return spot_direction;
}

void Light::setIntensity(float intensity)
{
    this->intensity = intensity;
}

void Light::setAttenuation(const glm::vec3& attenuation)
{
    opengl_attenuation = glm::max(attenuation, glm::vec3(0.0001f));
}

glm::vec3 Light::getAttenuation() const
{
    return opengl_attenuation;
}

Light::~Light()
{
    setScene(nullptr);
}
Light::Light(const Light& other) : Light()
{
    *this = other;
    if (auto* monster = std::get_if<MonsterLightAnim>(&animator)) {
        monster->start_time.reset();
        monster->fade_out = false;
        intensity = 1;
        visible = false;
    }
}

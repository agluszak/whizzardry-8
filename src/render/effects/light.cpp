#include "wiz8/render/light.h"

#include <cassert>
#include <cmath>
#include <variant>

#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/matrix.hpp"
#include "wiz8/math.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/vertex_channels.h"

/* Derived-state bits: bit0 = pushed as an active vertex processor this frame, bit1 = spotlight cone
   active, bit2 = directional, bit3 = OpenGL attenuation active, bit4 = constant-only OpenGL
   attenuation, bit5 = far-range attenuation active. */

Light::Light(WorldScene* scene, e_preset preset)
{
    m_save_marked = false;
    m_prop = nullptr;
    m_direction = 1;
    m_path_index = 0;
    m_path_direction = 1;
    m_position = glm::vec3(0);
    m_level = 0;
    m_level_time = m_path_time = w8_get_ticks() * .001f;

    enable_flags = 0;
    derived_flags = 0;
    channel_mask = 0;
    enable_flags = 0;
    ambient = glm::vec3(0.0f);
    diffuse = glm::vec3(1.0f);
    specular = glm::vec3(1.0f);
    spot_direction = glm::vec3(0, 0, 1);
    spot_exponent = 1.0f;
    intensity = 1.0f;
    spot_angle = (float)(3.141592653589793 * 0.5);
    safe_range = 0.0f;
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
        m_level_time = m_path_time = w8_get_ticks() * .001f;
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
        safe_range = other.safe_range;
    }
    return *this;
}

void Light::beginPass(GpuRenderer& target, const DrawTransform& view)
{
    auto* renderer = &target;
    {
        const auto light_transform = view.withModel(transform.matrix<glm::mat4>());
        const auto& model_view = light_transform.model_view;
        derived_flags = Light::DERIVED_ACTIVE;
        channel_mask = 0;
        const struct Channel {
            const glm::vec3& color;
            glm::vec4& scaled;
            VertexChannels::e_channel channel;
        } channels[] = {{ambient, scaled_ambient, VertexChannels::CHANNEL_LIGHT_AMBIENT},
                        {diffuse, scaled_diffuse, VertexChannels::CHANNEL_LIGHT_DIFFUSE},
                        {specular, scaled_specular, VertexChannels::CHANNEL_SPECULAR}};
        for (const auto& channel : channels) {
            if (channel.color != glm::vec3(0)) {
                channel_mask |= 1UL << channel.channel;
                channel.scaled =
                    glm::vec4(channel.color * intensity * renderer->scene_brightness, 0);
            }
        }
        if (channel_mask == 0) {
            derived_flags &= ~Light::DERIVED_ACTIVE;
        }
        if ((enable_flags & (1UL << Light::ENABLE_DIRECTIONAL)) != 0) {
            derived_flags |= Light::DERIVED_DIRECTIONAL;
            eye_location = normalizedOrZero(-glm::vec3(model_view[2]));
        } else {
            if ((enable_flags & (1UL << Light::ENABLE_SPOT)) != 0 && spot_angle > 0.0f &&
                spot_exponent != 0.0f) {
                const auto inverse = light_transform.inverseModelView();
                derived_flags |= Light::DERIVED_SPOT;
                spot_cutoff = (float)cos(spot_angle);
                spot_direction_eye =
                    normalizedOrZero(glm::transpose(glm::mat3(inverse)) * spot_direction);
            }
            if (attenuation_model != ATTENUATION_NONE) {
                if (attenuation_model == ATTENUATION_OPENGL) {
                    derived_flags |= Light::DERIVED_OPENGL_ATTENUATION;
                    if (fabsf(opengl_attenuation.y) < 5.9604645e-08f &&
                        fabsf(opengl_attenuation.z) < 5.9604645e-08f) {
                        if (opengl_attenuation.x == 1.0f ||
                            fabsf(opengl_attenuation.x) < 5.9604645e-08f) {
                            derived_flags &= ~Light::DERIVED_OPENGL_ATTENUATION;
                        }
                        derived_flags |= Light::DERIVED_CONSTANT_ATTENUATION;
                    }
                } else {
                    float scale = light_transform.maxModelViewScale();
                    scaled_near_start = scale * (float)near_start;
                    scaled_far_end = scale * (float)far_end;
                    if (near_start == near_end) {
                        near_attenuation = 1.0f;
                    } else {
                        near_attenuation = (float)(1.0 / (scale * (near_end - near_start)));
                    }
                    if (far_start == far_end) {
                        far_attenuation = 1.0f;
                    } else {
                        far_attenuation = (float)(1.0 / (scale * (far_end - far_start)));
                    }
                    if ((enable_flags & (1UL << Light::ENABLE_RANGE_FAR)) != 0) {
                        if ((enable_flags & (1UL << Light::ENABLE_BOUNDING_SPHERE)) != 0) {
                            if (!light_transform.sphereVisible(glm::vec3(0),
                                                               safe_range + far_end)) {
                                derived_flags &= ~Light::DERIVED_ACTIVE;
                            }
                        }
                        attenuation_range = scaled_far_end;
                        derived_flags |= Light::DERIVED_RANGE_CULL;
                    }
                }
            }
            eye_location = glm::vec3(model_view[3]);
        }
    }
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

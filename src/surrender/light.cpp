#include "surrender/channel_math.h"
#include "surrender/lighting_math.h"

#include <algorithm>
#include <ostream>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/srLight.h"
#include "surrender/stream_scope.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srMath.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srIlluminator.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srVertexProcessor.h"

/* Derived-state bits: bit0 = pushed as an active vertex processor this frame, bit1 = spotlight cone
   active, bit2 = directional, bit3 = OpenGL attenuation active, bit4 = constant-only OpenGL
   attenuation, bit5 = far-range attenuation active. */

// FUNCTION: SURRENDER 0x1004DDA0
srLight::srLight(srNode* parent, e_preset preset)
{
    enable_flags = 0;
    derived_flags = 0;
    channel_mask = 0;
    setFlag(FLAG_GLOBAL);
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
    enable_flags |= (1UL << srLight::ENABLE_RANGE_FAR);
    if (preset == PRESET_DIRECTIONAL) {
        enable_flags |= ((1UL << srLight::ENABLE_DIRECTIONAL) | (1UL << srLight::ENABLE_RANGE_FAR));
    } else if (preset == PRESET_SPOT) {
        enable_flags |= (1UL << srLight::ENABLE_SPOT);
    }
    if (parent != 0) {
        setParent(parent, 0);
    }
}

// FUNCTION: SURRENDER 0x1004DFB0
srLight& srLight::operator=(const srLight& other)
{
    if (this != &other) {
        srIlluminator::operator=(other);
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

// GLOBAL: SURRENDER 0x100A49D0
// Comma-separated light flag-name table, unset on disk; dump prints flag
// indices when it is null.
static const char* const s_light_flag_names = 0;

// The label is the first output, so the column width pads it; keep any '{' in the label.
static void dumpRow(std::ostream& stream, const char* label, const auto& value)
{
    stream.width(0x20);
    stream << label << value << '\n';
}

static void dumpVectorRow(std::ostream& stream, const char* label, const glm::vec3& value)
{
    stream.width(0x20);
    stream << label << value.x << ',' << value.y << ',' << value.z << '}' << '\n';
}

// FUNCTION: SURRENDER 0x1004E0C0
void srLight::dump(std::ostream& stream)
{
    srNode::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Control flags: ";
    srDumpFlags(stream, enable_flags, s_light_flag_names);
    stream << '\n';
    dumpRow(stream, "  Intensity: ", intensity);
    dumpVectorRow(stream, "  Ambient coeff.: {", ambient);
    dumpVectorRow(stream, "  Diffuse coeff.: {", diffuse);
    dumpVectorRow(stream, "  Specular coeff.: {", specular);
    dumpVectorRow(stream, "  Spot direction: {", spot_direction);
    dumpRow(stream, "  Spot angle: ", spot_angle);
    dumpRow(stream, "  Spot exponent: ", spot_exponent);
    dumpRow(stream, "  Safe Range: ", safe_range);
    stream.width(0x20);
    stream << "  Attenuation model: ";
    if (attenuation_model == ATTENUATION_OPENGL) {
        stream << "OpenGL" << '\n';
        dumpVectorRow(stream, "  Attenuation fact.: {", opengl_attenuation);
    } else if (attenuation_model == ATTENUATION_3DSTUDIO_MAX) {
        stream << "3DStudio Max" << '\n';
        stream.width(0x20);
        stream << "  Near Range : " << near_start << " - " << near_end << '\n';
        stream.width(0x20);
        stream << "  Far Range : " << far_start << " - " << far_end << '\n';
    } else {
        stream << "No attenuation" << '\n';
    }
}

// FUNCTION: SURRENDER 0x1004D650
void srLight::process(const ProcessInfo& info, e_processType type)
{
    srGERD* renderer = info.renderer;
    if (type != PROCESS_PUSH && type != PROCESS_PUSH_GLOBAL) {
        if (type != PROCESS_POP && type != PROCESS_POP_GLOBAL) {
            return;
        }
        if ((derived_flags & srLight::DERIVED_ACTIVE) == 0) {
            return;
        }
        renderer->popVertexProcessor();
        return;
    }
    {
        const auto transform = scopedWorldSpaceMatrix(*renderer);
        const auto model_view = renderer->getMatrix();
        derived_flags = srLight::DERIVED_ACTIVE;
        channel_mask = 0;
        const struct Channel {
            const glm::vec3& color;
            glm::vec4& scaled;
            srVertexProcessor::e_channel channel;
        } channels[] = {
            {ambient, scaled_ambient, srVertexProcessor::CHANNEL_LIGHT_AMBIENT},
            {diffuse, scaled_diffuse, srVertexProcessor::CHANNEL_LIGHT_DIFFUSE},
            {specular, scaled_specular, srVertexProcessor::CHANNEL_SPECULAR}};
        for (const auto& channel : channels) {
            if (channel.color != glm::vec3(0)) {
                channel_mask |= 1UL << channel.channel;
                channel.scaled = glm::vec4(channel.color * intensity, 0);
            }
        }
        if (channel_mask == 0) {
            derived_flags &= ~srLight::DERIVED_ACTIVE;
        }
        if ((enable_flags & (1UL << srLight::ENABLE_DIRECTIONAL)) != 0) {
            derived_flags |= srLight::DERIVED_DIRECTIONAL;
            eye_location = normalizedOrZero(-glm::vec3(model_view[2]));
        } else {
            if ((enable_flags & (1UL << srLight::ENABLE_SPOT)) != 0 && spot_angle > 0.0f &&
                spot_exponent != 0.0f) {
                const auto inverse = renderer->getInverseModelViewMatrix();
                derived_flags |= srLight::DERIVED_SPOT;
                spot_cutoff = (float)cos(spot_angle);
                spot_direction_eye =
                    normalizedOrZero(glm::transpose(glm::mat3(inverse)) * spot_direction);
            }
            if (attenuation_model != ATTENUATION_NONE) {
                if (attenuation_model == ATTENUATION_OPENGL) {
                    derived_flags |= srLight::DERIVED_OPENGL_ATTENUATION;
                    if (fabsf(opengl_attenuation.y) < 5.9604645e-08f &&
                        fabsf(opengl_attenuation.z) < 5.9604645e-08f) {
                        if (opengl_attenuation.x == 1.0f ||
                            fabsf(opengl_attenuation.x) < 5.9604645e-08f) {
                            derived_flags &= ~srLight::DERIVED_OPENGL_ATTENUATION;
                        }
                        derived_flags |= srLight::DERIVED_CONSTANT_ATTENUATION;
                    }
                } else {
                    float scale = renderer->getMaxModelViewScale();
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
                    if ((enable_flags & (1UL << srLight::ENABLE_RANGE_FAR)) != 0) {
                        if ((enable_flags & (1UL << srLight::ENABLE_BOUNDING_SPHERE)) != 0) {
                            if (renderer->testBoundingSphere(glm::vec3(0), safe_range + far_end) == 0) {
                                derived_flags &= ~srLight::DERIVED_ACTIVE;
                            }
                        }
                        attenuation_range = scaled_far_end;
                        derived_flags |= srLight::DERIVED_RANGE_CULL;
                    }
                }
            }
            eye_location = glm::vec3(model_view[3]);
        }
    }
    if ((derived_flags & srLight::DERIVED_ACTIVE) != 0) {
        renderer->pushVertexProcessor(*this);
    }
}

// FUNCTION: SURRENDER 0x1004CCC0
int srLight::isActive(srVertexPipe& pipe)
{
    const srVertexPipe::Input* input = pipe.input;
    if ((group_mask & input->exclusion_mask) != 0) {
        return 0;
    }
    if ((derived_flags & srLight::DERIVED_RANGE_CULL) != 0) {
        const auto offset = eye_location - input->eye_bounds.center;
        float range = attenuation_range + input->eye_bounds.radius;
        if (range * range <= glm::dot(offset, offset)) {
            return 0;
        }
    }
    if ((derived_flags & srLight::DERIVED_SPOT) != 0) {
        float radius = input->eye_bounds.radius;
        const auto offset = input->eye_bounds.center - eye_location;
        const float distance_squared = glm::dot(offset, offset);
        if (radius * radius < distance_squared &&
            glm::dot(offset, spot_direction_eye) / sqrtf(distance_squared) +
                    radius / sqrtf(radius * radius + distance_squared) <
                spot_cutoff) {
            return 0;
        }
    }
    return 1;
}

// FUNCTION: SURRENDER 0x1004CE00
void srLight::process(srVertexPipe& pipe)
{
    w8_ulong channels = channel_mask & pipe.channel_mask;
    if (channels == 0) {
        return;
    }
    /* Diffuse and specular lighting both need normal dot products. */
    bool need_normals = false;
    if ((channels & (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE)) != 0 ||
        (channels & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) != 0) {
        need_normals = true;
    }
    SRDWORD count = pipe.vertex_count;
    float spot_factors[0x40];
    float attenuation_bank[0x40];
    float dots[0x40];
    float distances[0x40];
    glm::vec3 directions[0x40];
    const auto light_position = eye_location;
    std::span<float> attenuation;
    const std::span<float> attenuation_values(attenuation_bank, count);
    const std::span<float> spot_values(spot_factors, count);
    const std::span<float> dot_values(dots, count);
    const std::span<float> distance_values(distances, count);
    const std::span<glm::vec3> direction_values(directions, count);

    if ((derived_flags & srLight::DERIVED_DIRECTIONAL) == 0) {
        for (SRDWORD i = 0; i < count; ++i) {
            const auto sample = srLightingMath::directionAndDistance(light_position -
                glm::vec3(pipe.eye_space_locations[pipe.batch_base + pipe.sub_batch_offset + i]));
            directions[i] = sample.direction;
            distances[i] = sample.distance;
        }
        if (attenuation_model == ATTENUATION_3DSTUDIO_MAX) {
            if ((enable_flags & (1UL << srLight::ENABLE_RANGE_NEAR)) != 0) {
                srChannelMath::distanceRamp(attenuation_values, distance_values, scaled_near_start,
                                            near_attenuation, 1, srChannelMath::RampDirection::increasing);
                attenuation = attenuation_values;
            }
            if ((enable_flags & (1UL << srLight::ENABLE_RANGE_FAR)) != 0) {
                const bool has_near = !attenuation.empty();
                const auto far_values = has_near ? spot_values : attenuation_values;
                srChannelMath::distanceRamp(far_values, distance_values, scaled_far_end,
                                            far_attenuation, 1, srChannelMath::RampDirection::decreasing);
                if (has_near) srChannelMath::multiply(attenuation, far_values);
                else attenuation = far_values;
            }
            if (!attenuation.empty() && srChannelMath::allZero(attenuation)) {
                return;
            }
        } else if (attenuation_model == ATTENUATION_OPENGL) {
            if ((derived_flags & srLight::DERIVED_OPENGL_ATTENUATION) != 0) {
                if ((derived_flags & srLight::DERIVED_CONSTANT_ATTENUATION) != 0) {
                    float constant = 1.0f / opengl_attenuation.x;
                    std::fill_n(attenuation_bank, count, constant);
                } else {
                    for (std::size_t i = 0; i < count; ++i)
                        attenuation_bank[i] =
                            1.0f / ((opengl_attenuation.z * distances[i] + opengl_attenuation.y) *
                                        distances[i] +
                                    opengl_attenuation.x);
                }
                for (std::size_t i = 0; i < count; ++i)
                    attenuation_bank[i] = glm::clamp(attenuation_bank[i], 0.0f, 1.0f);
                attenuation = attenuation_values;
            }
        }
        if ((derived_flags & srLight::DERIVED_SPOT) != 0) {
            const auto spot_direction = -spot_direction_eye;
            const float spot_scale = 1.0f / (1.0f - spot_cutoff);
            for (SRDWORD i = 0; i < count; ++i) {
                const float factor =
                    (glm::dot(directions[i], spot_direction) - spot_cutoff) * spot_scale;
                spot_factors[i] = factor <= 0.0f ? 0.0f : factor;
            }
            if (srChannelMath::allZero({spot_factors, count})) {
                return;
            }
            if (spot_exponent != 1.0f) {
                srLightingMath::power({spot_factors, count}, {spot_factors, count}, spot_exponent);
            }
            if (!attenuation.empty()) {
                srChannelMath::multiply(attenuation, spot_values);
            } else {
                attenuation = spot_values;
            }
        }
        if (need_normals) {
            const auto normals = pipe.getEyeSpaceNormal();
            srLightingMath::normalDots(dot_values, normals, direction_values,
                                       srLightingMath::DotRange::nonnegative);
        }
    } else if (need_normals) {
        srLightingMath::normalDots(dot_values, pipe.getEyeSpaceNormal(), {&light_position, 1},
                                   srLightingMath::DotRange::unit);
    }

    if ((channels & (1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT)) != 0) {
        glm::vec4 ambient;
        ambient = scaled_ambient * pipe.material_info.ambient;
        if (!attenuation.empty()) {
            pipe.applyDiffuseLight(attenuation, ambient);
        } else {
            pipe.applyDiffuseLight(ambient);
        }
    }
    if (!need_normals) {
        return;
    }
    if (srChannelMath::allZero({dots, count})) {
        return;
    }
    if ((channels & (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE)) != 0) {
        const glm::vec4 color = scaled_diffuse * pipe.material_info.diffuse;
        srLightingMath::accumulateLight(pipe.getDiffuse(), color, dot_values, attenuation);
    }
    if ((channels & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        return;
    }
    if ((derived_flags & srLight::DERIVED_DIRECTIONAL) != 0 && count != 0) {
        std::fill_n(directions, count, light_position);
    }
    const auto eye_directions = pipe.getEyeSpaceDir();
    for (std::size_t i = 0; i < count; ++i)
        directions[i] = srLightingMath::directionAndDistance(directions[i] - eye_directions[i]).direction;
    const auto normals = pipe.getEyeSpaceNormal();
    srLightingMath::normalDots(distance_values, normals, direction_values,
                               srLightingMath::DotRange::nonnegative);
    if (pipe.material_info.shininess > 1.0f) {
        srLightingMath::power({distances, count}, {distances, count},
                               pipe.material_info.shininess);
    }
    srChannelMath::multiply(distance_values, dot_values);
    const glm::vec4 color = scaled_specular * pipe.material_info.specular;
    srLightingMath::accumulateLight(pipe.getSpecular(), color, distance_values, attenuation);
}

// FUNCTION: SURRENDER 0x1004DC80
void srLight::traverse(TraverseInfo& info)
{
    traverseEffect(info, [&] {
        return fabs(intensity) <= 0.0001 ? EffectTraversal::childrenOnly : EffectTraversal::active;
    });
}

// FUNCTION: SURRENDER 0x1004DF40
void srLight::setLinearAttenuation(float range, float attenuation)
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

// FUNCTION: SURRENDER 0x1004E630
void srLight::enable(e_enable flag)
{
    enable_flags |= (1UL << srLight::ENABLE_SPOT) << flag;
}

// FUNCTION: SURRENDER 0x1004E610
void srLight::disable(e_enable flag)
{
    enable_flags &= ~(1 << flag);
}

// FUNCTION: SURRENDER 0x1004E650
int srLight::isEnabled(e_enable flag) const
{
    return (enable_flags & (1 << flag)) != 0;
}

// FUNCTION: SURRENDER 0x1004E760
void srLight::setAmbient(const glm::vec3& ambient)
{
    this->ambient = ambient;
}

// FUNCTION: SURRENDER 0x1004E670
glm::vec3 srLight::getAmbient() const
{
    return ambient;
}

// FUNCTION: SURRENDER 0x1004E780
void srLight::setDiffuse(const glm::vec3& diffuse)
{
    this->diffuse = diffuse;
}

// FUNCTION: SURRENDER 0x1004E6A0
glm::vec3 srLight::getDiffuse() const
{
    return diffuse;
}

// FUNCTION: SURRENDER 0x1004E7B0
void srLight::setSpecular(const glm::vec3& specular)
{
    this->specular = specular;
}

// FUNCTION: SURRENDER 0x1004E6E0
glm::vec3 srLight::getSpecular() const
{
    return specular;
}

// FUNCTION: SURRENDER 0x1004E830
void srLight::setSpotDirection(const glm::vec3& direction)
{
    spot_direction = normalizedOrZero(direction);
}

// FUNCTION: SURRENDER 0x1004E7D0
void srLight::setSpotAngle(float angle)
{
    if (angle <= 0.0f) {
        spot_angle = 0.0f;
    } else if (3.141592653589793 * 0.5 <= angle) {
        spot_angle = (float)(3.141592653589793 * 0.5);
    } else {
        spot_angle = angle;
    }
}

// FUNCTION: SURRENDER 0x1004E8A0
void srLight::setSpotExponent(float exponent)
{
    if (exponent <= 0.0f) {
        spot_exponent = 0.0f;
    } else if (exponent >= 128.0f) {
        spot_exponent = 128.0f;
    } else {
        spot_exponent = exponent;
    }
}

// FUNCTION: SURRENDER 0x1004E720
glm::vec3 srLight::getSpotDirection() const
{
    return spot_direction;
}

// FUNCTION: SURRENDER 0x1004E710
float srLight::getSpotAngle() const
{
    return spot_angle;
}

// FUNCTION: SURRENDER 0x1004E750
float srLight::getSpotExponent() const
{
    return spot_exponent;
}

// FUNCTION: SURRENDER 0x1004E7A0
void srLight::setIntensity(float intensity)
{
    this->intensity = intensity;
}

// FUNCTION: SURRENDER 0x1004E6D0
float srLight::getIntensity() const
{
    return intensity;
}

// FUNCTION: SURRENDER 0x1004E900
void srLight::setAttenuationModel(e_attenuationModel model)
{
    attenuation_model = model;
}

// FUNCTION: SURRENDER 0x1004E910
srLight::e_attenuationModel srLight::getAttenuationModel() const
{
    return attenuation_model;
}

// FUNCTION: SURRENDER 0x1004E920
void srLight::setAttenuation(const glm::vec3& attenuation)
{
    opengl_attenuation = glm::max(attenuation, glm::vec3(0.0001f));
}

// FUNCTION: SURRENDER 0x1004E9A0
glm::vec3 srLight::getAttenuation() const
{
    return opengl_attenuation;
}

// FUNCTION: SURRENDER 0x1004EA60
void srLight::setNearAttenuationRange(double start, double end)
{
    near_start = start;
    near_end = end;
}

// FUNCTION: SURRENDER 0x1004EA30
void srLight::setFarAttenuationRange(double start, double end)
{
    far_start = start;
    far_end = end;
}

// FUNCTION: SURRENDER 0x1004EA90
void srLight::setSafeRange(float range)
{
    safe_range = range;
}

// FUNCTION: SURRENDER 0x1004EAA0
float srLight::getSafeRange() const
{
    return safe_range;
}

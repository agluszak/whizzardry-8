
#include "surrender/srLight.h"
#include <algorithm>
#include "surrender/srStreamFlags.h"

#include <math.h>
#include <ostream>

#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srMath.h"
#include <cassert>

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
    ambient.SetZero();
    diffuse = 1.0f;
    specular = 1.0f;
    spot_direction.Set(0.0f, 0.0f, 1.0f);
    spot_exponent = 1.0f;
    intensity = 1.0f;
    spot_angle = (float)(3.141592653589793 * 0.5);
    safe_range = 0.0f;
    attenuation_model = ATTENUATION_OPENGL;
    opengl_attenuation.x = 1.0f;
    opengl_attenuation.y = 0.0f;
    opengl_attenuation.z = 0.0f;
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

/* WIZ8 0x0049D67C..0x0049D6C5 inlines the SDK copy: construct the
   illuminator with parent zero and a fresh identity, then copy its base state
   and light members. Compiler-generated copying shares scene
   links and registry ownership, leaving dangling siblings during unload. */
srLight::srLight(const srLight& other)
    : srClassSupport<srLight, srIlluminator, 0x1220>(other),
      attenuation_model(other.attenuation_model),
      near_start(other.near_start),
      near_end(other.near_end),
      far_start(other.far_start),
      far_end(other.far_end),
      scaled_near_start(other.scaled_near_start),
      scaled_far_end(other.scaled_far_end),
      near_attenuation(other.near_attenuation),
      far_attenuation(other.far_attenuation),
      opengl_attenuation(other.opengl_attenuation),
      enable_flags(other.enable_flags),
      ambient(other.ambient),
      diffuse(other.diffuse),
      specular(other.specular),
      spot_direction(other.spot_direction),
      spot_angle(other.spot_angle),
      spot_exponent(other.spot_exponent),
      intensity(other.intensity),
      safe_range(other.safe_range),
      scaled_ambient(other.scaled_ambient),
      scaled_diffuse(other.scaled_diffuse),
      scaled_specular(other.scaled_specular),
      spot_direction_eye(other.spot_direction_eye),
      spot_cutoff(other.spot_cutoff),
      attenuation_range(other.attenuation_range),
      derived_flags(other.derived_flags),
      channel_mask(other.channel_mask)
{
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

// FUNCTION: SURRENDER 0x1004E0C0
void srLight::dump(std::ostream& stream)
{
    srNode::dump(stream);
    w8_long flags = srGetStreamFlags(stream);
    srSetStreamFlags(stream, (flags & 0xfffffe7fL) | 0x40);
    stream.width(0x20);
    stream << "  Control flags: ";
    if (enable_flags == 0) {
        stream << "[NONE]";
    } else {
        stream << '[';
        bool first = true;
        const char* names = s_light_flag_names;
        for (w8_ulong index = 0; index < 0x20; ++index) {
            if ((enable_flags & (1ul << index)) == 0) {
                if (names != 0) {
                    while (*names != '\0' && *names != ',') {
                        ++names;
                    }
                }
            } else {
                if (first) {
                    first = false;
                } else {
                    stream << ',';
                }
                if (names == 0 || *names == '\0') {
                    stream << index;
                } else {
                    while (*names != '\0' && *names != ',') {
                        stream << *names;
                        ++names;
                    }
                }
            }
            if (names != 0 && *names == ',') {
                ++names;
            }
        }
        stream << ']';
    }
    stream << '\n';
    stream.width(0x20);
    stream << "  Intensity: " << intensity << '\n';
    stream.width(0x20);
    stream << "  Ambient coeff.: {" << ambient.x << ',' << ambient.y << ',' << ambient.z << '}'
           << '\n';
    stream.width(0x20);
    stream << "  Diffuse coeff.: {" << diffuse.x << ',' << diffuse.y << ',' << diffuse.z << '}'
           << '\n';
    stream.width(0x20);
    stream << "  Specular coeff.: {" << specular.x << ',' << specular.y << ',' << specular.z << '}'
           << '\n';
    stream.width(0x20);
    stream << "  Spot direction: {" << spot_direction.x << ',' << spot_direction.y << ','
           << spot_direction.z << '}' << '\n';
    stream.width(0x20);
    stream << "  Spot angle: " << spot_angle << '\n';
    stream.width(0x20);
    stream << "  Spot exponent: " << spot_exponent << '\n';
    stream.width(0x20);
    stream << "  Safe Range: " << safe_range << '\n';
    stream.width(0x20);
    stream << "  Attenuation model: ";
    if (attenuation_model == ATTENUATION_OPENGL) {
        stream << "OpenGL" << '\n';
        stream.width(0x20);
        stream << "  Attenuation fact.: {" << opengl_attenuation.x << ',' << opengl_attenuation.y
               << ',' << opengl_attenuation.z << '}' << '\n';
    } else if (attenuation_model == ATTENUATION_3DSTUDIO_MAX) {
        stream << "3DStudio Max" << '\n';
        stream.width(0x20);
        stream << "  Near Range : " << near_start << " - " << near_end << '\n';
        stream.width(0x20);
        stream << "  Far Range : " << far_start << " - " << far_end << '\n';
    } else {
        stream << "No attenuation" << '\n';
    }
    srSetStreamFlags(stream, flags & 0x7fff);
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
    applyWorldSpaceMatrix(*renderer);
    srMatrix4T<float> model_view;
    renderer->getMatrix(model_view);
    derived_flags = 0;
    derived_flags = srLight::DERIVED_ACTIVE;
    channel_mask = 0;
    if (ambient.x != 0.0f || ambient.y != 0.0f || ambient.z != 0.0f) {
        channel_mask |= (1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        scaled_ambient.x = ambient.x * intensity;
        scaled_ambient.y = ambient.y * intensity;
        scaled_ambient.z = ambient.z * intensity;
        scaled_ambient.w = 0.0f;
    }
    if (diffuse.x != 0.0f || diffuse.y != 0.0f || diffuse.z != 0.0f) {
        channel_mask |= (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        scaled_diffuse.x = diffuse.x * intensity;
        scaled_diffuse.y = diffuse.y * intensity;
        scaled_diffuse.z = diffuse.z * intensity;
        scaled_diffuse.w = 0.0f;
    }
    if (specular.x != 0.0f || specular.y != 0.0f || specular.z != 0.0f) {
        channel_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
        scaled_specular.x = specular.x * intensity;
        scaled_specular.y = specular.y * intensity;
        scaled_specular.z = specular.z * intensity;
        scaled_specular.w = 0.0f;
    }
    if (channel_mask == 0) {
        derived_flags &= ~srLight::DERIVED_ACTIVE;
    }
    if ((enable_flags & (1UL << srLight::ENABLE_DIRECTIONAL)) != 0) {
        derived_flags |= srLight::DERIVED_DIRECTIONAL;
        srVector3T<float> direction(-model_view.vectors[0].z, -model_view.vectors[1].z,
                                    -model_view.vectors[2].z);
        float length_squared = glm::dot(direction.glmValue(), direction.glmValue());
        if (length_squared != 1.0f) {
            double scale = 1.0 / sqrt(length_squared);
            direction *= scale;
        }
        eye_location = direction;
    } else {
        if ((enable_flags & (1UL << srLight::ENABLE_SPOT)) != 0 && spot_angle > 0.0f &&
            spot_exponent != 0.0f) {
            srMatrix4T<float> inverse;
            renderer->getInverseModelViewMatrix(inverse);
            derived_flags |= srLight::DERIVED_SPOT;
            spot_cutoff = (float)cos(spot_angle);
            spot_direction_eye.x = inverse.vectors[0].x * spot_direction.x +
                                   inverse.vectors[1].x * spot_direction.y +
                                   inverse.vectors[2].x * spot_direction.z;
            spot_direction_eye.y = inverse.vectors[0].y * spot_direction.x +
                                   inverse.vectors[1].y * spot_direction.y +
                                   inverse.vectors[2].y * spot_direction.z;
            spot_direction_eye.z = inverse.vectors[0].z * spot_direction.x +
                                   inverse.vectors[1].z * spot_direction.y +
                                   inverse.vectors[2].z * spot_direction.z;
            float length_squared =
                glm::dot(spot_direction_eye.glmValue(), spot_direction_eye.glmValue());
            if (length_squared != 0.0f) {
                double scale = 1.0 / sqrt(length_squared);
                spot_direction_eye *= scale;
            }
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
                        srVector3T<float> origin(0.0f, 0.0f, 0.0f);
                        if (renderer->testBoundingSphere(origin, safe_range + far_end) == 0) {
                            derived_flags &= ~srLight::DERIVED_ACTIVE;
                        }
                    }
                    attenuation_range = scaled_far_end;
                    derived_flags |= srLight::DERIVED_RANGE_CULL;
                }
            }
        }
        eye_location.x = model_view.vectors[0].w;
        eye_location.y = model_view.vectors[1].w;
        eye_location.z = model_view.vectors[2].w;
    }
    renderer->popMatrix();
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
        float dx = eye_location.x - input->eye_center.x;
        float dy = eye_location.y - input->eye_center.y;
        float dz = eye_location.z - input->eye_center.z;
        float range = attenuation_range + input->eye_radius;
        if (range * range <= dx * dx + dy * dy + dz * dz) {
            return 0;
        }
    }
    if ((derived_flags & srLight::DERIVED_SPOT) != 0) {
        float radius = input->eye_radius;
        float dx = input->eye_center.x - eye_location.x;
        float dy = input->eye_center.y - eye_location.y;
        float dz = input->eye_center.z - eye_location.z;
        float distance_squared = dx * dx + dy * dy + dz * dz;
        if (radius * radius < distance_squared &&
            (dx * spot_direction_eye.x + dy * spot_direction_eye.y + dz * spot_direction_eye.z) /
                        sqrtf(distance_squared) +
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
    srVector3 directions[0x40];
    srVertexPipe::Scratch* scratch = &pipe.scratch;
    float* attenuation = 0;

    if ((derived_flags & srLight::DERIVED_DIRECTIONAL) == 0) {
        std::ranges::transform(
            std::span<const srVector4>{
                pipe.eye_space_locations + pipe.batch_base + pipe.sub_batch_offset, count},
            directions, [&](const auto& v) { return glm::vec3(v.glmValue()); });
        std::ranges::transform(
            std::span<const srVector3>{directions, count}, directions,
            [&](const auto& v) { return eye_location.glmValue() - v.glmValue(); });
        {
            std::span<srVector3> input_destination{directions, count};
            std::span<float> input_lengths{distances, count};
            std::span<const srVector3> input_source{directions, count};
            for (std::size_t i = 0; i < input_destination.size(); ++i) {
                const auto v = glm::vec3(input_source[i].glmValue());
                input_lengths[i] = glm::length(v);
                input_destination[i] = v / input_lengths[i];
            }
        }
        if (attenuation_model == ATTENUATION_3DSTUDIO_MAX) {
            if ((enable_flags & (1UL << srLight::ENABLE_RANGE_NEAR)) != 0) {
                std::ranges::transform(std::span<const float>{distances, count}, attenuation_bank,
                                       [&](const auto& v) { return (-scaled_near_start) + v; });
                std::ranges::transform(std::span<const float>{attenuation_bank, count},
                                       attenuation_bank,
                                       [&](const auto& v) { return (near_attenuation)*v; });
                std::ranges::transform(std::span<const float>{attenuation_bank, count},
                                       attenuation_bank,
                                       [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
                attenuation = attenuation_bank;
            }
            if ((enable_flags & (1UL << srLight::ENABLE_RANGE_FAR)) != 0) {
                float* far_bank = attenuation != 0 ? spot_factors : attenuation_bank;
                std::ranges::transform(std::span<const float>{distances, count}, far_bank,
                                       [&](const auto& v) { return (scaled_far_end)-v; });
                std::ranges::transform(std::span<const float>{far_bank, count}, far_bank,
                                       [&](const auto& v) { return (far_attenuation)*v; });
                std::ranges::transform(std::span<const float>{far_bank, count}, far_bank,
                                       [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
                if (far_bank == spot_factors) {
                    std::ranges::transform(std::span<const float>{attenuation_bank, count},
                                           std::span<const float>{spot_factors, count},
                                           attenuation_bank,
                                           [&](const auto& a, const auto& b) { return a * b; });
                } else {
                    attenuation = far_bank;
                }
            }
            if (attenuation != 0 && std::ranges::all_of(std::span<const float>{attenuation, count},
                                                        [](float v) { return v == 0.0f; })) {
                return;
            }
        } else if (attenuation_model == ATTENUATION_OPENGL) {
            if ((derived_flags & srLight::DERIVED_OPENGL_ATTENUATION) != 0) {
                if ((derived_flags & srLight::DERIVED_CONSTANT_ATTENUATION) != 0) {
                    float constant = 1.0f / opengl_attenuation.x;
                    std::fill_n(attenuation_bank, count, constant);
                } else {
                    std::ranges::transform(
                        std::span<const float>{distances, count}, attenuation_bank,
                        [&](const auto& v) {
                            return 1.0f /
                                   (((opengl_attenuation).z * v + (opengl_attenuation).y) * v +
                                    (opengl_attenuation).x);
                        });
                }
                std::ranges::transform(std::span<const float>{attenuation_bank, count},
                                       attenuation_bank,
                                       [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
                attenuation = attenuation_bank;
            }
        }
        if ((derived_flags & srLight::DERIVED_SPOT) != 0) {
            srVector3T<float> negated(-spot_direction_eye.x, -spot_direction_eye.y,
                                      -spot_direction_eye.z);
            std::ranges::transform(
                std::span<const srVector3>{directions, count}, spot_factors,
                [&](const auto& v) { return glm::dot(v.glmValue(), negated.glmValue()); });
            std::ranges::transform(std::span<const float>{spot_factors, count}, spot_factors,
                                   [&](const auto& v) { return (-spot_cutoff) + v; });
            std::ranges::transform(
                std::span<const float>{spot_factors, count}, spot_factors,
                [&](const auto& v) { return (1.0f / (1.0f - spot_cutoff)) * v; });
            std::ranges::transform(std::span<const float>{spot_factors, count}, spot_factors,
                                   [&](const auto& v) { return v <= 0.0f ? 0.0f : v; });
            if (std::ranges::all_of(std::span<const float>{spot_factors, count},
                                    [](float v) { return v == 0.0f; })) {
                return;
            }
            if (spot_exponent != 1.0f) {
                srLight::specularPower({spot_factors, count}, {spot_factors, count}, spot_exponent);
            }
            if (attenuation != 0) {
                std::ranges::transform(std::span<const float>{attenuation, count},
                                       std::span<const float>{spot_factors, count}, attenuation,
                                       [&](const auto& a, const auto& b) { return a * b; });
            } else {
                attenuation = spot_factors;
            }
        }
        if (need_normals) {
            if ((scratch->flags & srVertexPipe::Scratch::READY_EYE_NORMALS) == 0) {
                pipe.setupEyeSpaceNormal();
            }
            std::ranges::transform(
                std::span<const srVector3>{scratch->normals + pipe.sub_batch_offset, count},
                std::span<const srVector3>{directions, count}, dots,
                [&](const auto& a, const auto& b) { return glm::dot(a.glmValue(), b.glmValue()); });
            std::ranges::transform(std::span<const float>{dots, count}, dots,
                                   [&](const auto& v) { return v <= 0.0f ? 0.0f : v; });
        }
    } else if (need_normals) {
        if ((scratch->flags & srVertexPipe::Scratch::READY_EYE_NORMALS) == 0) {
            pipe.setupEyeSpaceNormal();
        }
        std::ranges::transform(
            std::span<const srVector3>{scratch->normals + pipe.sub_batch_offset, count}, dots,
            [&](const auto& v) { return glm::dot(v.glmValue(), eye_location.glmValue()); });
        std::ranges::transform(std::span<const float>{dots, count}, dots,
                               [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
    }

    if ((channels & (1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT)) != 0) {
        srVector4T<float> ambient;
        ambient = scaled_ambient * pipe.material_info.ambient;
        if (attenuation != 0) {
            pipe.applyDiffuseLight(attenuation, ambient);
        } else {
            pipe.applyDiffuseLight(ambient);
        }
    }
    if (!need_normals) {
        return;
    }
    if (std::ranges::all_of(std::span<const float>{dots, count},
                            [](float v) { return v == 0.0f; })) {
        return;
    }
    if ((channels & (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE)) != 0) {
        srVector4T<float> diffuse;
        diffuse = scaled_diffuse * pipe.material_info.diffuse;
        srCore.getStatisticsManager()->statistics.diffuse_operations += count;
        if ((pipe.lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
            pipe.setupDiffuse();
        }
        srVector4T<float>* out =
            pipe.vertex_array->diffuse + pipe.batch_base + pipe.sub_batch_offset;
        if (attenuation != 0) {
            {
                std::span<srVector4> input_destination{out, count};
                std::span<const srVector4> input_add_source{out, count};
                const srVector4& input_multiply_constant = diffuse;
                std::span<const float> input_multiply_source_0{attenuation, count};
                std::span<const float> input_multiply_source_1{dots, count};
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i] = input_multiply_constant.glmValue() *
                                               input_multiply_source_0[i] *
                                               input_multiply_source_1[i] +
                                           input_add_source[i].glmValue();
            }
        } else {
            {
                std::span<srVector4> input_destination{out, count};
                std::span<const srVector4> input_add_source{out, count};
                const srVector4& input_multiply_constant = diffuse;
                std::span<const float> input_multiply_source{dots, count};
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i] =
                        input_multiply_constant.glmValue() * input_multiply_source[i] +
                        input_add_source[i].glmValue();
            }
        }
    }
    if ((channels & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        return;
    }
    if ((derived_flags & srLight::DERIVED_DIRECTIONAL) != 0 && count != 0) {
        srVector3 constant = eye_location;
        if (constant.x == constant.y && constant.x == constant.z) {
            constant.Set(eye_location.x, eye_location.x, eye_location.x);
        }
        std::fill_n(directions, count, constant);
    }
    if ((scratch->flags & srVertexPipe::Scratch::READY_EYE_DIRECTION) == 0) {
        pipe.setupEyeSpaceDirAndDist();
    }
    std::ranges::transform(
        std::span<const srVector3>{directions, count},
        std::span<const srVector3>{scratch->dir + pipe.sub_batch_offset, count}, directions,
        [&](const auto& a, const auto& b) { return a.glmValue() - b.glmValue(); });
    std::ranges::transform(std::span<const srVector3>{directions, count}, directions,
                           [&](const auto& v) {
                               return glm::length(v.glmValue()) == 0.0f
                                          ? v.glmValue() * 0.0f
                                          : glm::normalize(v.glmValue()) * static_cast<float>(1.0f);
                           });
    if ((scratch->flags & srVertexPipe::Scratch::READY_EYE_NORMALS) == 0) {
        pipe.setupEyeSpaceNormal();
    }
    std::ranges::transform(
        std::span<const srVector3>{scratch->normals + pipe.sub_batch_offset, count},
        std::span<const srVector3>{directions, count}, distances,
        [&](const auto& a, const auto& b) { return glm::dot(a.glmValue(), b.glmValue()); });
    std::ranges::transform(std::span<const float>{distances, count}, distances,
                           [&](const auto& v) { return v <= 0.0f ? 0.0f : v; });
    if (pipe.material_info.shininess > 1.0f) {
        srLight::specularPower({distances, count}, {distances, count},
                               pipe.material_info.shininess);
    }
    std::ranges::transform(std::span<const float>{distances, count},
                           std::span<const float>{dots, count}, distances,
                           [&](const auto& a, const auto& b) { return a * b; });
    srVector4T<float> specular;
    specular = scaled_specular * pipe.material_info.specular;
    srCore.getStatisticsManager()->statistics.specular_operations += count;
    if ((pipe.lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        pipe.setupSpecular();
    }
    srVector4T<float>* out = pipe.vertex_array->specular + pipe.batch_base + pipe.sub_batch_offset;
    if (attenuation != 0) {
        {
            std::span<srVector4> input_destination{out, count};
            std::span<const srVector4> input_add_source{out, count};
            const srVector4& input_multiply_constant = specular;
            std::span<const float> input_multiply_source_0{attenuation, count};
            std::span<const float> input_multiply_source_1{distances, count};
            for (std::size_t i = 0; i < input_destination.size(); ++i)
                input_destination[i] = input_multiply_constant.glmValue() *
                                           input_multiply_source_0[i] * input_multiply_source_1[i] +
                                       input_add_source[i].glmValue();
        }
    } else {
        {
            std::span<srVector4> input_destination{out, count};
            std::span<const srVector4> input_add_source{out, count};
            const srVector4& input_multiply_constant = specular;
            std::span<const float> input_multiply_source{distances, count};
            for (std::size_t i = 0; i < input_destination.size(); ++i)
                input_destination[i] =
                    input_multiply_constant.glmValue() * input_multiply_source[i] +
                    input_add_source[i].glmValue();
        }
    }
}

// FUNCTION: SURRENDER 0x1004DC80
void srLight::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if (testFlag(FLAG_TERMINATE)) {
        return;
    }
    if (testFlag(FLAG_DISABLE) || fabs(intensity) <= 0.0001) {
        if (first_child_ != 0) {
            first_child_->traverse(info);
        }
        return;
    }
    if (!testFlag(FLAG_GLOBAL)) {
        info.entries.push_back({this, 1});
    } else {
        info.nodes.push_back(this);
    }
    if (first_child_ != 0) {
        first_child_->traverse(info);
    }
    if (!testFlag(FLAG_GLOBAL)) {
        info.entries.push_back({this, 2});
    }
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
void srLight::setAmbient(const srVector3T<float>& ambient)
{
    this->ambient = ambient;
}

// FUNCTION: SURRENDER 0x1004E670
srVector3T<float> srLight::getAmbient() const
{
    return ambient;
}

// FUNCTION: SURRENDER 0x1004E780
void srLight::setDiffuse(const srVector3T<float>& diffuse)
{
    this->diffuse = diffuse;
}

// FUNCTION: SURRENDER 0x1004E6A0
srVector3T<float> srLight::getDiffuse() const
{
    return diffuse;
}

// FUNCTION: SURRENDER 0x1004E7B0
void srLight::setSpecular(const srVector3T<float>& specular)
{
    this->specular = specular;
}

// FUNCTION: SURRENDER 0x1004E6E0
srVector3T<float> srLight::getSpecular() const
{
    return specular;
}

// FUNCTION: SURRENDER 0x1004E830
void srLight::setSpotDirection(const srVector3T<float>& direction)
{
    spot_direction = direction;
    float length_squared = glm::dot(spot_direction.glmValue(), spot_direction.glmValue());
    if (length_squared != 0.0f) {
        double scale = 1.0 / sqrt(length_squared);
        spot_direction *= scale;
    }
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
srVector3T<float> srLight::getSpotDirection() const
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
void srLight::setAttenuation(const srVector3T<float>& attenuation)
{
    float z = attenuation.z;
    if (z <= 0.0001f) {
        z = 0.0001f;
    }
    float y = attenuation.y;
    if (y <= 0.0001f) {
        y = 0.0001f;
    }
    float x = attenuation.x;
    if (x <= 0.0001f) {
        x = 0.0001f;
    }
    opengl_attenuation.x = x;
    opengl_attenuation.y = y;
    opengl_attenuation.z = z;
}

// FUNCTION: SURRENDER 0x1004E9A0
srVector3T<float> srLight::getAttenuation() const
{
    return opengl_attenuation;
}

// FUNCTION: SURRENDER 0x1004EA60
void srLight::setNearAttenuationRange(double start, double end)
{
    near_start = start;
    near_end = end;
}

// FUNCTION: SURRENDER 0x1004EA00
void srLight::getNearAttenuationRange(double& start, double& end) const
{
    start = near_start;
    end = near_end;
}

// FUNCTION: SURRENDER 0x1004EA30
void srLight::setFarAttenuationRange(double start, double end)
{
    far_start = start;
    far_end = end;
}

// FUNCTION: SURRENDER 0x1004E9D0
void srLight::getFarAttenuationRange(double& start, double& end) const
{
    start = far_start;
    end = far_end;
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

namespace
{
constexpr double coefficients[0x12][4] = {
    {0, 1e-14, 1, 0},
    {-0.068979736114850004, 0.18179573075354999, 0.88948269483362996, -0.0032237222793199999},
    {-0.12576753110003999, 0.34382063299797, 0.78571136674362996, -0.0053941615606599999},
    {-0.17092256892982999, 0.48661079671178997, 0.68891153819327, -0.0067378060870700003},
    {-0.20507900130076001, 0.61089353418348002, 0.59915069732755, -0.0074389531232600002},
    {-0.22891455983919001, 0.71752488060732, 0.51637759148530005, -0.0076474469703200001},
    {-0.24312703917604001, 0.80744183884992005, 0.44045304108798999, -0.00748496029462},
    {-0.24841687096368001, 0.88162612503694004, 0.37117406021430999, -0.0070501036072999996},
    {-0.24547440268178, 0.94107687822182995, 0.30829269047641, -0.0064225898414199999},
    {-0.23497080142249999, 0.98679034859172998, 0.25153065736378, -0.0056666352687399996},
    {-0.21755174140842001, 1.01974500915189, 0.20059072482958001, -0.0048337418349100004},
    {-0.19383321994641001, 1.04089087244265, 0.15516544047627001, -0.0039649773154400002},
    {-0.16439899158457999, 1.05114205735211, 0.11494381932562001, -0.0030928469032899999},
    {-0.12979922349142001, 1.0513718575920701, 0.079616400344480004, -0.0022428316857099999},
    {-0.090550063549559995, 1.0424097253979201, 0.048879020037899998, -0.0014346549735899999},
    {-0.047133881807620001, 1.0250397112049401, 0.022435576368620001, -0.00068332584966999997},
    {-1e-14, 1.00000000000001, 0, 0},
    {-1e-14, 1.00000000000001, 0, 0},
};
constexpr float points[0x12][7] = {
    {0.000999900047f, 0.0316211991f, 0.177823499f, 0.421691209f, 0.649377584f, 0.805839717f,
     0.897685707f},
    {0.00474390015f, 0.0688759983f, 0.262442291f, 0.512291312f, 0.715745211f, 0.846017301f,
     0.919791996f},
    {0.0081093004f, 0.0900517032f, 0.300086111f, 0.547801077f, 0.740135908f, 0.860311508f,
     0.927529812f},
    {0.0111445002f, 0.1055675f, 0.324911505f, 0.570010126f, 0.75499022f, 0.86890173f, 0.932148993f},
    {0.0138889998f, 0.1178516f, 0.343295187f, 0.585914016f, 0.76545018f, 0.874900103f,
     0.935360909f},
    {0.0163755994f, 0.127967194f, 0.357724994f, 0.598101199f, 0.773370028f, 0.879414618f,
     0.937771082f},
    {0.0186313f, 0.136496499f, 0.369454414f, 0.607827604f, 0.779632986f, 0.882968307f,
     0.939663887f},
    {0.0206783991f, 0.143799901f, 0.379209489f, 0.615799904f, 0.784729183f, 0.885849416f,
     0.941195726f},
    {0.0225352999f, 0.150117606f, 0.387450188f, 0.622454882f, 0.788958073f, 0.888233185f,
     0.942461193f},
    {0.0242167003f, 0.155617207f, 0.394483387f, 0.628079116f, 0.792514384f, 0.890232801f,
     0.9435215f},
    {0.0257344991f, 0.160419807f, 0.400524408f, 0.632869899f, 0.795531213f, 0.891925573f,
     0.944418073f},
    {0.0270971991f, 0.164612293f, 0.405724406f, 0.636964977f, 0.798100889f, 0.893364906f,
     0.94517982f},
    {0.0283103995f, 0.168256894f, 0.410191387f, 0.640461802f, 0.800288618f, 0.89458847f,
     0.945826888f},
    {0.0293761995f, 0.171394899f, 0.413998604f, 0.643427312f, 0.802139223f, 0.895622194f,
     0.946373224f},
    {0.0302920006f, 0.174045995f, 0.417188197f, 0.645901084f, 0.803679705f, 0.896481812f,
     0.946827292f},
    {0.0310484003f, 0.176205605f, 0.419768512f, 0.647895396f, 0.804919481f, 0.897173107f,
     0.947192192f},
    {0.0316227004f, 0.177827701f, 0.421696186f, 0.649381399f, 0.805842102f, 0.897687078f,
     0.947463512f},
    {0.0316227004f, 0.177827701f, 0.421696186f, 0.649381399f, 0.805842102f, 0.897687078f,
     0.947463512f},
};
} // namespace

// FUNCTION: SURRENDER 0x10068A80
void srLight::specularPower(std::span<float> destination, std::span<const float> source,
                            float exponent)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    SRDWORD squarings = 0;
    if (exponent > 127.0) {
        exponent = 127.0f;
    }
    while (exponent >= 2.0f) {
        exponent *= 0.5f;
        ++squarings;
    }
    float index_value = (exponent - 1.0f) * 16.0f;
    int index = static_cast<int>(index_value);
    index_value -= index;
    double complement = 1.0 - index_value;
    float coefficient_0 =
        (float)(index_value * coefficients[index + 1][0] + complement * coefficients[index][0]);
    float coefficient_1 =
        (float)(index_value * coefficients[index + 1][1] + complement * coefficients[index][1]);
    float coefficient_2 =
        (float)(index_value * coefficients[index + 1][2] + complement * coefficients[index][2]);
    float coefficient_3 =
        (float)(index_value * coefficients[index + 1][3] + complement * coefficients[index][3]);
    float threshold = points[index + 1][squarings];
    for (std::size_t element = 0; element < count; ++element) {
        if (source[element] > threshold) {
            float value = source[element];
            for (SRDWORD square = 0; square < squarings; ++square) {
                value *= value;
            }
            destination[element] =
                ((value * coefficient_0 + coefficient_1) * value + coefficient_2) * value +
                coefficient_3;
        } else {
            destination[element] = 0.0f;
        }
    }
}

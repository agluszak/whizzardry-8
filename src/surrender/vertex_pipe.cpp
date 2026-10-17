#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "compat/compiler.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/channel_math.h"
#include "surrender/lighting_math.h"
#include "surrender/srARGB.h"
#include "surrender/srBoundingSphere.h"
#include "surrender/srMaterial.h"
#include "surrender/srShader.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"

// Opacity and fog coverage occupy colour alpha after their own channel setup.
// Some paths clamp coverage before scaling; others clamp the completed colour.
static void ScaleAndStoreCoverage(std::span<float> values, glm::vec4* colors, float scale,
                                  bool clamp_values)
{
    if (clamp_values) srChannelMath::clamp(values);
    srChannelMath::scale(values, scale);
    for (std::size_t index = 0; index < values.size(); ++index) colors[index].w = values[index];
}

// Attributes retain the original AVT even when geometry uses direct indices.
static void MultiplyIndexedColors(std::span<glm::vec4> destination, const glm::vec4* multipliers,
                                  std::span<const w8_ulong> indices)
{
    for (std::size_t index = 0; index < destination.size(); ++index)
        destination[index] *= multipliers[indices[index]];
}

template<class Vector>
static void TransformGeometry(const srVertexPipe::Input& input, std::span<const glm::vec3> source,
                              const glm::mat4& matrix, std::span<Vector> destination,
                              w8_ulong processed_base)
{
    for (w8_ulong index = 0; index < destination.size(); ++index)
        destination[index] =
            Vector(matrix * glm::vec4(source[input.geometryIndex(processed_base + index)], 1.0f));
}

// FUNCTION: SURRENDER 0x1002AA50
std::uint32_t srVertexPipe::getShaderDisableMask(const srShader& shader)
{
    w8_ulong disable = 0;
    if ((shader.value & srShader::MASK_FOG) == 0) {
        disable = 1 << srVertexProcessor::CHANNEL_FOG;
    }
    if ((shader.value & srShader::MASK_GRADIENT) == 0) {
        disable |=
            (1 << srVertexProcessor::CHANNEL_DIFFUSE) | (1 << srVertexProcessor::CHANNEL_ALPHA);
    } else if ((shader.value & srShader::MASK_ALPHATEST) == 0 &&
               ((shader.value >> srShader::DSTBLEND_SHIFT) & 7) != srShader::DSTBLEND_SRC_ALPHA &&
               ((shader.value >> srShader::DSTBLEND_SHIFT) & 7) !=
                   srShader::DSTBLEND_ONE_MINUS_SRC_ALPHA &&
               ((shader.value >> srShader::SRCBLEND_SHIFT) & 3) != srShader::SRCBLEND_SRC_ALPHA &&
               ((shader.value >> srShader::SRCBLEND_SHIFT) & 3) !=
                   srShader::SRCBLEND_ONE_MINUS_SRC_ALPHA) {
        disable |= 1 << srVertexProcessor::CHANNEL_ALPHA;
    }
    if ((shader.value & srShader::MASK_SECONDARY_GRADIENT) == 0) {
        disable |= 1 << srVertexProcessor::CHANNEL_SPECULAR;
    }
    if ((shader.value & srShader::MASK_TEXTURING) == 0) {
        return disable | (1 << srVertexProcessor::CHANNEL_ST0) |
               (1 << srVertexProcessor::CHANNEL_ST1) | (1 << srVertexProcessor::CHANNEL_Q0) |
               (1 << srVertexProcessor::CHANNEL_Q1);
    }
    if ((shader.value & (srShader::MASK_DETAILCOLOR0 | srShader::MASK_DETAILALPHA0 |
                         srShader::MASK_DETAILCOLOR1 | srShader::MASK_DETAILALPHA1)) == 0) {
        disable |= (1 << srVertexProcessor::CHANNEL_ST1) | (1 << srVertexProcessor::CHANNEL_Q1);
    }
    return disable;
}

// FUNCTION: SURRENDER 0x1002AAD0
std::uint32_t srVertexPipe::getShaderDisableMask(const srShader* shaders, const w8_ulong* channels,
                                                 w8_ulong channel_count)
{
    srShader shader;
    shader.value = shaders[channels[0]].value;
    w8_ulong available = ~getShaderDisableMask(shader);
    w8_ulong index = 0;
    if (channel_count != 0) {
        while (true) {
            index +=
                scanChangeIndexed(
                    (const w8_ulong*)
                        shaders /* c-style-cast-ok: srShader is one packed dword; scanChangeIndexed walks it as a dword table */
                    ,
                    shader.value, channels + 1 + index, channel_count - index - 1) +
                1;
            if (channel_count <= index) {
                break;
            }
            shader.value = shaders[channels[index]].value;
            available |= ~getShaderDisableMask(shader);
            if (available == 0xffffffff) {
                return 0;
            }
        }
    }
    return ~available;
}

// FUNCTION: SURRENDER 0x1002AB80
w8_ulong srVertexPipe::scanChangeIndexed(const w8_ulong* table, w8_ulong value,
                                         const w8_ulong* indices, w8_ulong index_count)
{
    for (w8_ulong index = 0; index < index_count; ++index) {
        if (table[indices[index]] != value) {
            return index;
        }
    }
    return index_count;
}

// FUNCTION: SURRENDER 0x1002AC10
srVertexPipe::srVertexPipe()
    : channel_mask(0), lazy_setup_mask(0), material_info(), material(nullptr), input(nullptr),
      avt(nullptr), current_record(nullptr), vertex_array(nullptr), eye_space_locations(nullptr),
      batch_base(0xffffffff), sub_batch_offset(0xffffffff), vertex_count(0), batch_count(0)
{
}

// FUNCTION: SURRENDER 0x1002ACF0
void srVertexPipe::processVertexBuffer()
{
    lazy_setup_mask = 0;
    w8_ulong channels = ~current_record->channels & ~material_info.disabled_channels;
    channel_mask = channels;
    if ((channels & (1 << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        channel_mask = channels & ~(1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        channel_mask = channels & ~((1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT) |
                                    (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE));
    }
    if ((channel_mask & ((1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT) |
                         (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE))) == 0) {
        channel_mask &= ~(1UL << srVertexProcessor::CHANNEL_DIFFUSE);
    }
    material->preProcess(*this);
    for (srVertexProcessor* processor : active_processors) {
        processor->process(*this);
    }
    material->postProcess(*this);
    channel_mask = ~current_record->channels;
    lazy_setup_mask &= channel_mask;
    finishDiffuseAlpha();
    finishSpecularFog();
    w8_ulong mask = channel_mask;
    for (unsigned stage = 0; stage < 2; ++stage) {
        const w8_ulong st_bit = 1u << (srVertexProcessor::CHANNEL_ST0 + stage);
        const w8_ulong q_bit = 1u << (srVertexProcessor::CHANNEL_Q0 + stage);
        if ((channel_mask & st_bit) == 0) continue;
        if ((lazy_setup_mask & q_bit) == 0) mask &= ~q_bit;

        if ((lazy_setup_mask & st_bit) == 0) setupST(stage);
        lazy_setup_mask |= st_bit;
        if ((lazy_setup_mask & q_bit) == 0) setupQ(stage);
        lazy_setup_mask |= q_bit;
    }
    w8_ulong packed = (mask >> 2) & 0xffffff7c;
    packed |= ((mask >> 1) & srVertexArray::ATTRIBUTE_SPECULAR) |
              ((mask & ((1 << srVertexProcessor::CHANNEL_DIFFUSE) |
                        (1 << srVertexProcessor::CHANNEL_ALPHA))) != 0
                   ? srVertexArray::ATTRIBUTE_DIFFUSE
                   : 0);
    std::fill_n(vertex_array->attributes + batch_base + sub_batch_offset, vertex_count, packed);
}

// FUNCTION: SURRENDER 0x1002AE90
void srVertexPipe::setMaterial(srMaterial* material)
{
    if (material == 0) {
        material = &default_material;
    }
    this->material = material;
    material_info = material->resolveMaterialInfo(current_record->material_overrides);
}

// FUNCTION: SURRENDER 0x1002AEC0
void srVertexPipe::process(const Input& input)
{
    this->input = &input;
    active_processors.clear();
    active_processors.reserve(input.processors.size());
    for (srVertexProcessor* processor : input.processors) {
        if (processor->isActive(*this)) {
            active_processors.push_back(processor);
        }
    }
    batch_base = 0;
    w8_ulong vertex_count = static_cast<w8_ulong>(input.active_vertices.size());
    if (vertex_count == 0) return;
    if (input.vertex_arrays.size() < input.records.size() || input.vertex_arrays.empty())
        throw std::invalid_argument("vertex pipeline requires an array for every render pass");
    eye_space_locations = input.vertex_arrays.front().eye_locations;
    for (w8_ulong processed = 0; processed < input.active_vertices.size(); ++processed) {
        const auto index = input.geometryIndex(processed);
        if (index >= input.positions.size() ||
            (!input.normals.empty() && index >= input.normals.size()))
            throw std::out_of_range("vertex pipeline geometry index exceeds its source channel");
    }
    while (vertex_count != 0) {
        vertex_count -= batch_base;
        batch_count = vertex_count;
        if (0x40 < vertex_count) {
            batch_count = 0x40;
        }
        avt = input.active_vertices.data() + batch_base;
        scratch.flags = 0;
        TransformGeometry(input, input.positions, input.model_view,
                          std::span{eye_space_locations + batch_base, batch_count}, batch_base);
        w8_ulong record_index;
        for (record_index = 0; record_index < input.records.size(); ++record_index) {
            const Record* record = &input.records[record_index];
            current_record = record;
            vertex_array = &input.vertex_arrays[record_index];
            if ((record->flags & Record::HAS_VERTEX_MATERIALS) == 0) {
                this->vertex_count = batch_count;
                sub_batch_offset = 0;
                setMaterial(record->material);
                processVertexBuffer();
            } else {
                srMaterial* material = (*record->materials)[avt[0]].get();
                setMaterial(material);
                sub_batch_offset = 0;
                while (sub_batch_offset < batch_count) {
                    srMaterial* next = (*record->materials)[avt[sub_batch_offset]].get();
                    if (next != material) {
                        setMaterial(next);
                        material = next;
                    }
                    {
                        const w8_ulong* indices = avt + 1 + sub_batch_offset;
                        const w8_ulong index_count = batch_count - sub_batch_offset - 1;
                        w8_ulong same = 0;
                        while (same < index_count &&
                               (*record->materials)[indices[same]].get() == material) {
                            ++same;
                        }
                        this->vertex_count = same + 1;
                    }
                    processVertexBuffer();
                    sub_batch_offset += this->vertex_count;
                }
            }
        }
        for (record_index = 1; record_index < input.records.size(); ++record_index) {
            glm::vec4* destination = input.vertex_arrays[record_index].eye_locations + batch_base;
            glm::vec4* source = eye_space_locations + batch_base;
            if (((batch_count * 0x10) != 0) && (destination != source)) {
                std::copy_n(source, batch_count, destination);
            }
        }
        batch_base += 0x40;
        vertex_count = static_cast<w8_ulong>(input.active_vertices.size());
        if (batch_base >= vertex_count) {
            break;
        }
    }
}

// FUNCTION: SURRENDER 0x1002B200
void srVertexPipe::finishDiffuseAlpha()
{
    if ((channel_mask & ((1 << srVertexProcessor::CHANNEL_DIFFUSE) |
                         (1 << srVertexProcessor::CHANNEL_ALPHA))) == 0) {
        return;
    }
    glm::vec4* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
    if (((lazy_setup_mask & ((1UL << srVertexProcessor::CHANNEL_DIFFUSE) |
                             (1UL << srVertexProcessor::CHANNEL_ALPHA))) == 0) &&
        ((current_record->flags &
          (srVertexPipe::Record::HAS_COLORS | srVertexPipe::Record::HAS_ALPHA)) == 0)) {
        glm::vec4 color = input->ambient_light * material_info.ambient + material_info.emissive;
        color.w = material_info.diffuse.w;
        const auto color_value = color;
        color = glm::mix(glm::clamp(color_value, 0.0f, 1.0f), glm::vec4(0),
                         glm::lessThanEqual(color_value, glm::vec4(0)));
        if ((current_record->flags & srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS) != 0) {
            for (std::size_t i = 0; i < vertex_count; ++i) {
                diffuse[i] = color * current_record->spec_for_diffuse[avt[sub_batch_offset + i]];
            }

            return;
        }
        if (vertex_count != 0) {
            std::fill_n(diffuse, vertex_count, color);
        }
        return;
    }
    if ((channel_mask & (1 << srVertexProcessor::CHANNEL_DIFFUSE)) != 0) {
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
            setupDiffuse();
        }
    }
    float* alpha;
    if ((channel_mask & (1 << srVertexProcessor::CHANNEL_ALPHA)) != 0) {
        if ((current_record->flags & srVertexPipe::Record::HAS_ALPHA) != 0) {
            alpha = scratch.alpha + sub_batch_offset;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
                if (vertex_count != 0) {
                    std::transform(
                        avt + sub_batch_offset, avt + sub_batch_offset + vertex_count, alpha,
                        [this](w8_ulong index) { return current_record->alpha_source[index]; });
                }
                lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_ALPHA);
            } else
                for (std::size_t i = 0; i < vertex_count; ++i) {
                    alpha[i] = alpha[i] * current_record->alpha_source[avt[sub_batch_offset + i]];
                }
        }
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
            float opacity = material_info.diffuse.w;
            if (opacity <= 0.0f) {
                opacity = 0.0f;
            } else if (opacity >= 1.0f) {
                opacity = 1.0f;
            }
            for (std::size_t i = 0; i < vertex_count; ++i) diffuse[i].w = opacity;

        } else {
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
                setupAlpha();
            }
            alpha = scratch.alpha + sub_batch_offset;
            ScaleAndStoreCoverage({alpha, vertex_count}, diffuse, material_info.diffuse.w,
                                  (channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) ==
                                      0);
        }
    }
    if (((channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) != 0) &&
        (vertex_count != 0)) {
        for (std::size_t i = 0; i < vertex_count; ++i)
            diffuse[i] = glm::clamp(diffuse[i], 0.0f, 1.0f);
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS) != 0) &&
        (vertex_count != 0))
        MultiplyIndexedColors({diffuse, vertex_count}, current_record->spec_for_diffuse,
                              {avt + sub_batch_offset, vertex_count});
}

// FUNCTION: SURRENDER 0x1002B5F0
void srVertexPipe::finishSpecularFog()
{
    w8_ulong specular = channel_mask & (1 << srVertexProcessor::CHANNEL_SPECULAR);
    if ((specular == 0) && ((channel_mask & (1 << srVertexProcessor::CHANNEL_FOG)) == 0)) {
        return;
    }
    glm::vec4* destination = vertex_array->specular + batch_base + sub_batch_offset;
    if ((lazy_setup_mask & ((1UL << srVertexProcessor::CHANNEL_SPECULAR) |
                            (1UL << srVertexProcessor::CHANNEL_FOG))) == 0) {
        const glm::vec4 zero(0);
        std::fill_n(destination, vertex_count, zero);
        return;
    }
    if (specular == 0) {
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
            for (std::size_t i = 0; i < vertex_count; ++i) destination[i].w = (0.0f);

        } else {
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
                setupFog();
            }
            float* fog = scratch.fog + sub_batch_offset;
            ScaleAndStoreCoverage({fog, vertex_count}, destination, material_info.fog_scale, true);
        }
    } else {
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
            setupSpecular();
        }
        if ((channel_mask & (1 << srVertexProcessor::CHANNEL_FOG)) != 0) {
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
                setupFog();
            }
            float* fog = scratch.fog + sub_batch_offset;
            ScaleAndStoreCoverage({fog, vertex_count}, destination, material_info.fog_scale, false);
        }
        if (vertex_count != 0) {
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i] = glm::clamp(destination[i], 0.0f, 1.0f);
        }
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS) != 0) &&
        (vertex_count != 0))
        MultiplyIndexedColors({destination, vertex_count}, current_record->spec_for_specular,
                              {avt + sub_batch_offset, vertex_count});
}

// FUNCTION: SURRENDER 0x1002B860
void srVertexPipe::setupEyeSpaceNormal()
{
    if (input->normals.empty()) {
        std::fill_n(scratch.normals, batch_count, glm::vec3(0, 0, -1));
    } else {
        TransformGeometry(*input, input->normals, input->normal_matrix,
                          std::span{scratch.normals, batch_count}, batch_base);
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_NORMALS;
}

// FUNCTION: SURRENDER 0x1002B910
void srVertexPipe::setupEyeSpaceDirAndDist()
{
    for (std::size_t i = 0; i < batch_count; ++i) {
        const auto v = glm::vec3(eye_space_locations[batch_base + i]);
        const auto sample =
            srLightingMath::directionAndDistance(v, srLightingMath::ZeroLengthDirection::divide);
        scratch.dist[i] = sample.distance;
        scratch.dir[i] = sample.direction;
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_DIRECTION;
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_DISTANCE;
}

// FUNCTION: SURRENDER 0x1002B970
void srVertexPipe::setupEyeSpaceZDist()
{
    const glm::vec4* locations = eye_space_locations + batch_base;
    for (w8_ulong index = 0; index < batch_count; ++index) {
        scratch.z_dist[index] = locations[index].z;
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_Z_DISTANCE;
}

// FUNCTION: SURRENDER 0x1002BA20
void srVertexPipe::setupAlpha()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
        std::fill_n(scratch.alpha + sub_batch_offset, vertex_count, 1.0f);
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_ALPHA);
    }
}

// FUNCTION: SURRENDER 0x1002BA70
void srVertexPipe::setupFog()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
        if (vertex_count != 0) {
            std::fill_n(scratch.fog + sub_batch_offset, vertex_count, 0.0f);
        }
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_FOG);
    }
}

// FUNCTION: SURRENDER 0x1002BAB0
void srVertexPipe::applyFog(std::span<const float> values)
{
    assert(values.size() >= vertex_count);

    float* fog = scratch.fog + sub_batch_offset;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) != 0) {
        for (w8_ulong i = 0; i < vertex_count; ++i)
            fog[i] = fog[i] * (1.0f - values[i]) + values[i];

        return;
    }
    if ((vertex_count != 0) && (fog != values.data())) {
        std::copy_n(values.begin(), vertex_count, fog);
    }
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_FOG);
}

// FUNCTION: SURRENDER 0x1002BCC0
void srVertexPipe::applyDiffuseLight(std::span<const float> values, const glm::vec4& light)
{
    assert(values.size() >= vertex_count);
    const auto diffuse = getDiffuse();
    srLightingMath::accumulateLight(diffuse, light, values.first(vertex_count));
}

// FUNCTION: SURRENDER 0x1002BD30
void srVertexPipe::copySpecularToDiffuse()
{
    copyColorChannel(getSpecular(), vertex_array->diffuse + batch_base + sub_batch_offset,
                     srVertexProcessor::CHANNEL_DIFFUSE);
}

// FUNCTION: SURRENDER 0x1002BDB0
void srVertexPipe::copyDiffuseToSpecular()
{
    copyColorChannel(getDiffuse(), vertex_array->specular + batch_base + sub_batch_offset,
                     srVertexProcessor::CHANNEL_SPECULAR);
}

// FUNCTION: SURRENDER 0x1002BE30
void srVertexPipe::swapDiffuseAndSpecular()
{
    const auto specular = getSpecular();
    const auto diffuse = getDiffuse();
    std::ranges::swap_ranges(diffuse, specular);
}

// FUNCTION: SURRENDER 0x1002BEC0
void srVertexPipe::applyDiffuseLight(const glm::vec4& light)
{
    const auto diffuse = getDiffuse();
    srLightingMath::accumulateLight(diffuse, light);
}

// FUNCTION: SURRENDER 0x1002BF20
void srVertexPipe::Record::ColorSource::copyDiffuseColors(glm::vec4* destination,
                                                          const w8_ulong* indices,
                                                          w8_ulong count) const
{
    if (colors == 0) {
        const glm::vec4 zero(0);
        std::fill_n(destination, count, zero);
        return;
    }
    if (format == FORMAT_ARGB) {
        const auto* argb = static_cast<const srARGB*>(colors);
        for (std::size_t i = 0; i < count; ++i) {
            const srARGB& color = argb[indices[i]];
            destination[i] =
                glm::vec4(color.red, color.green, color.blue, color.alpha) * (1.0f / 255.0f);
        }
        return;
    }
    if (format == FORMAT_VECTOR3) {
        for (std::size_t i = 0; i < count; ++i)
            destination[i] = glm::vec4((static_cast<const glm::vec3*>(colors))[indices[i]], 1.0f);
        return;
    }
    if (format == FORMAT_VECTOR4) {
        for (std::size_t i = 0; i < count; ++i)
            destination[i] = (static_cast<const glm::vec4*>(colors))[indices[i]];
    }
}

// FUNCTION: SURRENDER 0x1002BFB0
void srVertexPipe::setupDiffuse()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        const glm::vec4 color =
            input->ambient_light * material_info.ambient + material_info.emissive;
        glm::vec4* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
        if ((current_record->flags & srVertexPipe::Record::HAS_COLORS) == 0) {
            if (vertex_count != 0) {
                std::fill_n(diffuse, vertex_count, color);
            }
        } else {
            current_record->color_source.copyDiffuseColors(diffuse, avt + sub_batch_offset,
                                                           vertex_count);
            if (color != glm::vec4(0.0f) && vertex_count != 0) {
                srLightingMath::accumulateLight({diffuse, vertex_count}, color);
            }
        }
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_DIFFUSE);
    }
}

/* Depth cue is cached for the whole batch, shared by all material sub-batches. */
// FUNCTION: SURRENDER 0x1002C0D0
void srVertexPipe::setupDepthCue()
{
    float* depth_cue = this->scratch.depth_cue;
    float minimum = input->environment_minimum;
    float maximum = input->environment_maximum;
    float near_value = 1.0f - input->environment_scale;
    float far_value = 1.0f - input->environment_inverse_scale;
    if ((scratch.flags & srVertexPipe::Scratch::READY_EYE_DISTANCE) == 0) {
        setupEyeSpaceDirAndDist();
    }
    w8_ulong count = batch_count;
    float* dist = scratch.dist;
    if (minimum == maximum) {
        if (near_value == far_value) {
            std::fill_n(depth_cue, count, near_value);
        } else if (count != 0) {
            float* source = dist;
            float* out = depth_cue;
            do {
                float value = far_value;
                if (*source < minimum) {
                    value = near_value;
                }
                *out = value;
                ++out;
                ++source;
                --count;
            } while (count != 0);
        }
    } else {
        srChannelMath::distanceRamp({depth_cue, count}, {dist, count}, maximum,
                                    1.0f / (maximum - minimum), near_value - far_value,
                                    srChannelMath::RampDirection::decreasing);
        srChannelMath::add({depth_cue, count}, 1.0f - near_value);
    }
    scratch.flags |= srVertexPipe::Scratch::READY_DEPTH_CUE;
}

// FUNCTION: SURRENDER 0x1002C300
w8_ulong srVertexPipe::getExclusionMask() const
{
    return input->exclusion_mask;
}

// FUNCTION: SURRENDER 0x1002C350
int srVertexPipe::testEyeSpaceBounds(const glm::vec3& center, float radius) const
{
    const auto offset = center - input->eye_bounds.center;
    const float sum = radius + input->eye_bounds.radius;
    return glm::dot(offset, offset) < sum * sum;
}

// FUNCTION: SURRENDER 0x1002C3B0
void srVertexPipe::setupSpecular()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        const glm::vec4 zero(0);
        std::fill_n(vertex_array->specular + batch_base + sub_batch_offset, vertex_count, zero);
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
    }
}

// FUNCTION: SURRENDER 0x1002C400
void srVertexPipe::setupST(w8_ulong index)
{
    if (((current_record->flags & (srVertexPipe::Record::HAS_TEXCOORD0 << index)) != 0)) {
        const glm::vec2* source = current_record->st_source[index];
        if (source != 0) {
            auto* destination = (index == 0 ? vertex_array->st0 : vertex_array->st1) + batch_base +
                                sub_batch_offset;
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i] = source[avt[sub_batch_offset + i]];
        }
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
}

// FUNCTION: SURRENDER 0x1002C480
srBoundingSphere srVertexPipe::getEyeSpaceBoundingSphere() const
{
    return input->eye_bounds;
}

// FUNCTION: SURRENDER 0x1002C4C0
const srVertexProcessor::MaterialInfo& srVertexPipe::getMaterialInfo() const
{
    return material_info;
}

// FUNCTION: SURRENDER 0x1002C500
void srVertexPipe::setupQ(w8_ulong index)
{
    std::fill_n((&vertex_array->q0)[index] + batch_base + sub_batch_offset, vertex_count, 1.0f);
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_Q0);
}

// FUNCTION: SURRENDER 0x1002C560
std::span<const w8_ulong> srVertexPipe::getAVT() const
{
    return {avt + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C570
std::span<const glm::vec4> srVertexPipe::getEyeSpaceLocation()
{
    return {eye_space_locations + batch_base + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C590
std::span<const float> srVertexPipe::getDepthCue()
{
    return scratchChannel(scratch.depth_cue, Scratch::READY_DEPTH_CUE,
                          &srVertexPipe::setupDepthCue);
}

// FUNCTION: SURRENDER 0x1002C5F0
std::span<const glm::vec3> srVertexPipe::getEyeSpaceDir()
{
    return scratchChannel(scratch.dir, Scratch::READY_EYE_DIRECTION,
                          &srVertexPipe::setupEyeSpaceDirAndDist);
}

// FUNCTION: SURRENDER 0x1002C620
std::span<const float> srVertexPipe::getEyeSpaceDist()
{
    return scratchChannel(scratch.dist, Scratch::READY_EYE_DISTANCE,
                          &srVertexPipe::setupEyeSpaceDirAndDist);
}

// FUNCTION: SURRENDER 0x1002C650
std::span<const float> srVertexPipe::getEyeSpaceZDist()
{
    return scratchChannel(scratch.z_dist, Scratch::READY_EYE_Z_DISTANCE,
                          &srVertexPipe::setupEyeSpaceZDist);
}

// FUNCTION: SURRENDER 0x1002C680
std::span<float> srVertexPipe::getFog()
{
    prepareOutputChannel(srVertexProcessor::CHANNEL_FOG, &srVertexPipe::setupFog);
    return {scratch.fog + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C6C0
std::span<float> srVertexPipe::getAlpha()
{
    prepareOutputChannel(srVertexProcessor::CHANNEL_ALPHA, &srVertexPipe::setupAlpha);
    return {scratch.alpha + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C700
std::span<glm::vec4> srVertexPipe::getDiffuse()
{
    prepareOutputChannel(srVertexProcessor::CHANNEL_DIFFUSE, &srVertexPipe::setupDiffuse);
    return {vertex_array->diffuse + batch_base + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C740
std::span<glm::vec4> srVertexPipe::getSpecular()
{
    prepareOutputChannel(srVertexProcessor::CHANNEL_SPECULAR, &srVertexPipe::setupSpecular);
    return {vertex_array->specular + batch_base + sub_batch_offset, vertex_count};
}

// FUNCTION: SURRENDER 0x1002C7F0
float* srVertexPipe::getQ(w8_ulong index, int create)
{
    if ((create == 0) &&
        ((lazy_setup_mask & (1 << (index + srVertexProcessor::CHANNEL_Q0))) == 0)) {
        setupQ(index);
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_Q0);
    return (&vertex_array->q0)[index] + batch_base + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C850
void* srVertexPipe::getUserArray(w8_ulong index)
{
    return current_record->user[index];
}

void srVertexPipe::prepareOutputChannel(srVertexProcessor::e_channel channel, SetupChannel setup)
{
    if ((lazy_setup_mask & (1u << channel)) == 0) (this->*setup)();
}

void srVertexPipe::copyColorChannel(std::span<const glm::vec4> source, glm::vec4* destination,
                                    srVertexProcessor::e_channel channel)
{
    if (source.data() != destination) std::ranges::copy(source, destination);
    lazy_setup_mask |= 1u << channel;
}

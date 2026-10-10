#include "surrender/srMaterial.h" // IWYU pragma: keep - complete type for srMaterial to srMaterialIFace conversion.
#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srVertexPipe.h"
#include "surrender/srCore.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srShader.h"
#include "surrender/srMath.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srARGB.h"
#include "surrender/srFlags.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srVertexProcessor.h"

// FUNCTION: SURRENDER 0x1002AA50
srFlags<srVertexProcessor::e_channel> srVertexPipe::getShaderDisableMask(const srShader& shader)
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
        return srFlags<srVertexProcessor::e_channel>(
            disable | (1 << srVertexProcessor::CHANNEL_ST0) |
            (1 << srVertexProcessor::CHANNEL_ST1) | (1 << srVertexProcessor::CHANNEL_Q0) |
            (1 << srVertexProcessor::CHANNEL_Q1));
    }
    if ((shader.value & (srShader::MASK_DETAILCOLOR0 | srShader::MASK_DETAILALPHA0 |
                         srShader::MASK_DETAILCOLOR1 | srShader::MASK_DETAILALPHA1)) == 0) {
        disable |= (1 << srVertexProcessor::CHANNEL_ST1) | (1 << srVertexProcessor::CHANNEL_Q1);
    }
    return srFlags<srVertexProcessor::e_channel>(disable);
}

// FUNCTION: SURRENDER 0x1002AAD0
srFlags<srVertexProcessor::e_channel>
srVertexPipe::getShaderDisableMask(const srShader* shaders, const w8_ulong* channels,
                                   w8_ulong channel_count)
{
    srShader shader;
    shader.value = shaders[channels[0]].value;
    w8_ulong available = ~getShaderDisableMask(shader).value;
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
            available |= ~getShaderDisableMask(shader).value;
            if (available == 0xffffffff) {
                return srFlags<srVertexProcessor::e_channel>(0);
            }
        }
    }
    return srFlags<srVertexProcessor::e_channel>(~available);
}

// FUNCTION: SURRENDER 0x1002AB80
w8_ulong srVertexPipe::scanChangeIndexed(const w8_ulong* table, w8_ulong value,
                                              const w8_ulong* indices,
                                              w8_ulong index_count)
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
    if ((mask & (1 << srVertexProcessor::CHANNEL_ST0)) != 0) {
        if ((lazy_setup_mask & (1 << srVertexProcessor::CHANNEL_Q0)) == 0) {
            mask &= 0xffffff7f;
        }
        srCore.getStatisticsManager()->statistics.texture_coordinate_operations += vertex_count;
        if ((lazy_setup_mask & (1 << srVertexProcessor::CHANNEL_ST0)) == 0) {
            setupST(0);
        }
        w8_ulong lazy = lazy_setup_mask | (1 << srVertexProcessor::CHANNEL_ST0);
        lazy_setup_mask = lazy;
        if ((lazy & (1 << srVertexProcessor::CHANNEL_Q0)) == 0) {
            setupQ(0);
        }
        lazy_setup_mask |= 1 << srVertexProcessor::CHANNEL_Q0;
    }
    if ((channel_mask & (1 << srVertexProcessor::CHANNEL_ST1)) != 0) {
        if ((lazy_setup_mask & (1 << srVertexProcessor::CHANNEL_Q1)) == 0) {
            mask &= 0xfffffeff;
        }
        srCore.getStatisticsManager()->statistics.texture_coordinate_operations += vertex_count;
        if ((lazy_setup_mask & (1 << srVertexProcessor::CHANNEL_ST1)) == 0) {
            setupST(1);
        }
        w8_ulong lazy = lazy_setup_mask | (1 << srVertexProcessor::CHANNEL_ST1);
        lazy_setup_mask = lazy;
        if ((lazy & (1 << srVertexProcessor::CHANNEL_Q1)) == 0) {
            setupQ(1);
        }
        lazy_setup_mask |= 1 << srVertexProcessor::CHANNEL_Q1;
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
void srVertexPipe::setMaterial(srMaterialIFace* material)
{
    if (material == 0) {
        material = srCore.getMaterial();
    }
    if (material != this->material) {
        srCore.getStatisticsManager()->statistics.material_processing_stalls += 1;
        this->material = material;
        material->getMaterialInfo(material_info);
    }
}

// FUNCTION: SURRENDER 0x1002AEC0
void srVertexPipe::process(const Input& input)
{
    this->input = &input;
    active_processors.clear();
    active_processors.reserve(input.processor_count);
    for (w8_ulong index = 0; index < input.processor_count; ++index) {
        srVertexProcessor* processor = input.processors[index];
        if (processor->isActive(*this)) {
            active_processors.push_back(processor);
        }
    }
    setMaterial(0);
    eye_space_locations = input.vertex_arrays->eye_locations;
    batch_base = 0;
    w8_ulong vertex_count = input.vertex_count;
    if (vertex_count == 0)
        return;
    const auto model_view = (*input.model_view);
    while (vertex_count != 0) {
        vertex_count -= batch_base;
        batch_count = vertex_count;
        if (0x40 < vertex_count) {
            batch_count = 0x40;
        }
        avt = input.active_vertices + batch_base;
        scratch.flags = 0;
        for (w8_ulong i = 0; i < batch_count; ++i) {
            const auto index = input.direct_vertex_indices ? batch_base + i : avt[i];
            eye_space_locations[batch_base + i] =
                model_view * glm::vec4(input.positions[index].glmValue(), 1.0f);
        }
        w8_ulong record_index;
        for (record_index = 0; record_index < input.records.size(); ++record_index) {
            const Record* record = &input.records[record_index];
            current_record = record;
            vertex_array = input.vertex_arrays + record_index;
            if ((record->flags & Record::HAS_VERTEX_MATERIALS) == 0) {
                this->vertex_count = batch_count;
                sub_batch_offset = 0;
                setMaterial(record->material);
                processVertexBuffer();
            } else {
                srMaterialIFace* material = record->materials[avt[0]].get();
                setMaterial(material);
                sub_batch_offset = 0;
                while (sub_batch_offset < batch_count) {
                    srMaterialIFace* next = record->materials[avt[sub_batch_offset]].get();
                    if (next != material) {
                        setMaterial(next);
                        material = next;
                    }
                    {
                        const w8_ulong* indices = avt + 1 + sub_batch_offset;
                        const w8_ulong index_count = batch_count - sub_batch_offset - 1;
                        w8_ulong same = 0;
                        while (same < index_count &&
                               record->materials[indices[same]].get() == material) {
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
            glm::vec4* destination =
                input.vertex_arrays[record_index].eye_locations + batch_base;
            glm::vec4* source = eye_space_locations + batch_base;
            if (((batch_count * 0x10) != 0) && (destination != source)) {
                std::copy_n(source, batch_count, destination);
            }
        }
        batch_base += 0x40;
        vertex_count = input.vertex_count;
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
        srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
        glm::vec4 color;
        color.x = input->ambient_light.x * material_info.ambient.x + material_info.emissive.x;
        color.y = input->ambient_light.y * material_info.ambient.y + material_info.emissive.y;
        color.z = input->ambient_light.z * material_info.ambient.z + material_info.emissive.z;
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
        srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
            setupDiffuse();
        }
    }
    float* alpha;
    if ((channel_mask & (1 << srVertexProcessor::CHANNEL_ALPHA)) != 0) {
        srCore.getStatisticsManager()->statistics.alpha_operations += vertex_count;
        if ((current_record->flags & srVertexPipe::Record::HAS_ALPHA) != 0) {
            alpha = scratch.alpha + sub_batch_offset;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
                if (vertex_count != 0) {
                    std::transform(avt + sub_batch_offset,
                                   avt + sub_batch_offset + vertex_count, alpha,
                                   [this](w8_ulong index) {
                                       return current_record->alpha_source[index];
                                   });
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
            for (std::size_t i = 0; i < vertex_count; ++i)
                diffuse[i].w = opacity;

        } else {
            srCore.getStatisticsManager()->statistics.alpha_operations += vertex_count;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
                setupAlpha();
            }
            alpha = scratch.alpha + sub_batch_offset;
            if ((channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
                for (std::size_t i = 0; i < vertex_count; ++i)
                    alpha[i] = glm::clamp(alpha[i], 0.0f, 1.0f);
            }
            float opacity = material_info.diffuse.w;
            if ((vertex_count != 0) && (opacity != 1.0f)) {
                if (opacity == 0.0f) {
                    std::fill_n(alpha, vertex_count, 0.0f);
                } else {
                    for (std::size_t i = 0; i < vertex_count; ++i)
                        alpha[i] = opacity * alpha[i];
                }
            }
            for (std::size_t i = 0; i < vertex_count; ++i)
                diffuse[i].w = alpha[i];
        }
    }
    if (((channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) != 0) &&
        (vertex_count != 0)) {
        for (std::size_t i = 0; i < vertex_count; ++i)
            diffuse[i] = glm::clamp(diffuse[i], 0.0f, 1.0f);
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS) != 0) &&
        (vertex_count != 0))
        for (std::size_t i = 0; i < vertex_count; ++i) {
            diffuse[i] = diffuse[i] * current_record->spec_for_diffuse[avt[sub_batch_offset + i]];
    }
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
        srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
        const glm::vec4 zero(0);
        std::fill_n(destination, vertex_count, zero);
        return;
    }
    if (specular == 0) {
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i].w = (0.0f);

        } else {
            srCore.getStatisticsManager()->statistics.fog_operations += vertex_count;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
                setupFog();
            }
            float* fog = scratch.fog + sub_batch_offset;
            for (std::size_t i = 0; i < vertex_count; ++i)
                fog[i] = glm::clamp(fog[i], 0.0f, 1.0f);
            float scale = material_info.fog_scale;
            if ((vertex_count != 0) && (scale != 1.0f)) {
                if (scale == 0.0f) {
                    std::fill_n(fog, vertex_count, 0.0f);
                } else {
                    for (std::size_t i = 0; i < vertex_count; ++i)
                        fog[i] = scale * fog[i];
                }
            }
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i].w = fog[i];
        }
    } else {
        srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
            setupSpecular();
        }
        if ((channel_mask & (1 << srVertexProcessor::CHANNEL_FOG)) != 0) {
            srCore.getStatisticsManager()->statistics.fog_operations += vertex_count;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
                setupFog();
            }
            float* fog = scratch.fog + sub_batch_offset;
            float scale = material_info.fog_scale;
            if ((vertex_count != 0) && (scale != 1.0f)) {
                if (scale == 0.0f) {
                    std::fill_n(fog, vertex_count, 0.0f);
                } else {
                    for (std::size_t i = 0; i < vertex_count; ++i)
                        fog[i] = scale * fog[i];
                }
            }
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i].w = fog[i];
        }
        if (vertex_count != 0) {
            for (std::size_t i = 0; i < vertex_count; ++i)
                destination[i] = glm::clamp(destination[i], 0.0f, 1.0f);
        }
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS) != 0) &&
        (vertex_count != 0))
        for (std::size_t i = 0; i < vertex_count; ++i) {
            destination[i] =
                destination[i] * current_record->spec_for_specular[avt[sub_batch_offset + i]];
    }
}

// FUNCTION: SURRENDER 0x1002B860
void srVertexPipe::setupEyeSpaceNormal()
{
    const srVector3T<float>* normals = input->normals;
    if (normals == 0) {
        std::fill_n(scratch.normals, batch_count, glm::vec3(0, 0, -1));
    } else {
        const auto normal_transform = (*input->normal_matrix);
        for (w8_ulong i = 0; i < batch_count; ++i) {
            const auto index = input->direct_vertex_indices ? batch_base + i : avt[i];
            scratch.normals[i] = glm::vec3(
                normal_transform * glm::vec4(normals[index].glmValue(), 1.0f));
        }
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_NORMALS;
}

// FUNCTION: SURRENDER 0x1002B910
void srVertexPipe::setupEyeSpaceDirAndDist()
{
    for (std::size_t i = 0; i < batch_count; ++i) {
        const auto v = glm::vec3(eye_space_locations[batch_base + i]);
        scratch.dist[i] = glm::length(v);
        scratch.dir[i] = v / scratch.dist[i];
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
void srVertexPipe::applyFog(const float* values)
{
    srCore.getStatisticsManager()->statistics.fog_operations += vertex_count;
    float* fog = scratch.fog + sub_batch_offset;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) != 0) {
        for (w8_ulong i = 0; i < vertex_count; ++i)
            fog[i] = fog[i] * (1.0f - values[i]) + values[i];

        return;
    }
    if ((vertex_count != 0) && (fog != values)) {
        std::copy_n(values, vertex_count, fog);
    }
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_FOG);
}

// FUNCTION: SURRENDER 0x1002BCC0
void srVertexPipe::applyDiffuseLight(const float* values, const glm::vec4& light)
{
    glm::vec4* diffuse = getDiffuse();
    for (w8_ulong i = 0; i < vertex_count; ++i)
        diffuse[i] = diffuse[i] + light * values[i];
}

// FUNCTION: SURRENDER 0x1002BD30
void srVertexPipe::copySpecularToDiffuse()
{
    glm::vec4* source = getSpecular();
    glm::vec4* destination = vertex_array->diffuse + batch_base + sub_batch_offset;
    if (source != destination) {
        std::copy_n(source, vertex_count, destination);
    }
    lazy_setup_mask |= 1u << srVertexProcessor::CHANNEL_DIFFUSE;
}

// FUNCTION: SURRENDER 0x1002BDB0
void srVertexPipe::copyDiffuseToSpecular()
{
    glm::vec4* source = getDiffuse();
    glm::vec4* destination = vertex_array->specular + batch_base + sub_batch_offset;
    if (source != destination) {
        std::copy_n(source, vertex_count, destination);
    }
    lazy_setup_mask |= 1u << srVertexProcessor::CHANNEL_SPECULAR;
}

// FUNCTION: SURRENDER 0x1002BE30
void srVertexPipe::swapDiffuseAndSpecular()
{
    srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        setupSpecular();
    }
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    w8_ulong offset = batch_base + sub_batch_offset;
    std::swap_ranges(vertex_array->diffuse + offset,
                     vertex_array->diffuse + offset + vertex_count,
                     vertex_array->specular + offset);
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
    lazy_setup_mask |= ((1UL << srVertexProcessor::CHANNEL_DIFFUSE) |
                        (1UL << srVertexProcessor::CHANNEL_SPECULAR));
}

// FUNCTION: SURRENDER 0x1002BEC0
void srVertexPipe::applyDiffuseLight(const glm::vec4& light)
{
    glm::vec4* diffuse = getDiffuse();
    for (std::size_t i = 0; i < vertex_count; ++i)
        diffuse[i] = light + diffuse[i];
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
        for (std::size_t i = 0; i < count; ++i)
            destination[i] = glm::vec4((static_cast<const srARGB*>(colors))[indices[i]].red,
                                       (static_cast<const srARGB*>(colors))[indices[i]].green,
                                       (static_cast<const srARGB*>(colors))[indices[i]].blue,
                                       (static_cast<const srARGB*>(colors))[indices[i]].alpha) *
                   (1.0f / 255.0f);
        return;
    }
    if (format == FORMAT_VECTOR3) {
        for (std::size_t i = 0; i < count; ++i)
            destination[i] =
                glm::vec4((static_cast<const srVector3*>(colors))[indices[i]].glmValue(), 1.0f);
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
        glm::vec4 color;
        color.x = input->ambient_light.x * material_info.ambient.x + material_info.emissive.x;
        color.y = input->ambient_light.y * material_info.ambient.y + material_info.emissive.y;
        color.z = input->ambient_light.z * material_info.ambient.z + material_info.emissive.z;
        color.w = input->ambient_light.w * material_info.ambient.w + material_info.emissive.w;
        glm::vec4* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
        if ((current_record->flags & srVertexPipe::Record::HAS_COLORS) == 0) {
            if (vertex_count != 0) {
                std::fill_n(diffuse, vertex_count, color);
            }
        } else {
            current_record->color_source.copyDiffuseColors(diffuse, avt + sub_batch_offset,
                                                           vertex_count);
            if ((((color.x != 0.0f) || (color.y != 0.0f)) ||
                 ((color.z != 0.0f) || (color.w != 0.0f))) &&
                (vertex_count != 0)) {
                for (std::size_t i = 0; i < vertex_count; ++i)
                    diffuse[i] = color + diffuse[i];
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
        if (count != 0) {
            if (maximum == 0.0f) {
                for (std::size_t i = 0; i < count; ++i)
                    depth_cue[i] = -dist[i];
            } else {
                for (std::size_t i = 0; i < count; ++i)
                    depth_cue[i] = maximum - dist[i];
            }
        }
        float scale = 1.0f / (maximum - minimum);
        if (count != 0) {
            if (scale != 1.0f) {
                if (scale == 0.0f) {
                    std::fill_n(depth_cue, count, 0.0f);
                } else {
                    for (std::size_t i = 0; i < count; ++i)
                        depth_cue[i] = scale * depth_cue[i];
                }
            }
            for (std::size_t i = 0; i < count; ++i)
                depth_cue[i] = glm::clamp(depth_cue[i], 0.0f, 1.0f);
        }
        float range = near_value - far_value;
        if (((range != 1.0f) && (count != 0)) && (range != 1.0f)) {
            if (range == 0.0f) {
                std::fill_n(depth_cue, count, 0.0f);
            } else {
                for (std::size_t i = 0; i < count; ++i)
                    depth_cue[i] = range * depth_cue[i];
            }
        }
        if (((near_value != 1.0f) && (count != 0)) && (1.0f - near_value != 0.0f)) {
            for (std::size_t i = 0; i < count; ++i)
                depth_cue[i] = (1.0f - near_value) + depth_cue[i];
        }
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
    const auto offset = center - input->eye_center;
    const float sum = radius + input->eye_radius;
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
            for (std::size_t i = 0; i < vertex_count; ++i)
                ((&vertex_array->st0)[index] + batch_base + sub_batch_offset)[i] =
                    (reinterpret_cast<const glm::vec2*>(source))[avt[sub_batch_offset + i]];
        }
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
}

// FUNCTION: SURRENDER 0x1002C480
void srVertexPipe::getEyeSpaceBoundingSphere(glm::vec3& center, float& radius) const
{
    center = input->eye_center;
    radius = input->eye_radius;
}

// FUNCTION: SURRENDER 0x1002C4C0
const srVertexProcessor::MaterialInfo& srVertexPipe::getMaterialInfo() const
{
    return material_info;
}

// FUNCTION: SURRENDER 0x1002C4D0
srFlags<srVertexProcessor::e_channel> srVertexPipe::getChannelMask() const
{
    return srFlags<srVertexProcessor::e_channel>(channel_mask);
}

// FUNCTION: SURRENDER 0x1002C500
void srVertexPipe::setupQ(w8_ulong index)
{
    std::fill_n((&vertex_array->q0)[index] + batch_base + sub_batch_offset, vertex_count, 1.0f);
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_Q0);
}

// FUNCTION: SURRENDER 0x1002C560
const w8_ulong* srVertexPipe::getAVT() const
{
    return avt + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C570
const glm::vec4* srVertexPipe::getEyeSpaceLocation()
{
    return eye_space_locations + batch_base + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C590
const float* srVertexPipe::getDepthCue()
{
    if ((scratch.flags & srVertexPipe::Scratch::READY_DEPTH_CUE) == 0) {
        setupDepthCue();
    }
    return scratch.depth_cue + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C5F0
const glm::vec3* srVertexPipe::getEyeSpaceDir()
{
    if ((scratch.flags & srVertexPipe::Scratch::READY_EYE_DIRECTION) == 0) {
        setupEyeSpaceDirAndDist();
    }
    return scratch.dir + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C620
const float* srVertexPipe::getEyeSpaceDist()
{
    if ((scratch.flags & srVertexPipe::Scratch::READY_EYE_DISTANCE) == 0) {
        setupEyeSpaceDirAndDist();
    }
    return scratch.dist + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C650
const float* srVertexPipe::getEyeSpaceZDist()
{
    if ((scratch.flags & srVertexPipe::Scratch::READY_EYE_Z_DISTANCE) == 0) {
        setupEyeSpaceZDist();
    }
    return scratch.z_dist + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C680
float* srVertexPipe::getFog()
{
    srCore.getStatisticsManager()->statistics.fog_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
        setupFog();
    }
    return scratch.fog + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C6C0
float* srVertexPipe::getAlpha()
{
    srCore.getStatisticsManager()->statistics.alpha_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
        setupAlpha();
    }
    return scratch.alpha + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C700
glm::vec4* srVertexPipe::getDiffuse()
{
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    return vertex_array->diffuse + batch_base + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C740
glm::vec4* srVertexPipe::getSpecular()
{
    srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        setupSpecular();
    }
    return vertex_array->specular + batch_base + sub_batch_offset;
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

#include <algorithm>

#include "surrender/srVertexPipe.h"

#include "surrender/srCore.h"
#include "surrender/srMaterial.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srPalette.h"
#include "surrender/srShader.h"
#include "surrender/srMath.h"
#include <cassert>

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
    while (vertex_count != 0) {
        vertex_count -= batch_base;
        batch_count = vertex_count;
        if (0x40 < vertex_count) {
            batch_count = 0x40;
        }
        avt = input.active_vertices + batch_base;
        scratch.flags = 0;
        if (this->input->direct_vertex_indices == 0) {
            {
                std::span<srVector4> input_destination{eye_space_locations + batch_base,
                                                       batch_count};
                const srVector3* input_source = this->input->positions;
                std::span<const SRDWORD> input_indices{avt, batch_count};
                const srMatrix4& input_matrix = *this->input->model_view;
                const auto transform_matrix = input_matrix.glmValue();
                for (std::size_t i = 0; i < input_indices.size(); ++i)
                    input_destination[i] =
                        transform_matrix *
                        glm::vec4(input_source[input_indices[i]].glmValue(), 1.0f);
            }
        } else {
            {
                std::span<srVector4> input_destination{eye_space_locations + batch_base,
                                                       batch_count};
                std::span<const srVector3> input_vectors{this->input->positions + batch_base,
                                                         batch_count};
                const srMatrix4& input_matrix = *this->input->model_view;
                const auto transform_matrix = input_matrix.glmValue();
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i] =
                        transform_matrix * glm::vec4(input_vectors[i].glmValue(), 1.0f);
            }
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
            srVector4T<float>* destination =
                input.vertex_arrays[record_index].eye_locations + batch_base;
            srVector4T<float>* source = eye_space_locations + batch_base;
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
    srVector4T<float>* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
    if (((lazy_setup_mask & ((1UL << srVertexProcessor::CHANNEL_DIFFUSE) |
                             (1UL << srVertexProcessor::CHANNEL_ALPHA))) == 0) &&
        ((current_record->flags &
          (srVertexPipe::Record::HAS_COLORS | srVertexPipe::Record::HAS_ALPHA)) == 0)) {
        srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
        srVector4T<float> color;
        color.x = input->ambient_light.x * material_info.ambient.x + material_info.emissive.x;
        color.y = input->ambient_light.y * material_info.ambient.y + material_info.emissive.y;
        color.z = input->ambient_light.z * material_info.ambient.z + material_info.emissive.z;
        color.w = material_info.diffuse.w;
        {
            const auto color_value = color.glmValue();
            color = glm::mix(glm::clamp(color_value, float(0), float(1)), glm::vec4(0),
                             glm::lessThanEqual(color_value, glm::vec4(0)));
        }
        if ((current_record->flags & srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS) != 0) {
            {
                std::span<srVector4> input_destination{diffuse, vertex_count};
                const srVector4& input_constant = color;
                const srVector4* input_indexed_source = current_record->spec_for_diffuse;
                std::span<const SRDWORD> input_indices{avt + sub_batch_offset, vertex_count};
                for (std::size_t i = 0; i < input_indices.size(); ++i) {
                    input_destination[i] = input_constant.glmValue() *
                                           input_indexed_source[input_indices[i]].glmValue();
                }
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
            } else {
                {
                    std::span<float> input_destination{alpha, vertex_count};
                    std::span<const float> input_linear_source{alpha, vertex_count};
                    const float* input_indexed_source = current_record->alpha_source;
                    std::span<const SRDWORD> input_indices{avt + sub_batch_offset, vertex_count};
                    for (std::size_t i = 0; i < input_indices.size(); ++i) {
                        input_destination[i] =
                            input_linear_source[i] * input_indexed_source[input_indices[i]];
                    }
                }
            }
        }
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
            float opacity = material_info.diffuse.w;
            if (opacity <= 0.0f) {
                opacity = 0.0f;
            } else if (opacity >= 1.0f) {
                opacity = 1.0f;
            }
            {
                std::span<srVector4> input_destination{diffuse, vertex_count};
                float input_constant = opacity;
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i].w = input_constant;
            }
        } else {
            srCore.getStatisticsManager()->statistics.alpha_operations += vertex_count;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_ALPHA)) == 0) {
                setupAlpha();
            }
            alpha = scratch.alpha + sub_batch_offset;
            if ((channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
                std::ranges::transform(std::span<const float>{alpha, vertex_count}, alpha,
                                       [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
            }
            float opacity = material_info.diffuse.w;
            if ((vertex_count != 0) && (opacity != 1.0f)) {
                if (opacity == 0.0f) {
                    std::fill_n(alpha, vertex_count, 0.0f);
                } else {
                    std::ranges::transform(std::span<const float>{alpha, vertex_count}, alpha,
                                           [&](const auto& v) { return (opacity)*v; });
                }
            }
            {
                std::span<srVector4> input_destination{diffuse, vertex_count};
                std::span<const float> input_source{alpha, vertex_count};
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i].w = input_source[i];
            }
        }
    }
    if (((channel_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) != 0) &&
        (vertex_count != 0)) {
        std::ranges::transform(std::span<const srVector4>{diffuse, vertex_count}, diffuse,
                               [&](const auto& v) { return glm::clamp(v.glmValue(), 0.0f, 1.0f); });
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS) != 0) &&
        (vertex_count != 0)) {
        {
            std::span<srVector4> input_destination{diffuse, vertex_count};
            std::span<const srVector4> input_linear_source{diffuse, vertex_count};
            const srVector4* input_indexed_source = current_record->spec_for_diffuse;
            std::span<const SRDWORD> input_indices{avt + sub_batch_offset, vertex_count};
            for (std::size_t i = 0; i < input_indices.size(); ++i) {
                input_destination[i] = input_linear_source[i].glmValue() *
                                       input_indexed_source[input_indices[i]].glmValue();
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1002B5F0
void srVertexPipe::finishSpecularFog()
{
    w8_ulong specular = channel_mask & (1 << srVertexProcessor::CHANNEL_SPECULAR);
    if ((specular == 0) && ((channel_mask & (1 << srVertexProcessor::CHANNEL_FOG)) == 0)) {
        return;
    }
    srVector4T<float>* destination = vertex_array->specular + batch_base + sub_batch_offset;
    if ((lazy_setup_mask & ((1UL << srVertexProcessor::CHANNEL_SPECULAR) |
                            (1UL << srVertexProcessor::CHANNEL_FOG))) == 0) {
        srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
        srVector4T<float> zero;
        zero = 0.0f;
        std::fill_n(destination, vertex_count, zero);
        return;
    }
    if (specular == 0) {
        if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
            {
                std::span<srVector4> input_destination{destination, vertex_count};
                float input_constant = 0.0f;
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i].w = input_constant;
            }
        } else {
            srCore.getStatisticsManager()->statistics.fog_operations += vertex_count;
            if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_FOG)) == 0) {
                setupFog();
            }
            float* fog = scratch.fog + sub_batch_offset;
            std::ranges::transform(std::span<const float>{fog, vertex_count}, fog,
                                   [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
            float scale = material_info.fog_scale;
            if ((vertex_count != 0) && (scale != 1.0f)) {
                if (scale == 0.0f) {
                    std::fill_n(fog, vertex_count, 0.0f);
                } else {
                    std::ranges::transform(std::span<const float>{fog, vertex_count}, fog,
                                           [&](const auto& v) { return (scale)*v; });
                }
            }
            {
                std::span<srVector4> input_destination{destination, vertex_count};
                std::span<const float> input_source{fog, vertex_count};
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i].w = input_source[i];
            }
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
                    std::ranges::transform(std::span<const float>{fog, vertex_count}, fog,
                                           [&](const auto& v) { return (scale)*v; });
                }
            }
            {
                std::span<srVector4> input_destination{destination, vertex_count};
                std::span<const float> input_source{fog, vertex_count};
                for (std::size_t i = 0; i < input_destination.size(); ++i)
                    input_destination[i].w = input_source[i];
            }
        }
        if (vertex_count != 0) {
            std::ranges::transform(
                std::span<const srVector4>{destination, vertex_count}, destination,
                [&](const auto& v) { return glm::clamp(v.glmValue(), 0.0f, 1.0f); });
        }
    }
    if (((current_record->flags & srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS) != 0) &&
        (vertex_count != 0)) {
        {
            std::span<srVector4> input_destination{destination, vertex_count};
            std::span<const srVector4> input_linear_source{destination, vertex_count};
            const srVector4* input_indexed_source = current_record->spec_for_specular;
            std::span<const SRDWORD> input_indices{avt + sub_batch_offset, vertex_count};
            for (std::size_t i = 0; i < input_indices.size(); ++i) {
                input_destination[i] = input_linear_source[i].glmValue() *
                                       input_indexed_source[input_indices[i]].glmValue();
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1002B860
void srVertexPipe::setupEyeSpaceNormal()
{
    const srVector3T<float>* normals = input->normals;
    if (normals == 0) {
        srVector3T<float> constant;
        constant.Set(0.0f, 0.0f, -1.0f);
        std::fill_n(scratch.normals, batch_count, constant);
    } else if (input->direct_vertex_indices == 0) {
        {
            std::span<srVector3> input_destination{scratch.normals,
                                                   static_cast<std::size_t>(batch_count)};
            const srVector3* input_source = normals;
            std::span<const SRDWORD> input_indices{avt, static_cast<std::size_t>(batch_count)};
            const srMatrix4& input_matrix = *input->normal_matrix;
            const auto transform_matrix = input_matrix.glmValue();
            for (std::size_t i = 0; i < input_indices.size(); ++i)
                input_destination[i] = glm::vec3(
                    transform_matrix * glm::vec4(input_source[input_indices[i]].glmValue(), 1.0f));
        }
    } else {
        {
            std::span<srVector3> input_destination{scratch.normals,
                                                   static_cast<std::size_t>(batch_count)};
            std::span<const srVector3> input_vectors{normals + batch_base, batch_count};
            const srMatrix4& input_matrix = *input->normal_matrix;
            const auto transform_matrix = input_matrix.glmValue();
            for (std::size_t i = 0; i < input_destination.size(); ++i)
                input_destination[i] =
                    glm::vec3(transform_matrix * glm::vec4(input_vectors[i].glmValue(), 1.0f));
        }
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_NORMALS;
}

// FUNCTION: SURRENDER 0x1002B910
void srVertexPipe::setupEyeSpaceDirAndDist()
{
    {
        std::span<srVector3> input_destination{scratch.dir, batch_count};
        std::span<float> input_lengths{scratch.dist, batch_count};
        std::span<const srVector4> input_source{eye_space_locations + batch_base, batch_count};
        for (std::size_t i = 0; i < input_destination.size(); ++i) {
            const auto v = glm::vec3(input_source[i].glmValue());
            input_lengths[i] = glm::length(v);
            input_destination[i] = v / input_lengths[i];
        }
    }
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_DIRECTION;
    scratch.flags |= srVertexPipe::Scratch::READY_EYE_DISTANCE;
}

// FUNCTION: SURRENDER 0x1002B970
void srVertexPipe::setupEyeSpaceZDist()
{
    const srVector4T<float>* locations = eye_space_locations + batch_base;
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
        float one_minus[0x40];
        std::ranges::transform(std::span<const float>{values, vertex_count}, one_minus,
                               [&](const auto& v) { return (1.0f) - v; });
        {
            std::span<float> input_destination{fog, vertex_count};
            std::span<const float> input_add_source{values, vertex_count};
            std::span<const float> input_scale_source{fog, vertex_count};
            std::span<const float> input_multiply_source{one_minus, vertex_count};
            for (std::size_t i = 0; i < input_destination.size(); ++i)
                input_destination[i] =
                    input_scale_source[i] * input_multiply_source[i] + input_add_source[i];
        }
        return;
    }
    if ((vertex_count != 0) && (fog != values)) {
        std::copy_n(values, vertex_count, fog);
    }
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_FOG);
}

// FUNCTION: SURRENDER 0x1002BCC0
void srVertexPipe::applyDiffuseLight(const float* values, const srVector4T<float>& light)
{
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    srVector4T<float>* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
    if (vertex_count != 0) {
        {
            std::span<srVector4> input_destination{diffuse, vertex_count};
            std::span<const srVector4> input_add_source{diffuse, vertex_count};
            const srVector4& input_multiply_constant = light;
            std::span<const float> input_multiply_source{values, vertex_count};
            for (std::size_t i = 0; i < input_destination.size(); ++i)
                input_destination[i] =
                    input_multiply_constant.glmValue() * input_multiply_source[i] +
                    input_add_source[i].glmValue();
        }
    }
}

// FUNCTION: SURRENDER 0x1002BD30
void srVertexPipe::copySpecularToDiffuse()
{
    srCore.getStatisticsManager()->statistics.specular_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        setupSpecular();
    }
    w8_ulong offset = batch_base + sub_batch_offset;
    srVector4T<float>* specular = vertex_array->specular + offset;
    srVector4T<float>* diffuse = vertex_array->diffuse + offset;
    if (((vertex_count * 4) != 0) && (diffuse != specular)) {
        std::copy_n(specular, vertex_count, diffuse);
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_DIFFUSE);
        return;
    }
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_DIFFUSE);
}

// FUNCTION: SURRENDER 0x1002BDB0
void srVertexPipe::copyDiffuseToSpecular()
{
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    w8_ulong offset = batch_base + sub_batch_offset;
    srVector4T<float>* diffuse = vertex_array->diffuse + offset;
    srVector4T<float>* specular = vertex_array->specular + offset;
    if (((vertex_count * 4) != 0) && (specular != diffuse)) {
        std::copy_n(diffuse, vertex_count, specular);
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
        return;
    }
    lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
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
void srVertexPipe::applyDiffuseLight(const srVector4T<float>& light)
{
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    srVector4T<float>* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
    std::ranges::transform(std::span<const srVector4>{diffuse, vertex_count}, diffuse,
                           [&](const auto& v) { return light.glmValue() + v.glmValue(); });
}

// FUNCTION: SURRENDER 0x1002BF20
void srVertexPipe::Record::ColorSource::copyDiffuseColors(srVector4T<float>* destination,
                                                          const w8_ulong* indices,
                                                          w8_ulong count) const
{
    if (colors == 0) {
        srVector4T<float> zero;
        zero = 0.0f;
        std::fill_n(destination, count, zero);
        return;
    }
    if (format == FORMAT_ARGB) {
        std::ranges::transform(std::span<const SRDWORD>{indices, count}, destination, [&](auto i) {
            return glm::vec4((static_cast<const srARGB*>(colors))[i].red,
                             (static_cast<const srARGB*>(colors))[i].green,
                             (static_cast<const srARGB*>(colors))[i].blue,
                             (static_cast<const srARGB*>(colors))[i].alpha) *
                   (1.0f / 255.0f);
        });
        return;
    }
    if (format == FORMAT_VECTOR3) {
        std::ranges::transform(std::span<const SRDWORD>{indices, count}, destination, [&](auto i) {
            return glm::vec4((static_cast<const srVector3*>(colors))[i].glmValue(), 1.0f);
        });
        return;
    }
    if (format == FORMAT_VECTOR4) {
        std::ranges::transform(std::span<const SRDWORD>{indices, count}, destination,
                               [&](auto i) { return (static_cast<const srVector4*>(colors))[i]; });
    }
}

// FUNCTION: SURRENDER 0x1002BFB0
void srVertexPipe::setupDiffuse()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        srVector4T<float> color;
        color.x = input->ambient_light.x * material_info.ambient.x + material_info.emissive.x;
        color.y = input->ambient_light.y * material_info.ambient.y + material_info.emissive.y;
        color.z = input->ambient_light.z * material_info.ambient.z + material_info.emissive.z;
        color.w = input->ambient_light.w * material_info.ambient.w + material_info.emissive.w;
        srVector4T<float>* diffuse = vertex_array->diffuse + batch_base + sub_batch_offset;
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
                std::ranges::transform(
                    std::span<const srVector4>{diffuse, vertex_count}, diffuse,
                    [&](const auto& v) { return color.glmValue() + v.glmValue(); });
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
                std::ranges::transform(std::span<const float>{dist, count}, depth_cue,
                                       [&](const auto& v) { return -v; });
            } else {
                std::ranges::transform(std::span<const float>{dist, count}, depth_cue,
                                       [&](const auto& v) { return (maximum)-v; });
            }
        }
        float scale = 1.0f / (maximum - minimum);
        if (count != 0) {
            if (scale != 1.0f) {
                if (scale == 0.0f) {
                    std::fill_n(depth_cue, count, 0.0f);
                } else {
                    std::ranges::transform(std::span<const float>{depth_cue, count}, depth_cue,
                                           [&](const auto& v) { return (scale)*v; });
                }
            }
            std::ranges::transform(std::span<const float>{depth_cue, count}, depth_cue,
                                   [&](const auto& v) { return glm::clamp(v, 0.0f, 1.0f); });
        }
        float range = near_value - far_value;
        if (((range != 1.0f) && (count != 0)) && (range != 1.0f)) {
            if (range == 0.0f) {
                std::fill_n(depth_cue, count, 0.0f);
            } else {
                std::ranges::transform(std::span<const float>{depth_cue, count}, depth_cue,
                                       [&](const auto& v) { return (range)*v; });
            }
        }
        if (((near_value != 1.0f) && (count != 0)) && (1.0f - near_value != 0.0f)) {
            std::ranges::transform(std::span<const float>{depth_cue, count}, depth_cue,
                                   [&](const auto& v) { return (1.0f - near_value) + v; });
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
int srVertexPipe::testEyeSpaceBounds(const srVector3T<float>& center, float radius) const
{
    float dx = center.x - input->eye_center.x;
    float dy = center.y - input->eye_center.y;
    float dz = center.z - input->eye_center.z;
    float sum = radius + input->eye_radius;
    if (dx * dx + dy * dy + dz * dz < sum * sum) {
        return 1;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1002C3B0
void srVertexPipe::setupSpecular()
{
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_SPECULAR)) == 0) {
        srVector4T<float> zero;
        zero = 0.0f;
        std::fill_n(vertex_array->specular + batch_base + sub_batch_offset, vertex_count, zero);
        lazy_setup_mask |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
    }
}

// FUNCTION: SURRENDER 0x1002C400
void srVertexPipe::setupST(w8_ulong index)
{
    if (((current_record->flags & (srVertexPipe::Record::HAS_TEXCOORD0 << index)) != 0)) {
        const srVector2T<float>* source = current_record->st_source[index];
        if (source != 0) {
            std::ranges::transform(
                std::span<const SRDWORD>{avt + sub_batch_offset, vertex_count},
                (&vertex_array->st0)[index] + batch_base + sub_batch_offset,
                [&](auto i) { return (reinterpret_cast<const srVector2*>(source))[i]; });
        }
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
}

// FUNCTION: SURRENDER 0x1002C480
void srVertexPipe::getEyeSpaceBoundingSphere(srVector3T<float>& center, float& radius) const
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
const srVector4T<float>* srVertexPipe::getEyeSpaceLocation()
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
const srVector3T<float>* srVertexPipe::getEyeSpaceDir()
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
srVector4T<float>* srVertexPipe::getDiffuse()
{
    srCore.getStatisticsManager()->statistics.diffuse_operations += vertex_count;
    if ((lazy_setup_mask & (1UL << srVertexProcessor::CHANNEL_DIFFUSE)) == 0) {
        setupDiffuse();
    }
    return vertex_array->diffuse + batch_base + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C740
srVector4T<float>* srVertexPipe::getSpecular()
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

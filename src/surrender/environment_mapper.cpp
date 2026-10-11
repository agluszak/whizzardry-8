
#include "glm/geometric.hpp"
#include "surrender/srEnvironmentMapper.h"
#include "surrender/srCore.h"
#include "surrender/srVertexPipe.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srStatisticsManager.h"
#include "surrender/srVertexProcessor.h"

// FUNCTION: SURRENDER 0x100352e0
int srEnvironmentMapper::isActive(srVertexPipe&)
{
    return 1;
}

// FUNCTION: SURRENDER 0x100352f0
void srEnvironmentMapper::process(srVertexPipe& pipe)
{
    w8_ulong count = pipe.vertex_count;
    const auto directions = pipe.getEyeSpaceDir();
    const auto normals = pipe.getEyeSpaceNormal();
    glm::vec2* st = pipe.vertex_array->st0 + pipe.batch_base + pipe.sub_batch_offset;
    srCore.getStatisticsManager()->statistics.texture_coordinate_operations += count;
    pipe.lazy_setup_mask |= 1 << srVertexProcessor::CHANNEL_ST0;
    for (w8_ulong index = 0; index < count; ++index) {
        const glm::vec3& direction = directions[index];
        const glm::vec3& normal = normals[index];
        const glm::vec3 reflected = glm::reflect(direction, normal) + glm::vec3(0, 0, 1);
        st[index] = glm::vec2(reflected) / (2 * glm::length(reflected)) + 0.5f;
    }
}

// GLOBAL: SURRENDER 0x100A48CC
srEnvironmentMapper srEnvironmentMapper;

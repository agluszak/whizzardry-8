#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "wiz8/render/clip.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/mesh_math.h"
#include "wiz8/render/triangle_runs.h"
#include "wiz8/render/triangle_selection.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_stream.h"

/* Mark `bit` in output wherever the indexed table value changes between adjacent entries; when
   `initialized` is zero the output chunk is cleared first. */
template<class Lookup>
static void markTransitions(std::uint32_t* output, const std::uint32_t* indices, Lookup lookup,
                            std::uint32_t bit, std::uint32_t count, std::uint32_t initialized)
{
    if (initialized == 0) {
        std::fill_n(output, count, 0);
    }
    if (count == 0) {
        return;
    }
    auto previous = lookup(indices[0]);
    output[0] |= bit;
    for (std::uint32_t index = 1; index < count; ++index) {
        auto value = lookup(indices[index]);
        if (value != previous) {
            output[index] |= bit;
            previous = value;
        }
    }
}

/* dst[i] = src[i] + offset over `count` dwords. */
static void offsetIndices(std::uint32_t* destination, const std::uint32_t* source,
                          std::uint32_t offset, std::uint32_t count)
{
    std::transform(source, source + count, destination,
                   [offset](std::uint32_t index) { return index + offset; });
}

// Rebase in unsigned GLM arithmetic to retain dword wrap behavior.
static glm::ivec3 remappedTriangle(const glm::ivec3& triangle,
                                   std::span<const std::uint32_t> vertices,
                                   std::uint32_t vertex_base)
{
    const auto mapped = vertices.empty() ? triangle : meshMath::remapTriangle(triangle, vertices);
    const glm::uvec3 indices = glm::uvec3(mapped) + vertex_base;
    return glm::ivec3(indices);
}

// An empty order denotes consecutive source triangles; an empty vertex map denotes
// direct indices. Both gathers and pass replication use the same rebase kernel.
static void gatherTriangles(std::span<glm::ivec3> destination, std::span<const glm::ivec3> source,
                            std::span<const std::uint32_t> order, std::uint32_t vertex_base,
                            std::span<const std::uint32_t> vertices = {})
{
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const auto source_index = !order.empty() ? order[index] : index;
        destination[index] = remappedTriangle(source[source_index], vertices, vertex_base);
    }
}

void GpuRenderer::Renderer::resetStatistics()
{
    statistics = {};
}

GpuRenderer::Renderer::Statistics GpuRenderer::Renderer::getStatistics() const
{
    return statistics;
}

std::uint32_t GpuRenderer::Renderer::TextureSetCache::intern(const TextureSetKey& key)
{
    const auto found = map.find(key);
    if (found != map.end()) {
        return found->second;
    }

    const auto index = static_cast<std::uint32_t>(sets.size());
    TextureSet& set = sets.emplace_back();
    try {
        map.emplace(key, index);
    } catch (...) {
        sets.pop_back();
        throw;
    }
    set.texture0 = key.texture0;
    set.texture1 = key.texture1;
    set.shader = key.shader;
    set.polygon_offset = key.polygon_offset;
    set.fog_color = key.fog_color;
    set.texture_reduction = key.texture_reduction;
    switch ((key.shader.value >> DrawShader::DSTBLEND_SHIFT) & 7) {
    case DrawShader::DSTBLEND_ONE:
        set.blend = 2;
        break;
    case DrawShader::DSTBLEND_SRC_COLOR:
    case DrawShader::DSTBLEND_ONE_MINUS_SRC_COLOR:
        set.blend = 3;
        break;
    case DrawShader::DSTBLEND_SRC_ALPHA:
    case DrawShader::DSTBLEND_ONE_MINUS_SRC_ALPHA:
        set.blend = 1;
        break;
    default:
        set.blend = 0;
        break;
    }
    return index;
}

GpuRenderer::Renderer::IndexWrite GpuRenderer::Renderer::IndexBatch::alloc(std::uint32_t count)
{
    std::uint32_t needed = count + 0x40 + this->count;
    triangles.resize(needed);
    texture_set.resize(needed);
    sort_key.resize(needed);
    aux.resize(needed);
    const IndexWrite write{triangles.data() + this->count, texture_set.data() + this->count,
                           sort_key.data() + this->count, aux.data() + this->count};
    this->count += count;
    return write;
}

void GpuRenderer::Renderer::IndexBatch::reset(int release)
{
    if (release != 0) {
        decltype(triangles){}.swap(triangles);
        decltype(texture_set){}.swap(texture_set);
        decltype(sort_key){}.swap(sort_key);
        decltype(aux){}.swap(aux);
    }
    count = 0;
}

/* Vertex-stream teardown expanded in Renderer::reset. The method name is
   descriptive, following the companion IndexBatch operation. */
void GpuRenderer::Renderer::VertexArrays::reset(int release)
{
    if (release != 0) {
        decltype(diffuse){}.swap(diffuse);
        decltype(specular){}.swap(specular);
        decltype(positions){}.swap(positions);
        std::vector<glm::vec2>{}.swap(st[0]);
        std::vector<glm::vec2>{}.swap(st[1]);
        std::vector<float>{}.swap(q[0]);
        std::vector<float>{}.swap(q[1]);
        decltype(attributes){}.swap(attributes);
    }
    count = 0;
}

ShadedVertexArrays GpuRenderer::Renderer::VertexArrays::alloc(std::uint32_t count)
{
    std::uint32_t needed = count + this->count;
    if (positions.size() < needed) {
        needed += 0x40;
        // Retail constant-fill helper SURRENDER 0x10027BA0 is replaced by typed defaults.
        glm::vec4 zero;
        zero = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        glm::vec4 eye_default;
        eye_default = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        diffuse.resize(needed, zero);
        specular.resize(needed, zero);
        positions.resize(needed, eye_default);
        st[0].resize(needed, glm::vec2(0.0f, 0.0f));
        st[1].resize(needed, glm::vec2(0.0f, 0.0f));
        q[0].resize(needed, 1.0f);
        q[1].resize(needed, 1.0f);
        attributes.resize(needed, 0);
    }
    const auto arrays = bind(this->count);
    this->count += count;
    return arrays;
}

GpuRenderer::Renderer::Renderer(GpuRenderer* gerd, bool sorted) : gerd(gerd), sorted(sorted)
{
    first_vertex = -1;
    statistics = {};
}

bool GpuRenderer::Renderer::isBatchFull() const
{
    return !sorted && vertices.count > static_cast<std::int32_t>(renderer_batch_limit);
}

ShadedVertexArrays GpuRenderer::Renderer::allocVertexArray(std::uint32_t count)
{
    first_vertex = vertices.count;
    return vertices.alloc(count);
}

void GpuRenderer::Renderer::assignTextureSets(std::uint32_t* texture_set,
                                              const std::uint32_t* indices, std::uint32_t count,
                                              const MeshPass* pass, const TriInput& input)
{
    enum { TABLE_TEXTURE0 = 1u, TABLE_TEXTURE1 = 2u, TABLE_SHADER = 4u };
    TextureSetKey key;
    key.texture0 = pass->textures[0];
    key.texture1 = pass->textures[1];
    key.shader = pass->shader;
    key.polygon_offset = input.polygon_offset;
    key.fog_color = input.fog_color;
    key.texture_reduction = input.texture_reduction;
    unsigned char mask = pass->texture_tables[0] != 0 ? TABLE_TEXTURE0 : 0;
    if (pass->texture_tables[1] != 0) {
        mask |= TABLE_TEXTURE1;
    }
    if (pass->shaders != 0) {
        mask |= TABLE_SHADER;
    }
    if (mask == 0) {
        std::uint32_t set = texture_sets.intern(key);
        if (count != 0) {
            std::fill_n(texture_set, count, set);
        }
        return;
    }
    const auto texture0 = [&](std::uint32_t polygon) {
        return (*pass->texture_tables[0])[polygon].get();
    };
    const auto texture1 = [&](std::uint32_t polygon) {
        return (*pass->texture_tables[1])[polygon].get();
    };
    const auto shader = [&](std::uint32_t polygon) { return pass->shaders[polygon].value; };
    std::uint32_t set = texture_sets.intern(key);
    std::uint32_t done = 0;
    while (done < count) {
        std::uint32_t chunk = count - done;
        if (chunk > 0x100) {
            chunk = 0x100;
        }
        const std::uint32_t* chunk_indices = indices + done;
        std::uint32_t* out = texture_set + done;
        std::uint32_t initialized = 0;
        if ((mask & TABLE_TEXTURE0) != 0) {
            markTransitions(out, chunk_indices, texture0, TABLE_TEXTURE0, chunk, initialized);
            initialized = 1;
        }
        if ((mask & TABLE_TEXTURE1) != 0) {
            markTransitions(out, chunk_indices, texture1, TABLE_TEXTURE1, chunk, initialized);
            initialized += 1;
        }
        if ((mask & TABLE_SHADER) != 0) {
            markTransitions(out, chunk_indices, shader, TABLE_SHADER, chunk, initialized);
        }
        if (chunk != 0) {
            for (std::uint32_t index = 0; index < chunk; index++) {
                std::uint32_t changed = out[index];
                if (changed != 0) {
                    std::uint32_t vertex = chunk_indices[index];
                    if ((changed & TABLE_TEXTURE0) != 0) {
                        key.texture0 = texture0(vertex);
                    }
                    if ((changed & TABLE_TEXTURE1) != 0) {
                        key.texture1 = texture1(vertex);
                    }
                    if ((changed & TABLE_SHADER) != 0) {
                        key.shader = pass->shaders[vertex];
                    }
                    set = texture_sets.intern(key);
                }
                out[index] = set;
            }
        }
        done += 0x100;
    }
}

/* Depth-sort keys: each triangle's key is the negated sum of its corner view-space z's plus the
   bias, mapped to a sortable unsigned value. */
static void sortKeys(std::span<std::uint32_t> keys, std::span<const glm::ivec3> triangles,
                     std::span<const glm::vec4> positions, float bias)
{
    assert(keys.size() == triangles.size());
    const float scaled_bias = bias * 3.0f;
    std::ranges::transform(triangles, keys.begin(), [&](const glm::ivec3& triangle) {
        const float depth = -(positions[triangle.x].z + positions[triangle.y].z +
                              positions[triangle.z].z + scaled_bias);
        const auto bits = std::bit_cast<std::uint32_t>(depth);
        return (bits & 0x80000000u) == 0 ? bits | 0x80000000u : 0x7fffffffu - (bits & 0x7fffffffu);
    });
}

void GpuRenderer::Renderer::expandTriangles(const TriInput& input, int sorted)
{
    const auto write = indices.alloc(input.triangleCount() * input.record_count);
    const MeshPass* passes = input.passes;
    std::uint32_t record = 0;
    while (record < input.record_count && passes[record].poly_uv == 0) {
        record++;
    }
    if (record == input.record_count) {
        /* Flat path: gather the chunk once, then replicate it per record
       with the record's vertex offset. */
        for (std::uint32_t done = 0; done < input.triangleCount(); done += 0x100) {
            std::uint32_t chunk = input.triangleCount() - done;
            if (chunk > 0x100) {
                chunk = 0x100;
            }
            gatherTriangles({write.triangles + done, chunk}, input.triangles,
                            input.indices.subspan(done, chunk), first_vertex,
                            input.direct_vertex_indices ? std::span<const std::uint32_t>{}
                                                        : input.vertices);
            if (sorted != 0) {
                sortKeys({write.sort_key + done, chunk}, {write.triangles + done, chunk},
                         vertices.positions, input.sort_bias);
                for (std::uint32_t replica = 1; replica < input.record_count; replica++) {
                    offsetIndices(write.sort_key + input.triangleCount() * replica + done,
                                  write.sort_key + done, replica, chunk);
                }
            }
            for (std::uint32_t replica = 1; replica < input.record_count; replica++) {
                gatherTriangles({write.triangles + input.triangleCount() * replica + done, chunk},
                                std::span<const glm::ivec3>(write.triangles + done, chunk), {},
                                replica * input.vertex_count);
            }
        }
    } else {
        /* Dedup path: any pass with a poly-UV corner table reuses the
           designated source corner's vertex and allocates fresh slots for
           the others; remap carries six dwords per triangle — the new
           vertices' batch positions, then their corner-source indices. */
        std::uint32_t free_vertex = input.record_count * input.vertex_count + first_vertex;
        this->remap.resize(input.triangleCount() * 6);
        std::uint32_t* remap = this->remap.data();
        std::uint32_t* corner_remap = remap + input.triangleCount() * 3;
        std::uint32_t written = 0;
        for (record = 0; record < input.record_count; record++) {
            const MeshPass& pass = passes[record];
            std::uint32_t vertex_base = record * input.vertex_count + first_vertex;
            if (pass.poly_uv != 0) {
                const glm::ivec3* poly_uv = pass.poly_uv;
                std::uint32_t new_count = 0;
                std::uint32_t next = free_vertex;
                std::uint32_t base = free_vertex;
                std::uint32_t* remap_out = remap;
                std::uint32_t* corner_out = corner_remap;
                for (std::uint32_t index = 0; index < input.triangleCount(); index++) {
                    const glm::ivec3& triangle = input.triangles[input.indices[index]];
                    const glm::ivec3& uv = poly_uv[input.indices[index]];
                    glm::ivec3& out = write.triangles[written + index];
                    for (int corner = 0; corner < 3; ++corner) {
                        const auto vertex = triangle[corner];
                        const auto texcoord = uv[corner];
                        const std::uint32_t mapped = input.vertices[vertex] + vertex_base;
                        if (vertex == texcoord) {
                            out[corner] = mapped;
                        } else {
                            *remap_out++ = mapped;
                            *corner_out++ = texcoord;
                            out[corner] = next++;
                            ++new_count;
                        }
                    }
                }
                if (new_count != 0) {
                    vertices.alloc(new_count);
                    const auto arrays = vertices.bind(0);
                    const auto gather_stream = [remap, base, new_count](auto* stream) {
                        std::transform(remap, remap + new_count, stream + base,
                                       [stream](std::uint32_t index) { return stream[index]; });
                    };
                    gather_stream(arrays.eye_locations);
                    gather_stream(arrays.diffuse);
                    gather_stream(arrays.specular);
                    gather_stream(arrays.st1);
                    gather_stream(arrays.q1);
                    gather_stream(arrays.attributes);
                    for (std::size_t i = 0; i < new_count; ++i)
                        arrays.st0[base + i] = pass.texcoords[corner_remap[i]];
                    std::fill_n(arrays.q0 + base, new_count, 1.0f);
                }
                free_vertex = base + new_count;
            } else {
                gatherTriangles({write.triangles + written, input.triangleCount()}, input.triangles,
                                input.indices, vertex_base, input.vertices);
            }
            if (sorted != 0)
                sortKeys({write.sort_key + written, input.triangleCount()},
                         {write.triangles + written, input.triangleCount()}, vertices.positions,
                         input.sort_bias);
            written += input.triangleCount();
        }
    }
    std::uint32_t texture_set_offset = 0;
    for (record = 0; record < input.record_count; record++) {
        assignTextureSets(write.texture_set + texture_set_offset, input.indices.data(),
                          input.triangleCount(), passes + record, input);
        texture_set_offset += input.triangleCount();
    }
}

void GpuRenderer::Renderer::transformVertices(const TriInput& input, unsigned char* clip_flags)
{
    if (input.vertex_count == 0) return;
    auto* positions = vertices.positions.data() + first_vertex;
    const auto& projection = *input.project_clip_near;
    for (std::uint32_t i = 0; i < input.vertex_count; ++i) positions[i] = projection * positions[i];
    clipMath::clipFlags({clip_flags, input.vertex_count}, {positions, input.vertex_count});
    for (std::uint32_t replica = 1; replica < input.record_count; ++replica)
        std::copy_n(positions, input.vertex_count, positions + replica * input.vertex_count);
}

/* Whether all clip-flag bytes share a clip plane. */
static int fullyClipped(const unsigned char* flags, std::uint32_t count)
{
    unsigned char mask = vertexStream::FRUSTUM_CLIP_MASK;
    std::uint32_t index;
    for (index = 0; index < (count & ~3UL); index += 4) {
        mask &= flags[index] & flags[index + 1] & flags[index + 2] & flags[index + 3];
        if (mask == 0) {
            return 0;
        }
    }
    for (; index < count; index++) {
        mask &= flags[index];
    }
    return mask != 0;
}

/* OR the per-vertex packed attribute bytes into the aggregate mask. */
static std::uint32_t attributeMask(const unsigned char* packed, std::uint32_t count)
{
    std::uint32_t mask = 0;
    for (std::uint32_t i = 0; i < count; ++i) mask |= packed[i];
    return mask;
}

void GpuRenderer::Renderer::drawTriangleRuns(std::span<const std::uint32_t> order,
                                             const vertexStream::VertexArrayInfo& arrays)
{
    const auto count = indices.count;
    if (count == 0) return;
    const auto triangles = std::span{indices.triangles}.first(count);
    const auto texture_sets = std::span{indices.texture_set}.first(count);
    std::array<glm::ivec3, 512> gathered;
    visitTriangleRuns(
        texture_sets, order, order.empty() ? count : gathered.size(), [&](const TriangleRun& run) {
            const TextureSet& set = this->texture_sets.sets[run.texture_set];
            const glm::ivec3* submitted = triangles.data() + run.first;
            if (!order.empty()) {
                gatherTriangles(std::span{gathered}.first(run.count), triangles,
                                order.subspan(run.first, run.count), 0);
                submitted = gathered.data();
            }
            // Positions are already in clip space, so the device applies no transform.
            gerd->draw(
                {.primitive = vertexStream::PRIMITIVE_TRIANGLES,
                 .vertices = arrays,
                 /* reinterpret-ok: each triangle is three packed 32-bit indices. */
                 .indices = {reinterpret_cast<const std::uint32_t*>(submitted), run.count * 3},
                 .shader = set.shader,
                 .textures = {set.texture0, set.texture1},
                 .fog_color = set.fog_color,
                 .polygon_offset = set.polygon_offset,
                 .texture_reduction = set.texture_reduction});
        });
}

void GpuRenderer::Renderer::drawImmediate(const vertexStream::VertexArrayInfo& arrays)
{
    drawTriangleRuns({}, arrays);
}

void GpuRenderer::Renderer::drawSorted(const vertexStream::VertexArrayInfo& arrays)
{
    const auto count = indices.count;
    if (count == 0) return;
    const auto order = sortedTriangleOrder(std::span{indices.sort_key}.first(count));
    drawTriangleRuns(order, arrays);
}

/* The batch's device streams: positions, diffuse, specular (+ its w alone
   when only the alpha attribute is present) and per-stage texture
   coordinate and Q streams. */
vertexStream::VertexArrayInfo GpuRenderer::Renderer::vertexArrays(const ShadedVertexArrays& arrays,
                                                                  std::uint32_t count) const
{
    const auto attributes = attributeMask(arrays.attributes, count);
    vertexStream::VertexArrayInfo result{};
    const auto stream = [&](vertexStream::e_vertexArray slot, std::int32_t components,
                            std::uint32_t stride, const void* values) {
        result.mask |= 1u << slot;
        result.components[slot] = components;
        result.strides[slot] = stride;
        result.arrays[slot] = values;
    };
    result.count = count;
    stream(vertexStream::VERTEX_ARRAY_POSITIONS, 4, 0x10, arrays.eye_locations);
    if ((attributes & ShadedVertexArrays::ATTRIBUTE_DIFFUSE) != 0)
        stream(vertexStream::VERTEX_ARRAY_DIFFUSE, 4, 0x10, arrays.diffuse);
    if ((attributes & ShadedVertexArrays::ATTRIBUTE_SPECULAR) != 0) {
        stream(vertexStream::VERTEX_ARRAY_SPECULAR,
               ((attributes & ShadedVertexArrays::ATTRIBUTE_SPECULAR_ALPHA) != 0) + 3, 0x10,
               arrays.specular);
    } else if ((attributes & ShadedVertexArrays::ATTRIBUTE_SPECULAR_ALPHA) != 0) {
        stream(vertexStream::VERTEX_ARRAY_SPECULAR_ALPHA, 1, 0x10, &arrays.specular->w);
    }
    const std::array coordinates{arrays.st0, arrays.st1};
    const std::array weights{arrays.q0, arrays.q1};
    for (unsigned stage = 0; stage < coordinates.size(); ++stage) {
        const std::uint32_t st_bit = ShadedVertexArrays::ATTRIBUTE_ST0 << stage;
        const std::uint32_t q_bit = ShadedVertexArrays::ATTRIBUTE_Q0 << stage;
        if ((attributes & st_bit) == 0) continue;
        stream(
            static_cast<vertexStream::e_vertexArray>(vertexStream::VERTEX_ARRAY_TEXCOORD0 + stage),
            2, sizeof(glm::vec2), coordinates[stage]);
        result.texture_weights[stage] = (attributes & q_bit) != 0 ? weights[stage] : nullptr;
    }
    return result;
}

void GpuRenderer::Renderer::submit()
{
    std::uint32_t vertex_count = vertices.count;
    statistics.device_calls += 1;
    statistics.device_vertices += vertex_count;
    statistics.device_vertex_indices += indices.count;
    if (gerd != 0 && vertex_count != 0 && indices.count != 0) {
        const auto arrays = vertexArrays(vertices.bind(0), vertex_count);
        if (sorted == 0) {
            drawImmediate(arrays);
        } else {
            drawSorted(arrays);
        }
    }
    indices.reset(0);
    vertices.count = 0;
}

void GpuRenderer::Renderer::reset(int release_buffers)
{
    indices.reset(release_buffers);
    vertices.reset(release_buffers);
    texture_sets.clear();
    if (release_buffers != 0) {
        decltype(bytes){}.swap(bytes);
        decltype(dwords){}.swap(dwords);
        decltype(remap){}.swap(remap);
    }
}

/* Copy the indices of the not-fully-clipped triangles; a triangle drops out only when all three
   corners share a clip flag. With direct_vertex_indices clear the corners index the flag array
   through the vertices remap. */
static std::uint32_t filterTriangles(std::uint32_t* destination, const unsigned char* flags,
                                     const GpuRenderer::Renderer::TriInput& input)
{
    const auto* remap = input.direct_vertex_indices ? nullptr : input.vertices.data();
    return clipMath::selectTriangles(
        {destination, input.triangleCount()}, input.indices, [&](std::uint32_t index) {
            return clipMath::triangleVisible(input.triangles[index], flags, remap);
        });
}

void GpuRenderer::Renderer::render(const TriInput& input)
{
    if (first_vertex == -1) {
        return;
    }
    if (input.triangleCount() == 0) {
        vertices.count += first_vertex - vertices.count;
        first_vertex = -1;
        return;
    }
    statistics.triangle_chunks += 1;
    statistics.input_triangles += input.triangleCount() * input.record_count;
    statistics.input_vertices += input.vertex_count * input.record_count;
    bytes.resize(input.vertex_count);
    unsigned char* flags = bytes.data();
    transformVertices(input, flags);
    if (fullyClipped(flags, input.vertex_count) != 0) {
        vertices.count += first_vertex - vertices.count;
        first_vertex = -1;
        return;
    }
    const TriInput* batch = &input;
    TriInput filtered;
    std::uint32_t mask = attributeMask(flags, input.vertex_count);
    if (mask != 0) {
        filtered = input;
        dwords.resize(input.triangleCount());
        filtered.indices = std::span{dwords}.first(filterTriangles(dwords.data(), flags, input));
        if (!filtered.indices.empty()) {
            batch = &filtered;
        } else {
            vertices.count += first_vertex - vertices.count;
            first_vertex = -1;
            return;
        }
    }
    if (!batch->picks.empty()) {
        PickInput pick;
        pick.indices = batch->indices;
        pick.triangles = batch->triangles;
        pick.vertices =
            batch->direct_vertex_indices ? std::span<const std::uint32_t>{} : batch->vertices;
        pick.positions = std::span{vertices.positions}.subspan(first_vertex, batch->vertex_count);
        pick.picks = batch->picks;
        pick.owner = batch->pick_owner;
        gerd->performPickTest(pick);
    }
    expandTriangles(*batch, sorted != 0);
    first_vertex = -1;
}

ShadedVertexArrays GpuRenderer::Renderer::VertexArrays::bind(std::uint32_t base)
{
    return {positions.data() + base, diffuse.data() + base,   specular.data() + base,
            st[0].data() + base,     st[1].data() + base,     q[0].data() + base,
            q[1].data() + base,      attributes.data() + base};
}

void clipMath::clipFlags(std::span<std::uint8_t> destination, std::span<const glm::vec4> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        const glm::vec4& point = source[index];
        const float negative_w = -point.w;
        // Each axis owns two bits: the low side (below -w) and the high side (above w).
        // x uses 0x01/0x02, y 0x04/0x08, z 0x10/0x20.
        std::uint8_t flags = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const auto low = static_cast<std::uint8_t>(1u << (2 * axis));
            if (negative_w > point[axis]) flags |= low;
            if (point[axis] > point.w) flags |= static_cast<std::uint8_t>(low << 1);
        }
        destination[index] = flags;
    }
}

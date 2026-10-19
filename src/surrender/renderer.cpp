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

#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "surrender/mesh_math.h"
#include "surrender/srClip.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVertexProcessor.h"
#include "surrender/triangle_runs.h"
#include "surrender/triangle_selection.h"

/* Mark `bit` in output wherever the indexed table value changes between adjacent entries; when
   `initialized` is zero the output chunk is cleared first. */
template<class Lookup>
static void markTransitions(w8_ulong* output, const w8_ulong* indices, Lookup lookup, w8_ulong bit,
                            w8_ulong count, w8_ulong initialized)
{
    if (initialized == 0) {
        std::fill_n(output, count, 0);
    }
    if (count == 0) {
        return;
    }
    auto previous = lookup(indices[0]);
    output[0] |= bit;
    for (w8_ulong index = 1; index < count; ++index) {
        auto value = lookup(indices[index]);
        if (value != previous) {
            output[index] |= bit;
            previous = value;
        }
    }
}

/* dst[i] = src[i] + offset over `count` dwords. */
static void offsetIndices(w8_ulong* destination, const w8_ulong* source, w8_ulong offset,
                          w8_ulong count)
{
    std::transform(source, source + count, destination,
                   [offset](w8_ulong index) { return index + offset; });
}

// Rebase in unsigned GLM arithmetic to retain dword wrap behavior.
static glm::ivec3 remappedTriangle(const glm::ivec3& triangle, std::span<const w8_ulong> vertices,
                                   w8_ulong vertex_base)
{
    const auto mapped = vertices.empty() ? triangle : srMeshMath::remapTriangle(triangle, vertices);
    const glm::uvec3 indices = glm::uvec3(mapped) + vertex_base;
    return glm::ivec3(indices);
}

// An empty order denotes consecutive source triangles; an empty vertex map denotes
// direct indices. Both gathers and pass replication use the same rebase kernel.
static void gatherTriangles(std::span<glm::ivec3> destination, std::span<const glm::ivec3> source,
                            std::span<const w8_ulong> order, w8_ulong vertex_base,
                            std::span<const w8_ulong> vertices = {})
{
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const auto source_index = !order.empty() ? order[index] : index;
        destination[index] = remappedTriangle(source[source_index], vertices, vertex_base);
    }
}

void srGERD::Renderer::resetStatistics()
{
    statistics = {};
}

srGERD::Renderer::Statistics srGERD::Renderer::getStatistics() const
{
    return statistics;
}

w8_ulong srGERD::Renderer::TextureSetCache::intern(const TextureSetKey& key)
{
    const auto found = map.find(key);
    if (found != map.end()) {
        return found->second;
    }

    const auto index = static_cast<w8_ulong>(sets.size());
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
    switch ((key.shader.value >> srShader::DSTBLEND_SHIFT) & 7) {
    case srShader::DSTBLEND_ONE:
        set.blend = 2;
        break;
    case srShader::DSTBLEND_SRC_COLOR:
    case srShader::DSTBLEND_ONE_MINUS_SRC_COLOR:
        set.blend = 3;
        break;
    case srShader::DSTBLEND_SRC_ALPHA:
    case srShader::DSTBLEND_ONE_MINUS_SRC_ALPHA:
        set.blend = 1;
        break;
    default:
        set.blend = 0;
        break;
    }
    return index;
}

srGERD::Renderer::IndexWrite srGERD::Renderer::IndexBatch::alloc(w8_ulong count)
{
    w8_ulong needed = count + 0x40 + this->count;
    triangles.resize(needed);
    texture_set.resize(needed);
    sort_key.resize(needed);
    aux.resize(needed);
    const IndexWrite write{triangles.data() + this->count, texture_set.data() + this->count,
                           sort_key.data() + this->count, aux.data() + this->count};
    this->count += count;
    return write;
}

void srGERD::Renderer::IndexBatch::reset(int release)
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
void srGERD::Renderer::VertexArrays::reset(int release)
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

srVertexArray srGERD::Renderer::VertexArrays::alloc(w8_ulong count)
{
    w8_ulong needed = count + this->count;
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

srGERD::Renderer::Renderer(const Parameters& parameters)
    : gerd(parameters.gerd), sorted(parameters.sorted), batch_limit(parameters.batch_limit),
      texture_stages(parameters.texture_stages)
{
    clip_state = 0;
    first_vertex = -1;
    statistics = {};
    texture0 = 0;
    texture1 = 0;
    shader = srShader();
}

int srGERD::Renderer::isBatchFull() const
{
    if (sorted == 0 && batch_limit < vertices.count) {
        return 1;
    }
    return 0;
}

srVertexArray srGERD::Renderer::allocVertexArray(w8_ulong count)
{
    first_vertex = vertices.count;
    return vertices.alloc(count);
}

void srGERD::Renderer::assignTextureSets(w8_ulong* texture_set, const w8_ulong* indices,
                                         w8_ulong count, const srTriMeshPipeline::Pass* pass)
{
    enum { TABLE_TEXTURE0 = 1u, TABLE_TEXTURE1 = 2u, TABLE_SHADER = 4u };
    TextureSetKey key;
    key.texture0 = pass->textures[0];
    key.texture1 = pass->textures[1];
    key.shader = pass->shader;
    unsigned char mask = pass->texture_tables[0] != 0 ? TABLE_TEXTURE0 : 0;
    if (pass->texture_tables[1] != 0) {
        mask |= TABLE_TEXTURE1;
    }
    if (pass->shaders != 0) {
        mask |= TABLE_SHADER;
    }
    if (mask == 0) {
        w8_ulong set = texture_sets.intern(key);
        if (count != 0) {
            std::fill_n(texture_set, count, set);
        }
        return;
    }
    const auto texture0 = [&](w8_ulong polygon) {
        return (*pass->texture_tables[0])[polygon].get();
    };
    const auto texture1 = [&](w8_ulong polygon) {
        return (*pass->texture_tables[1])[polygon].get();
    };
    const auto shader = [&](w8_ulong polygon) { return pass->shaders[polygon].value; };
    w8_ulong set = texture_sets.intern(key);
    w8_ulong done = 0;
    while (done < count) {
        w8_ulong chunk = count - done;
        if (chunk > 0x100) {
            chunk = 0x100;
        }
        const w8_ulong* chunk_indices = indices + done;
        w8_ulong* out = texture_set + done;
        w8_ulong initialized = 0;
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
            for (w8_ulong index = 0; index < chunk; index++) {
                w8_ulong changed = out[index];
                if (changed != 0) {
                    w8_ulong vertex = chunk_indices[index];
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
static void sortKeys(std::span<w8_ulong> keys, std::span<const glm::ivec3> triangles,
                     std::span<const glm::vec4> positions, float bias)
{
    assert(keys.size() == triangles.size());
    const float scaled_bias = bias * 3.0f;
    std::ranges::transform(triangles, keys.begin(), [&](const glm::ivec3& triangle) {
        const float depth = -(positions[triangle.x].z + positions[triangle.y].z +
                              positions[triangle.z].z + scaled_bias);
        const auto bits = std::bit_cast<w8_ulong>(depth);
        return (bits & 0x80000000u) == 0 ? bits | 0x80000000u : 0x7fffffffu - (bits & 0x7fffffffu);
    });
}

void srGERD::Renderer::expandTriangles(const TriInput& input, int sorted)
{
    const auto write = indices.alloc(input.triangleCount() * input.record_count);
    const srTriMeshPipeline::Pass* passes = input.passes;
    w8_ulong record = 0;
    while (record < input.record_count && passes[record].poly_uv == 0) {
        record++;
    }
    if (record == input.record_count) {
        /* Flat path: gather the chunk once, then replicate it per record
       with the record's vertex offset. */
        for (w8_ulong done = 0; done < input.triangleCount(); done += 0x100) {
            w8_ulong chunk = input.triangleCount() - done;
            if (chunk > 0x100) {
                chunk = 0x100;
            }
            gatherTriangles({write.triangles + done, chunk}, input.triangles,
                            input.indices.subspan(done, chunk), first_vertex,
                            input.direct_vertex_indices ? std::span<const w8_ulong>{}
                                                        : input.vertices);
            if (sorted != 0) {
                sortKeys({write.sort_key + done, chunk}, {write.triangles + done, chunk},
                         vertices.positions, input.sort_bias);
                for (w8_ulong replica = 1; replica < input.record_count; replica++) {
                    offsetIndices(write.sort_key + input.triangleCount() * replica + done,
                                  write.sort_key + done, replica, chunk);
                }
            }
            for (w8_ulong replica = 1; replica < input.record_count; replica++) {
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
        w8_ulong free_vertex = input.record_count * input.vertex_count + first_vertex;
        this->remap.resize(input.triangleCount() * 6);
        w8_ulong* remap = this->remap.data();
        w8_ulong* corner_remap = remap + input.triangleCount() * 3;
        w8_ulong written = 0;
        for (record = 0; record < input.record_count; record++) {
            const srTriMeshPipeline::Pass& pass = passes[record];
            w8_ulong vertex_base = record * input.vertex_count + first_vertex;
            if (pass.poly_uv != 0) {
                const glm::ivec3* poly_uv = pass.poly_uv;
                w8_ulong new_count = 0;
                w8_ulong next = free_vertex;
                w8_ulong base = free_vertex;
                w8_ulong* remap_out = remap;
                w8_ulong* corner_out = corner_remap;
                for (w8_ulong index = 0; index < input.triangleCount(); index++) {
                    const glm::ivec3& triangle = input.triangles[input.indices[index]];
                    const glm::ivec3& uv = poly_uv[input.indices[index]];
                    glm::ivec3& out = write.triangles[written + index];
                    for (int corner = 0; corner < 3; ++corner) {
                        const auto vertex = triangle[corner];
                        const auto texcoord = uv[corner];
                        const w8_ulong mapped = input.vertices[vertex] + vertex_base;
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
                                       [stream](w8_ulong index) { return stream[index]; });
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
    w8_ulong texture_set_offset = 0;
    for (record = 0; record < input.record_count; record++) {
        assignTextureSets(write.texture_set + texture_set_offset, input.indices.data(),
                          input.triangleCount(), passes + record);
        texture_set_offset += input.triangleCount();
    }
}

void srGERD::Renderer::transformVertices(const TriInput& input, unsigned char* clip_flags)
{
    if (input.vertex_count == 0) return;
    auto* positions = vertices.positions.data() + first_vertex;
    const auto& projection = *input.project_clip_near;
    for (w8_ulong i = 0; i < input.vertex_count; ++i) positions[i] = projection * positions[i];
    srRender::clipFlags({clip_flags, input.vertex_count}, {positions, input.vertex_count});
    for (w8_ulong replica = 1; replica < input.record_count; ++replica)
        std::copy_n(positions, input.vertex_count, positions + replica * input.vertex_count);
}

/* Whether all clip-flag bytes share a clip plane. */
static int fullyClipped(const unsigned char* flags, w8_ulong count)
{
    unsigned char mask = srRendererDefs::FRUSTUM_CLIP_MASK;
    w8_ulong index;
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
static w8_ulong attributeMask(const unsigned char* packed, w8_ulong count)
{
    w8_ulong mask = 0;
    for (w8_ulong i = 0; i < count; ++i) mask |= packed[i];
    return mask;
}

void srGERD::Renderer::drawTriangleRuns(std::span<const w8_ulong> order)
{
    const auto count = indices.count;
    if (count == 0) return;
    const auto triangles = std::span{indices.triangles}.first(count);
    const auto texture_sets = std::span{indices.texture_set}.first(count);
    std::array<glm::ivec3, 512> gathered;
    visitTriangleRuns(texture_sets, order, order.empty() ? count : gathered.size(),
                      [&](const srTriangleRun& run) {
                          // Refresh the first set even if another draw changed GERD's state.
                          bindTextureSet(run.texture_set, run.first == 0);
                          const glm::ivec3* submitted = triangles.data() + run.first;
                          if (!order.empty()) {
                              gatherTriangles(std::span{gathered}.first(run.count), triangles,
                                              order.subspan(run.first, run.count), 0);
                              submitted = gathered.data();
                          }
                          gerd->drawElements(srRendererDefs::PRIMITIVE_TRIANGLES,
                                             static_cast<w8_ulong>(run.count * 3),
                                             srRendererDefs::INDEX_ULONG, submitted);
                      });
}

void srGERD::Renderer::drawImmediate()
{
    drawTriangleRuns({});
}

void srGERD::Renderer::drawSorted()
{
    const auto count = indices.count;
    if (count == 0) return;
    const auto order = sortedTriangleOrder(std::span{indices.sort_key}.first(count));
    drawTriangleRuns(order);
}

/* Program the GERD vertex-array slots from the bound batch: diffuse,
   specular (+ its w alone when only the alpha attribute is present) and a
   per-stage texture coordinate and Q streams. */
void srGERD::Renderer::programVertexArrays(srVertexArray* arrays, w8_ulong count)
{
    w8_ulong attributes = attributeMask(arrays->attributes, count);
    if (texture_stages < 2) {
        /* Single-stage devices cannot take stage-1 st/q streams. */
        attributes &= ~(srVertexArray::ATTRIBUTE_ST1 | srVertexArray::ATTRIBUTE_Q1);
    }
    std::uint32_t mask = 1u << srRendererDefs::VERTEX_ARRAY_POSITIONS;
    gerd->setClipState(clip_state);
    gerd->setVertexPointer(4, srRendererDefs::TYPE_FLOAT, 0x10, arrays->eye_locations,
                           static_cast<w8_long>(count));
    if ((attributes & srVertexArray::ATTRIBUTE_DIFFUSE) != 0) {
        gerd->setDataPtr(srRendererDefs::VERTEX_ARRAY_DIFFUSE, 4, srRendererDefs::TYPE_FLOAT, 0x10,
                         arrays->diffuse);
        mask |= 1u << srRendererDefs::VERTEX_ARRAY_DIFFUSE;
    }
    if ((attributes & srVertexArray::ATTRIBUTE_SPECULAR) != 0) {
        mask |= 1u << srRendererDefs::VERTEX_ARRAY_SPECULAR;
        gerd->setDataPtr(srRendererDefs::VERTEX_ARRAY_SPECULAR,
                         ((attributes & srVertexArray::ATTRIBUTE_SPECULAR_ALPHA) != 0) + 3,
                         srRendererDefs::TYPE_FLOAT, 0x10, arrays->specular);
    } else if ((attributes & srVertexArray::ATTRIBUTE_SPECULAR_ALPHA) != 0) {
        mask |= 1u << srRendererDefs::VERTEX_ARRAY_SPECULAR_ALPHA;
        gerd->setDataPtr(srRendererDefs::VERTEX_ARRAY_SPECULAR_ALPHA, 1, srRendererDefs::TYPE_FLOAT,
                         0x10, &arrays->specular->w);
    }
    const std::array coordinates{arrays->st0, arrays->st1};
    const std::array weights{arrays->q0, arrays->q1};
    for (unsigned stage = 0; stage < coordinates.size(); ++stage) {
        const w8_ulong st_bit = srVertexArray::ATTRIBUTE_ST0 << stage;
        const w8_ulong q_bit = srVertexArray::ATTRIBUTE_Q0 << stage;
        if ((attributes & st_bit) == 0) continue;
        const auto array = static_cast<srRendererDefs::e_vertexArray>(
            srRendererDefs::VERTEX_ARRAY_TEXCOORD0 + stage);
        mask |= 1u << array;
        gerd->setDataPtr(array, 2, srRendererDefs::TYPE_FLOAT, sizeof(glm::vec2),
                         coordinates[stage]);
        gerd->vertex_arrays.texture_weights[stage] =
            (attributes & q_bit) != 0 ? weights[stage] : nullptr;
    }
    gerd->setVertexArrayMask(mask);
}

void srGERD::Renderer::submit()
{
    w8_ulong vertex_count = vertices.count;
    statistics.device_calls += 1;
    statistics.device_vertices += vertex_count;
    statistics.device_vertex_indices += indices.count;
    if (gerd != 0 && vertex_count != 0 && indices.count != 0) {
        e_matrixMode saved_mode = gerd->getMatrixMode();
        gerd->matrixMode(MATRIX_PROJECTION);
        gerd->pushMatrix();
        gerd->loadIdentity();
        auto arrays = vertices.bind(0);
        programVertexArrays(&arrays, vertex_count);
        e_cullMode saved_cull = gerd->getCullMode();
        gerd->setCullMode(CULL_NONE);
        if (sorted == 0) {
            drawImmediate();
        } else {
            drawSorted();
        }
        gerd->popMatrix();
        gerd->matrixMode(saved_mode);
        gerd->setCullMode(saved_cull);
    }
    indices.reset(0);
    vertices.count = 0;
    clip_state = 0;
}

void srGERD::Renderer::reset(int release_buffers)
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
static w8_ulong filterTriangles(w8_ulong* destination, const unsigned char* flags,
                                const srGERD::Renderer::TriInput& input)
{
    const auto* remap = input.direct_vertex_indices ? nullptr : input.vertices.data();
    return srRender::selectTriangles(
        {destination, input.triangleCount()}, input.indices, [&](w8_ulong index) {
            return srRender::triangleVisible(input.triangles[index], flags, remap);
        });
}

void srGERD::Renderer::render(const TriInput& input)
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
    w8_ulong mask = attributeMask(flags, input.vertex_count);
    if (mask != 0) {
        filtered = input;
        dwords.resize(input.triangleCount());
        filtered.indices = std::span{dwords}.first(filterTriangles(dwords.data(), flags, input));
        if (!filtered.indices.empty()) {
            clip_state |= mask;
            batch = &filtered;
        } else {
            vertices.count += first_vertex - vertices.count;
            first_vertex = -1;
            return;
        }
    }
    if (gerd->pick.pick_depth != 0) {
        PickInput pick;
        pick.indices = batch->indices;
        pick.triangles = batch->triangles;
        pick.vertices =
            batch->direct_vertex_indices ? std::span<const w8_ulong>{} : batch->vertices;
        pick.positions = std::span{vertices.positions}.subspan(first_vertex, batch->vertex_count);
        gerd->performPickTest(pick);
    }
    expandTriangles(*batch, sorted != 0);
    first_vertex = -1;
}

srVertexArray srGERD::Renderer::VertexArrays::bind(w8_ulong base)
{
    return {positions.data() + base, diffuse.data() + base,   specular.data() + base,
            st[0].data() + base,     st[1].data() + base,     q[0].data() + base,
            q[1].data() + base,      attributes.data() + base};
}

void srGERD::Renderer::bindTextureSet(w8_ulong index, bool force)
{
    const TextureSet& set = texture_sets.sets[index];
    if (force || set.texture0 != texture0) {
        texture0 = set.texture0;
        gerd->setTexture(texture0, 0);
    }
    if (force || set.texture1 != texture1) {
        texture1 = set.texture1;
        gerd->setTexture(texture1, 1);
    }
    if (force || set.shader.value != shader.value) {
        shader = set.shader;
        gerd->setShader(shader);
    }
}

void srRender::clipFlags(std::span<SRBYTE> destination, std::span<const glm::vec4> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        const glm::vec4& point = source[index];
        const float negative_w = -point.w;
        // Each axis owns two bits: the low side (below -w) and the high side (above w).
        // x uses 0x01/0x02, y 0x04/0x08, z 0x10/0x20.
        SRBYTE flags = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto low = static_cast<SRBYTE>(1u << (2 * axis));
            if (negative_w > point[axis]) flags |= low;
            if (point[axis] > point.w) flags |= static_cast<SRBYTE>(low << 1);
        }
        destination[index] = flags;
    }
}

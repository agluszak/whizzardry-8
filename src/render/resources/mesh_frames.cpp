#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/checked_integer.h"
#include "wiz8/math.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_math.h"
#include "wiz8/render/mesh_resources.h"
#include "wiz8/render/mesh_submission.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/triangle_selection.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/spatial/bounding_box.h"

class Material;

std::vector<Mesh*> g_mesh_models;

int g_decompressed_mesh_bytes;

/* Byte budget for the decompressed per-frame caches; AllocateFrameBuffers
   reclaims least-recently-used frames past it. */
static int g_decompressed_mesh_byte_limit = 0x800000;

Mesh::~Mesh()
{
    ReleaseDecompressedFrames();
    std::erase(g_mesh_models, this);
}

Mesh::Bounds Mesh::getBounds()
{
    if ((dirty_flags & (1UL << DIRTY_BOUNDS)) != 0) {
        if (parts.empty() && frames.empty())
            calculateBounds();
        else
            calculateCompoundBounds();
    }
    return {{bounds_minimum, bounds_maximum}, {bounds_center, bounds_radius}};
}

/* Recompute the model's union bounds: clear this mesh's cached box,
   accumulate every frame's bounds from the head and all parts, then
   push the resulting box, center and radius to each member. Models whose
   flags bit 2 is clear answer from the base-class bounding box; the rest
   decompress each frame's vertex table. */
void Mesh::calculateCompoundBounds()
{
    Mesh* first = this;

    BoundingBox box;
    bool has_vertices = false;
    for (auto* model : meshParts(first)) {
        if (model->vertex_location_count == 0) continue;
        for (std::size_t frame = 0; frame < std::max(size_t{1}, model->frames.size()); ++frame) {
            BoundingBox frame_box;
            if (model->flags & W8_MESH_HAS_FRAME_STORAGE)
                frame_box = model->GetFrameBounds(wiz8::checked_integer<int>(frame));
            else {
                model->calculateBounds();
                frame_box = {model->bounds_minimum, model->bounds_maximum};
            }
            if (has_vertices)
                box.include(frame_box);
            else
                box = frame_box;
            has_vertices = true;
        }
    }
    const auto center = box.center();
    const float radius = glm::distance(box.maximum, center);
    for (auto* model : meshParts(first)) model->setBounds(box.minimum, box.maximum, center, radius);
}

/* Reveal nearby vertices in world space, preserving already explored vertices. */
void Mesh::RevealVerticesNear(const glm::vec3& position, float range, const glm::mat4& world)
{
    if (vertex_location_count == 0 || !(range >= 0)) return;
    const auto lights = GetVertexLights(true, -1);
    const auto* vertices =
        (flags & W8_MESH_HAS_FRAME_STORAGE) ? GetVertexLocations(0, true, 0.0f) : getVertexLoc();
    for (int i = 0; i < vertex_location_count; ++i) {
        const auto point = glm::vec3(world * glm::vec4(vertices[i], 1));
        if (glm::distance(point, position) <= range) {
            lights[i] = glm::vec3(1);
            flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
        }
    }
}

BoundingBox Mesh::GetFrameBounds(int frame)
{
    if (static_cast<unsigned int>(frame) >= frames.size() || vertex_location_count == 0) {
        return {};
    }
    std::vector<glm::vec3> scratch;
    const auto& cache = frames[frame].locations;
    const glm::vec3* vertices;
    if (cache) {
        vertices = cache->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch);
        vertices = scratch.data();
    }
    return boundingBox(std::span<const glm::vec3>(vertices, vertex_location_count));
}

/* Apply pending vertex DIG lighting when flags bit 1 is set, then return
   the SurRender TriMesh cache. */
void Mesh::updateBakedLighting()
{
    const auto& lights = vertex_lights[vertex_light_table];
    if (lights.empty() || vertex_sunlight.empty()) return;
    auto* destination = getVertexDIG(0, 1);
    const auto* materials = getVertexMaterialTable(0, SIDE_FRONT, false);
    const bool suppress_ambient = ambient_color == glm::vec3(0) || vertex_light_table == 1;
    const glm::vec3 base_ambient =
        materials || suppress_ambient
            ? glm::vec3(0)
            : ambient_color *
                  glm::vec3(static_cast<Material*>(getMaterial(0, SIDE_FRONT))->parms.ambient);
    const auto environment = environment_offset;
    const bool add_environment = vertex_light_table != 1 && environment != glm::vec3(0);
    const float light_scale = brightness;
    const Material* previous_material = nullptr;
    glm::vec3 material_ambient(0);

    for (int vertex = 0; vertex < vertex_location_count; ++vertex) {
        glm::vec3 color;
        if (materials) {
            color = suppress_ambient ? glm::vec3(0) : ambient_color * vertex_sunlight[vertex];
            const auto* material = (*materials)[vertex].get();
            if (material) {
                if (material != previous_material) {
                    material_ambient =
                        glm::vec3(static_cast<const Material*>(material)->parms.ambient);
                    previous_material = material;
                }
                color = material_ambient == glm::vec3(0) ? glm::vec3(0) : color * material_ambient;
            }
            color += lights[vertex];
        } else if (suppress_ambient) {
            color = lights[vertex];
        } else {
            color = base_ambient * vertex_sunlight[vertex];
            color += lights[vertex];
        }
        if (add_environment) color += environment;
        if (light_scale != 1) color = light_scale == 0 ? glm::vec3(0) : color * light_scale;
        destination[vertex] = color;
    }
}

std::vector<w8_ulong> Mesh::facingPolygons(const TriMesh& mesh, std::span<const glm::vec3> normals,
                                           const glm::vec3& eye, bool positive_winding,
                                           float threshold)
{
    const auto available = mesh.active_polygons ? mesh.active_polygons->size() : mesh.polygon_count;
    if (available <= 0) return {};
    const auto count = static_cast<w8_ulong>(available);
    std::vector<w8_ulong> polygons(count);
    const auto keep = [&](w8_ulong polygon) {
        const auto position = mesh.positions[mesh.poly_vertices[polygon].y];
        const float facing = glm::dot(eye - position, normals[polygon]);
        return positive_winding ? threshold <= facing : facing <= threshold;
    };
    const auto kept =
        mesh.active_polygons
            ? clipMath::selectTriangles(polygons, *mesh.active_polygons, keep)
            : clipMath::selectTriangles(polygons, std::views::iota(w8_ulong{0}, count), keep);
    polygons.resize(kept);
    return polygons;
}

/* Wizardry-extended Mesh::renderTriMesh. Optional polygon normals enable
   a software backface cull into an owned active-polygon list and
   forces CULL_NONE; a null table leaves hardware cull at CULL_BACK. */
void Mesh::RenderTriMeshWithEquations(GpuRenderer& renderer, const TriMesh& mesh,
                                      std::span<const glm::vec3> arg_poly_equations)
{
    if (arg_poly_equations.empty()) {
        renderTriMesh(renderer, mesh);
        last_decompress_release_tick = w8_get_ticks();
        return;
    }
    if (!arg_poly_equations.empty() &&
        arg_poly_equations.size() < static_cast<std::size_t>(mesh.polygon_count))
        throw std::invalid_argument("Incomplete mesh polygon normals");
    std::vector<w8_ulong> software_polygons;
    if (mesh.polygon_count != 0 && mesh.vertex_count != 0) {
        {
            const meshSubmission::EnableScope enabled(
                renderer, (mesh.control_flags & (1UL << Mesh::CONTROL_SORTED_RENDERING)) != 0);

            if (!arg_poly_equations.empty()) {
                renderer.setCullMode(GpuRenderer::CULL_NONE);

                software_polygons = facingPolygons(
                    mesh, arg_poly_equations, glm::vec3(renderer.getInverseModelViewMatrix()[3]),
                    renderer.getWinding() == GpuRenderer::WINDING_POSITIONAL_0,
                    static_cast<float>(0.0));
            } else {
                renderer.setCullMode(GpuRenderer::CULL_BACK);
            }

            if (arg_poly_equations.empty() || !software_polygons.empty()) {
                auto submitted = mesh;
                submitted.poly_equations = nullptr;
                submitted.active_polygons = std::span<const w8_ulong>(software_polygons);
                meshSubmission::drawSides(renderer, submitted, [](w8_long side) { return side; });
            }
        }
        last_decompress_release_tick = w8_get_ticks();
    }
}

/* Copy or translate `count` vertices: a zero offset is a plain copy and a
   nonzero one goes through the vp constant-vector add. */

static bool MeshNameEquals(std::string_view left, std::string_view right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
                                                     [](unsigned char a, unsigned char b) {
                                                         return std::tolower(a) == std::tolower(b);
                                                     });
}

void Mesh::ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures)
{
    std::vector<w8_ulong> active;
    active.reserve(polygon_count);
    auto* textures = getPolygonTextureTable(0, 0, false);
    if (textures != nullptr) {
        for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count);
             ++polygon) {
            if ((*textures)[polygon] == nullptr ||
                std::none_of(excluded_textures.begin(), excluded_textures.end(), [&](auto name) {
                    return MeshNameEquals(name, (*textures)[polygon]->getName());
                })) {
                active.push_back(polygon);
            }
        }
    }
    if (active.size() == static_cast<unsigned int>(polygon_count)) {
        automap_polygons.reset();
    } else {
        automap_polygons = std::move(active);
    }
    automap_filter_active = true;
}

void Mesh::ClearAutomapPolygonFilter()
{
    automap_polygons.reset();
    // Clearing the automap override must allow the regular blank-texture scan
    // on the next draw, rather than leaving a cached "all polygons" result.
    automap_filter_active = false;
}

/* The active-polygon table selects which polygons a texture-table draw
   submits. Skin scans drop textures whose name starts with "blank"; a table
   not found among the skins uses the base/automap selection. */
Mesh::PolygonSelection Mesh::GetActivePolygons(int table, bool check_blanking)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                             [table](const auto& skin) { return skin.id == table; });
    auto& selection = skin != skins.end() ? skin->active_polygons : automap_polygons;
    auto& checked = skin != skins.end() ? skin->blanking_checked : automap_filter_active;
    if (!checked && check_blanking) {
        std::vector<w8_ulong> active;
        active.reserve(polygon_count);
        auto* textures =
            skin != skins.end() ? &skin->textures : getPolygonTextureTable(0, 0, false);
        if (textures != nullptr) {
            for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count);
                 ++polygon) {
                if ((*textures)[polygon] == nullptr ||
                    !MeshNameEquals(std::string_view((*textures)[polygon]->getName()).substr(0, 5),
                                    "blank")) {
                    active.push_back(polygon);
                }
            }
            if (active.size() != static_cast<unsigned int>(polygon_count)) {
                selection = std::move(active);
            }
        }
        checked = true;
    }
    if (!selection) {
        return std::nullopt;
    }
    return std::span<const w8_ulong>(*selection);
}

void Mesh::SetMappedVertex(int vertex, short key)
{
    mapped_vertices.insert_or_assign(key, vertex);
}

int Mesh::FindMappedIndex(short key) const
{
    if (key < 0) {
        return -1;
    }
    auto entry = mapped_vertices.find(key);
    return entry != mapped_vertices.end() ? entry->second : -1;
}

void Mesh::AddPart(std::unique_ptr<Mesh> part)
{
    parts.push_back(std::move(part));
    setDirty(DIRTY_BOUNDS);
}

std::optional<MappedVertex> findMappedVertex(Mesh* model, int key)
{
    if (key < 0) return std::nullopt;
    for (auto* part : meshParts(model)) {
        if (const int index = part->FindMappedIndex(static_cast<short>(key)); index >= 0)
            return MappedVertex{part, index};
    }
    return std::nullopt;
}

/* One frame's compressed vertex table, refused outright when there is no
   table at all. */
short* Mesh::GetVertex(unsigned int frame)
{
    return frame < frames.size() ? frames[frame].compressed_locations.data() : nullptr;
}

void Mesh::InitializeVertexFrames(int count)
{
    if (count <= 0) {
        Fail("uiFrames");
        return;
    }
    // ReadMesh calls this for each frame; it must not discard earlier frames.
    if (!frames.empty()) {
        return;
    }
    std::vector<Frame> storage(count);
    for (auto& frame : storage) {
        frame.compressed_locations.resize(vertex_location_count * 3);
        frame.compressed_vertex_normals.resize(vertex_location_count * 3);
        frame.compressed_polygon_normals.resize(polygon_count * 3);
    }
    g_mesh_models.push_back(this);
    frames = std::move(storage);
    flags |= W8_MESH_HAS_FRAME_STORAGE;
}

/* Drop every decompressed float cache, returning the bytes actually released.
   Resetting the optional destroys its vector allocation, unlike vector::clear. */
int Mesh::ReleaseDecompressedFrames()
{
    int released = 0;
    for (auto& frame : frames) {
        for (auto* cache : {&frame.locations, &frame.vertex_normals, &frame.polygon_normals}) {
            if (*cache) {
                released += (*cache)->capacity() * sizeof(glm::vec3);
                cache->reset();
            }
        }
    }
    last_decompress_release_tick = w8_get_ticks();
    g_decompressed_mesh_bytes -= released;
    return released;
}

/* Evict least-recently-used decompressed frames until `needed` bytes are
   available, or give up when every eligible model has been drained. */
unsigned char ReclaimDecompressedBytes(unsigned int needed, const Mesh* retained_model)
{
    unsigned int released = 0;
    while (released < needed) {
        Mesh* oldest = nullptr;
        for (Mesh* model : g_mesh_models) {
            if (model == retained_model) {
                continue;
            }
            bool resident =
                std::any_of(model->frames.begin(), model->frames.end(), [](const auto& frame) {
                    return (frame.locations && !frame.locations->empty()) ||
                           (frame.vertex_normals && !frame.vertex_normals->empty()) ||
                           (frame.polygon_normals && !frame.polygon_normals->empty());
                });
            if (resident && (oldest == nullptr || model->last_decompress_release_tick <
                                                      oldest->last_decompress_release_tick)) {
                oldest = model;
            }
        }
        if (oldest == nullptr) {
            return 0;
        }
        released += oldest->ReleaseDecompressedFrames();
    }
    return 1;
}

int Mesh::FindSkinTable(std::string_view name)
{
    for (const auto& skin : skins) {
        if (MeshNameEquals(name, skin.name)) {
            // AddExistingDamageStage stores this as the GetTextureTable ID,
            // not the record's changing position after another skin is removed.
            return skin.id;
        }
    }
    return -1;
}

MeshResources<Texture>* Mesh::GetTextureTable(int table)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                             [table](const auto& skin) { return skin.id == table; });
    return skin != skins.end() ? &skin->textures : nullptr;
}

/* Clone one polygon-texture table under an owned name. Table ids are the lowest
   free non-negative integer and remain stable independently of vector position. */
int Mesh::CreateSkinTable(std::string_view name, int base_table)
{
    if (FindSkinTable(name) != -1) {
        return -1;
    }
    auto* source = GetTextureTable(base_table);
    if (source == nullptr) {
        source = getPolygonTextureTable(0, 0, false);
    }
    if (source == nullptr) {
        return -1;
    }
    int table = 0;
    while (std::any_of(skins.begin(), skins.end(),
                       [table](const auto& skin) { return skin.id == table; })) {
        ++table;
    }
    Skin skin{table, std::string(name), *source, std::nullopt, false};
    skins.push_back(std::move(skin));
    return table;
}

void Mesh::RemoveSkinTable(int index)
{
    if (static_cast<unsigned int>(index) < skins.size()) {
        skins.erase(std::next(skins.begin(), index));
    }
}

/* Skin tables are named with the owning cycle plus a one-character suffix.
   Final teardown removes every table whose name has that cycle prefix. */

static void DecompressMeshNormals(const unsigned char* normals, int count, glm::vec3* destination)
{
    // The former lookup table (WIZ8 0x00659ce8, ready flag 0x0065a0ef) only decoded signed bytes.
    const auto expand = [](unsigned char component) {
        return std::clamp(static_cast<signed char>(component) * (1.0f / 127.0f), -1.0f, 1.0f);
    };
    for (int index = 0; index < count; ++index) {
        const unsigned char* source = &normals[index * 3];
        destination[index] = glm::vec3(expand(source[0]), expand(source[1]), expand(source[2]));
    }
}

/* Expand one frame's compressed table into `destination`: bit 1 the vertex
   locations, bit 2 the vertex normals, bit 4 the polygon normals. */
unsigned char Mesh::DecompressFrame(int frame, unsigned char flags,
                                    std::span<glm::vec3> destination)
{
    if (static_cast<unsigned int>(frame) >= frames.size()) {
        return 0;
    }
    const auto& storage = frames[frame];
    const auto count = flags & (W8_MESH_FRAME_LOCATIONS | W8_MESH_FRAME_VERTEX_NORMALS)
                           ? vertex_location_count
                           : polygon_count;
    if (destination.size() < static_cast<std::size_t>(count)) return 0;
    if (flags & W8_MESH_FRAME_LOCATIONS) {
        for (int index = 0; index < vertex_location_count; ++index) {
            const short* source = &storage.compressed_locations[index * 3];
            destination[index] = glm::vec3(source[0] * vertex_compression_scale,
                                           source[1] * vertex_compression_scale,
                                           source[2] * vertex_compression_scale);
        }
        return 1;
    }
    if (flags & W8_MESH_FRAME_VERTEX_NORMALS) {
        DecompressMeshNormals(storage.compressed_vertex_normals.data(), vertex_location_count,
                              destination.data());
        return 1;
    }
    if (flags & W8_MESH_FRAME_POLYGON_NORMALS) {
        DecompressMeshNormals(storage.compressed_polygon_normals.data(), polygon_count,
                              destination.data());
        return 1;
    }
    return 0;
}

/* Allocate one frame's decompressed caches for the tables named by `flags`
   (bit 0 locations, bit 1 vertex normals, bit 2 polygon normals), reclaiming
   least-recently-used frames when the byte budget would overflow. */
unsigned char Mesh::AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags)
{
    if (uiFrame >= frames.size()) {
        return 0;
    }
    auto& frame = frames[uiFrame];
    auto allocate = [&](auto& cache, int count, unsigned char bit) {
        if ((flags & bit) == 0 || cache) {
            return;
        }
        int needed = count * sizeof(glm::vec3);
        if (g_decompressed_mesh_byte_limit <= g_decompressed_mesh_bytes + needed) {
            // Never evict this model between returning positions and normals,
            // or between loading the two frames of an interpolation. The budget
            // is soft only when that working set alone cannot fit; drain other
            // models first even if they cannot supply the entire request.
            ReclaimDecompressedBytes(needed, this);
        }
        cache.emplace(count, glm::vec3(0.0f, 0.0f, 0.0f));
        g_decompressed_mesh_bytes += cache->capacity() * sizeof(glm::vec3);
    };
    allocate(frame.locations, vertex_location_count, W8_MESH_FRAME_LOCATIONS);
    allocate(frame.vertex_normals, vertex_location_count, W8_MESH_FRAME_VERTEX_NORMALS);
    allocate(frame.polygon_normals, polygon_count, W8_MESH_FRAME_POLYGON_NORMALS);
    last_decompress_release_tick = w8_get_ticks();
    return 1;
}

/* Return frame `frame`'s vertex locations, decompressing on demand. When
   `interpolation` is positive and another frame follows, both frames are
   decompressed and lerped into lerp_buffer (m_pLerpBuffer). */
glm::vec3* Mesh::GetVertexLocations(unsigned int frame, bool load, float interpolation)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    if (0.0f < interpolation && frame + 1 < frames.size()) {
        const auto* current = GetVertexLocations(frame, true, 0.0f);
        const auto* next = GetVertexLocations(frame + 1, true, 0.0f);
        lerp_buffer.resize(vertex_location_count);
        if (vertex_location_count != 0) {
            if (interpolation == 1.0f) {
                std::copy_n(next, vertex_location_count, lerp_buffer.data());
            } else {
                for (int i = 0; i < vertex_location_count; ++i)
                    lerp_buffer[i] = glm::mix(current[i], next[i], interpolation);
            }
        }
        return lerp_buffer.data();
    }
    return frameTable(frame, load, W8_MESH_FRAME_LOCATIONS);
}

glm::vec3* Mesh::GetVertexNormals(unsigned int frame, bool load)
{
    return frameTable(frame, load, W8_MESH_FRAME_VERTEX_NORMALS);
}

glm::vec3* Mesh::GetPolygonNormals(unsigned int frame, bool load)
{
    return frameTable(frame, load, W8_MESH_FRAME_POLYGON_NORMALS);
}

glm::vec3* Mesh::frameTable(unsigned int frame, bool load, unsigned char kind)
{
    if (frame >= frames.size()) return nullptr;
    auto& storage = frames[frame];
    auto& cache = kind == W8_MESH_FRAME_LOCATIONS        ? storage.locations
                  : kind == W8_MESH_FRAME_VERTEX_NORMALS ? storage.vertex_normals
                                                         : storage.polygon_normals;
    if (!cache) {
        AllocateFrameBuffers(frame, kind);
        if (load) DecompressFrame(frame, kind, cache.value());
    }
    return cache.value().data();
}

Mesh::FrameGeometry Mesh::frameGeometry(unsigned int frame, float interpolation)
{
    if (frame >= frames.size()) throw std::out_of_range("Mesh frame index out of range");
    auto* positions = GetVertexLocations(frame, true, interpolation);
    auto* normals = GetVertexNormals(frame, true);
    const auto* polygon_normals = GetPolygonNormals(frame, true);
    return {{positions, static_cast<std::size_t>(vertex_location_count)},
            {normals, static_cast<std::size_t>(vertex_location_count)},
            {polygon_normals, static_cast<std::size_t>(polygon_count)}};
}

void Mesh::ClearVertexLights()
{
    std::ranges::fill(GetVertexLights(true, 1), glm::vec3(0));
    flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
}

void Mesh::SetAmbientColor(const glm::vec3& color, const glm::vec3& environment, float brightness)
{
    if (ambient_color != color || environment_offset != environment ||
        this->brightness != brightness) {
        ambient_color = color;
        environment_offset = environment;
        this->brightness = brightness;
        flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    }
}

static inline char CompressNormalByte(float value)
{
    // Invalid imported geometry must not trigger an undefined float-to-byte cast.
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<char>(value);
}

static void CompressMeshNormals(const std::vector<glm::vec3>& normals,
                                std::vector<unsigned char>& compressed)
{
    compressed.resize(normals.size() * 3);
    for (std::size_t i = 0; i < normals.size(); ++i) {
        const auto normal = normalizedOrZero(normals[i]) * 127.0f;
        for (int component = 0; component < 3; ++component)
            compressed[i * 3 + component] = CompressNormalByte(normal[component]);
    }
}

/* Build one frame's compressed polygon and vertex normals from its vertex
   locations. Vertex normals are summed per polygon corner, remapped through
   the shade index table when there is one, unitized, and stored as signed
   bytes scaled by 127. */
void Mesh::ComputeFrameNormals(int frame)
{
    if (static_cast<unsigned int>(frame) >= frames.size() || polygon_count == 0 ||
        vertex_location_count == 0) {
        return;
    }

    glm::ivec3* poly_vertex = getPolyVertex();
    if (poly_vertex == nullptr) {
        return;
    }
    std::vector<glm::vec3> pnorm(polygon_count);
    auto& storage = frames[frame];
    std::vector<glm::vec3> scratch;
    const glm::vec3* l;
    if (storage.locations) {
        l = storage.locations->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch);
        l = scratch.data();
    }

    for (int poly = 0; poly < polygon_count; ++poly)
        pnorm[poly] = meshMath::polygonNormal(std::span<const glm::vec3>(l, vertex_location_count),
                                              poly_vertex[poly]);

    const auto* shade_index = getVertexShadeIndex(0);
    const auto vnorm = meshMath::vertexNormalSums(
        std::span<const glm::ivec3>(poly_vertex, polygon_count), std::span<const glm::vec3>(pnorm),
        vertex_location_count,
        shade_index ? std::span<const w8_ulong>(shade_index, vertex_location_count)
                    : std::span<const w8_ulong>{});

    CompressMeshNormals(vnorm, storage.compressed_vertex_normals);
    CompressMeshNormals(pnorm, storage.compressed_polygon_normals);

    if (storage.vertex_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS, *storage.vertex_normals);
    }
    if (storage.polygon_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS, *storage.polygon_normals);
    }
}

/* Thirteen-byte forwarder onto the per-frame normal builder. */
std::span<glm::vec3> Mesh::GetVertexLights(bool initialize, int table)
{
    if (table == -1) {
        table = vertex_light_table;
    }
    auto& lights = vertex_lights[table];
    if (lights.empty() && initialize) {
        lights.resize(vertex_location_count, glm::vec3(0));
        if (!vertex_sunlight.empty()) {
            vertex_lighting_ready = true;
        }
    }
    return lights;
}

std::span<float> Mesh::GetVertexSunlight(bool initialize)
{
    if (vertex_sunlight.empty() && initialize) {
        vertex_sunlight.resize(vertex_location_count, 1.0f);
        vertex_lighting_ready = true;
    }
    return vertex_sunlight;
}

void Mesh::FinalizeVertexFrame(int frame)
{
    ComputeFrameNormals(frame);
}

/* Retail ICF folds this empty thiscall onto W8OptionsGraphicsPanel::OnDragEnd
   at 0x005AA400 (OptionsScreen.cpp). This source function has no separately
   retained retail address, so it intentionally has no FUNCTION marker. */

#include "wiz8/engine_code/ReadLevel.h"
#include "surrender/srModel.h"
#include <cstddef>
#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <ranges>
#include "surrender/triangle_selection.h"

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModel.h"
#include "wiz8/float_constants.h"
#include "wiz8/sr_api.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTypeRegistry.h"
#include "wiz8/engine_code/Octree.h"
#include "surrender/srMath.h"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srFlags.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srVertexPipe.h"

class srMaterialIFace;

extern float g_monster_light_scale;

// GLOBAL: WIZ8 0x00659cb8
std::vector<stMeshModel*> g_mesh_models;

// GLOBAL: WIZ8 0x0065a0e8
int g_decompressed_mesh_bytes;

/* Byte budget for the decompressed per-frame caches; AllocateFrameBuffers
   reclaims least-recently-used frames past it. */
// GLOBAL: WIZ8 0x00609d34
static int g_decompressed_mesh_byte_limit = 0x800000;

// FUNCTION: WIZ8 0x00470B00
stMeshModel::stMeshModel(w8_long polygons, w8_long vertices)
    : srClassSupport<stMeshModel, srMeshModel, 0x10003>(polygons, vertices)
{
    vertex_lighting_ready = false;
    flags = W8_MESH_VERTEX_LIGHTING_DIRTY;
    ambient_color = glm::vec3(-1);
    vertex_light_table = 0;
    vertex_compression_scale = 1.0f;
    last_decompress_release_tick = w8_get_ticks();
    render_control.value &= ~0x10UL;
    setDirty(DIRTY_BOUNDS);
    automap_filter_active = false;
}

// FUNCTION: WIZ8 0x00470ED0
stMeshModel::~stMeshModel()
{
    ReleaseDecompressedFrames();
    std::erase(g_mesh_models, this);
}

srModel::Bounds stMeshModel::getBounds()
{
    if ((dirty_flags.value & (1UL << DIRTY_BOUNDS)) != 0) {
        CalculateLinkedBounds();
    }
    return {{bounds_minimum, bounds_maximum}, {bounds_center, bounds_radius}};
}


/* Recompute the model's union bounds: clear this mesh's cached box,
   accumulate every frame's bounds from the head and all parts, then
   push the resulting box, center and radius to each member. Models whose
   flags bit 2 is clear answer from the base-class bounding box; the rest
   decompress each frame's vertex table. */
// FUNCTION: WIZ8 0x00471E10
void stMeshModel::CalculateLinkedBounds()
{
    stMeshModel* first = head ? head : this;

    srBoundingBox box;
    bool has_vertices = false;
    for (auto* model : Meshes(first)) {
        if (model->vertex_location_count == 0)
            continue;
        for (std::size_t frame = 0; frame < std::max(size_t{1}, model->frames.size()); ++frame) {
            srBoundingBox frame_box;
            if (model->flags & W8_MESH_HAS_FRAME_STORAGE)
                frame_box = model->GetFrameBounds(frame);
            else
                frame_box = model->srMeshModel::getBounds().box;
            if (has_vertices)
                box.include(frame_box);
            else
                box = frame_box;
            has_vertices = true;
        }
    }
    const auto center = box.center();
    const float radius = glm::distance(box.maximum, center);
    for (auto* model : Meshes(first))
        model->setBounds(box.minimum, box.maximum, center, radius);
}

/* Reveal nearby vertices in world space, preserving already explored vertices. */
void stMeshModel::RevealVerticesNear(const glm::vec3& position, float range, const glm::mat4& world)
{
    if (vertex_location_count == 0 || !(range >= 0))
        return;
    const auto lights = GetVertexLights(true, -1);
    const auto* vertices =
        (flags & W8_MESH_HAS_FRAME_STORAGE) ? GetVertexLocations(0, true, 0.0f) : getVertexLoc();
    for (int i = 0; i < vertex_location_count; ++i) {
        const auto point = glm::vec3(world * glm::vec4(vertices[i].glmValue(), 1));
        if (glm::distance(point, position) <= range) {
            lights[i] = glm::vec3(1);
            flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
        }
    }
}
// FUNCTION: WIZ8 0x00473190
srBoundingBox stMeshModel::GetFrameBounds(int frame)
{
    if (static_cast<unsigned int>(frame) >= frames.size() || vertex_location_count == 0) {
        return {};
    }
    std::vector<srVector3T<float>> scratch;
    const auto& cache = frames[frame].locations;
    const srVector3T<float>* vertices;
    if (cache) {
        vertices = cache->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch.data());
        vertices = scratch.data();
    }
    return boundingBox(std::span<const srVector3>(vertices, vertex_location_count));
}

/* Apply pending vertex DIG lighting when flags bit 1 is set, then return
   the SurRender TriMesh cache. */
// FUNCTION: WIZ8 0x00472270
void stMeshModel::updateBakedLighting()
{
    const auto& lights = vertex_lights[vertex_light_table];
    if (lights.empty() || vertex_sunlight.empty())
        return;
    auto* destination = getVertexDIG(0, 1);
    const auto* materials = getVertexMaterial(0, SIDE_FRONT, 0);
    const bool suppress_ambient = ambient_color == glm::vec3(0) || vertex_light_table == 1;
    const glm::vec3 base_ambient = materials || suppress_ambient
        ? glm::vec3(0)
        : ambient_color * glm::vec3(static_cast<srMaterial*>(getMaterial(0, SIDE_FRONT))->parms.ambient);
    const auto environment = g_environment_offset;
    const bool add_environment = vertex_light_table != 1 && environment != glm::vec3(0);
    const float light_scale = g_monster_light_scale;
    const srMaterialIFace* previous_material = nullptr;
    glm::vec3 material_ambient(0);

    for (int vertex = 0; vertex < vertex_location_count; ++vertex) {
        glm::vec3 color;
        if (materials) {
            color = suppress_ambient ? glm::vec3(0) : ambient_color * vertex_sunlight[vertex];
            const auto* material = materials[vertex].get();
            if (material) {
                if (material != previous_material) {
                    material_ambient = glm::vec3(static_cast<const srMaterial*>(material)->parms.ambient);
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
        if (add_environment)
            color += environment;
        if (light_scale != 1)
            color = light_scale == 0 ? glm::vec3(0) : color * light_scale;
        destination[vertex] = color;
    }
}

srMeshModel::TriMesh stMeshModel::getTriMesh()
{
    if ((flags & W8_MESH_VERTEX_LIGHTING_DIRTY) != 0 && !g_render_unlit) {
        updateBakedLighting();
        flags &= ~static_cast<unsigned int>(W8_MESH_VERTEX_LIGHTING_DIRTY);
    }
    return srMeshModel::getTriMesh();
}

// FUNCTION: WIZ8 0x00470360
void stMeshModel::renderTriMesh(srGERD& renderer, const TriMesh& mesh)
{
    RenderTriMeshWithEquations(renderer, mesh, 0);
}

std::vector<w8_ulong> stMeshModel::facingPolygons(const TriMesh& mesh,
                                                std::span<const srVector3T<float>> normals,
                                                const glm::vec3& eye, bool positive_winding,
                                                float threshold)
{
    const auto available = mesh.active_polygons ? mesh.active_polygons->size() : mesh.polygon_count;
    if (available <= 0) return {};
    const auto count = static_cast<w8_ulong>(available);
    std::vector<w8_ulong> polygons(count);
    const auto keep = [&](w8_ulong polygon) {
        const auto position = mesh.positions[mesh.poly_vertices[polygon].y].glmValue();
        const float facing = glm::dot(eye - position, normals[polygon].glmValue());
        return positive_winding ? threshold <= facing : facing <= threshold;
    };
    const auto kept = mesh.active_polygons
        ? srRender::selectTriangles(polygons, *mesh.active_polygons, keep)
        : srRender::selectTriangles(polygons, std::views::iota(w8_ulong{0}, count), keep);
    polygons.resize(kept);
    return polygons;
}

/* Wizardry-extended srMeshModel::renderTriMesh. Optional polygon normals enable
   a software backface cull into an owned active-polygon list and
   forces CULL_NONE; a null table leaves hardware cull at CULL_BACK unless
   g_render_cull_front already requested front culling. */
// FUNCTION: WIZ8 0x00470380
void stMeshModel::RenderTriMeshWithEquations(srGERD& renderer, const TriMesh& mesh,
                                             const srVector3T<float>* arg_poly_equations)
{
    std::vector<w8_ulong> software_polygons;
    srShader shader;

    if (mesh.polygon_count != 0 && mesh.vertex_count != 0) {
        renderer.pushEnable();
        if ((g_inverted_depth_render ||
             (mesh.control_flags & (1UL << srMeshModel::CONTROL_SORTED_RENDERING)) != 0) &&
            !renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
            renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
        }

        if (g_render_cull_front) {
            renderer.setCullMode(srGERD::CULL_NONE);
        } else if (arg_poly_equations != 0) {
            renderer.setCullMode(srGERD::CULL_NONE);

            software_polygons = facingPolygons(mesh,
                {arg_poly_equations, static_cast<std::size_t>(mesh.polygon_count)},
                glm::vec3(renderer.getInverseModelViewMatrix()[3]),
                renderer.getWinding() == srGERD::WINDING_POSITIONAL_0,
                static_cast<float>(g_double_zero));
        } else {
            renderer.setCullMode(srGERD::CULL_BACK);
        }

        for (int side = 1; side >= 0; --side) {
            if ((mesh.control_flags & (1u << side)) != 0 &&
                (arg_poly_equations == nullptr || !software_polygons.empty())) {
                srTriMeshPipeline* pipeline = srTriMeshPipeline::Get(&renderer);
                pipeline->sort_bias = mesh.sort_bias;
                pipeline->triangles = mesh.poly_vertices;
                pipeline->triangle_count = static_cast<w8_ulong>(mesh.polygon_count);
                pipeline->positions = mesh.positions;
                pipeline->vertex_count = static_cast<w8_ulong>(mesh.vertex_count);
                pipeline->vertex_extras = mesh.normals;

                if (arg_poly_equations != 0) {
                    pipeline->projected_vertices = 0;
                } else {
                    pipeline->projected_vertices = mesh.poly_equations;
                }

                if (arg_poly_equations != 0) {
                    pipeline->active_triangles = std::span<const w8_ulong>(software_polygons);
                } else if (mesh.active_polygons.has_value()) {
                    pipeline->active_triangles = mesh.active_polygons;
                }

                pipeline->SetMeshBounds(mesh);

                pipeline->records.resize(mesh.pass_count + 1);
                pipeline->passes.resize(mesh.pass_count + 1);
                pipeline->PrepareSlot();
                for (w8_long pass = 0; pass < mesh.pass_count; ++pass) {
                    shader = mesh.shaders[pass];
                    if (g_inverted_depth_render) {
                        shader.value = (shader.value & 0xfffffffeUL) | 6UL;
                    }
                    pipeline->AppendMeshPass(mesh, pass, side, shader);
                }

                pipeline->FlushIfCurrent();
            }
        }

        renderer.popEnable();
        last_decompress_release_tick = w8_get_ticks();
    }
}

// FUNCTION: WIZ8 0x0046ffa0
int __fastcall IsZeroVector(const srVector3T<float>* vector)
{
    if (vector->x == g_float_zero && vector->y == g_float_zero && vector->z == g_float_zero) {
        return 1;
    }
    return 0;
}

/* Copy or translate `count` vertices: a zero offset is a plain copy and a
   nonzero one goes through the vp constant-vector add. */
// FUNCTION: WIZ8 0x00470040


static bool MeshNameEquals(std::string_view left, std::string_view right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           });
}

// FUNCTION: WIZ8 0x00473fa0
void stMeshModel::ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures)
{
    std::vector<w8_ulong> active;
    active.reserve(polygon_count);
    auto* textures = getPolyTexture(0, 0, 0);
    if (textures != nullptr) {
        for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count); ++polygon) {
            if (textures[polygon] == nullptr ||
                std::none_of(excluded_textures.begin(), excluded_textures.end(), [&](auto name) {
                    return MeshNameEquals(name, textures[polygon]->getName());
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

// FUNCTION: WIZ8 0x00474120
void stMeshModel::ClearAutomapPolygonFilter()
{
    automap_polygons.reset();
    // Clearing the automap override must allow the regular blank-texture scan
    // on the next draw, rather than leaving a cached "all polygons" result.
    automap_filter_active = false;
}

/* The active-polygon table selects which polygons a texture-table draw
   submits. Skin scans drop textures whose name starts with "blank"; a table
   not found among the skins uses the base/automap selection. */
// FUNCTION: WIZ8 0x00473CD0
srMeshModel::PolygonSelection stMeshModel::GetActivePolygons(int table, bool check_blanking)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                            [table](const auto& skin) { return skin.id == table; });
    auto& selection = skin != skins.end() ? skin->active_polygons : automap_polygons;
    auto& checked = skin != skins.end() ? skin->blanking_checked : automap_filter_active;
    if (!checked && check_blanking) {
        std::vector<w8_ulong> active;
        active.reserve(polygon_count);
        auto* textures = skin != skins.end() ? skin->textures.data() : getPolyTexture(0, 0, 0);
        if (textures != nullptr) {
            for (unsigned int polygon = 0; polygon < static_cast<unsigned int>(polygon_count); ++polygon) {
                if (textures[polygon] == nullptr ||
                    !MeshNameEquals(std::string_view(textures[polygon]->getName()).substr(0, 5), "blank")) {
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

// FUNCTION: WIZ8 0x00471160
void stMeshModel::SetMappedVertex(short vertex, short key)
{
    mapped_vertices.insert_or_assign(key, vertex);
}

// FUNCTION: WIZ8 0x004712d0
int stMeshModel::FindMappedIndex(short key)
{
    if (key < 0) {
        return -1;
    }
    auto entry = mapped_vertices.find(key);
    return entry != mapped_vertices.end() ? entry->second : -1;
}

void stMeshModel::AddPart(std::unique_ptr<stMeshModel> part)
{
    part->head = this;
    parts.push_back(std::move(part));
}

stMeshModel* stMeshModel::Next() const
{
    if (head == nullptr) {
        return parts.empty() ? nullptr : parts.front().get();
    }
    auto found = std::find_if(head->parts.begin(), head->parts.end(),
                              [this](const auto& part) { return part.get() == this; });
    return found + 1 == head->parts.end() ? nullptr : (found + 1)->get();
}

/* One frame's compressed vertex table, refused outright when there is no
   table at all. */
// FUNCTION: WIZ8 0x00471aa0
short* stMeshModel::GetVertex(unsigned int frame)
{
    return frame < frames.size() ? frames[frame].compressed_locations.data() : nullptr;
}

// FUNCTION: WIZ8 0x00473b00
void stMeshModel::InitializeVertexFrames(int count)
{
    if (count <= 0) {
        srAssertFail("uiFrames", "C:\\Projects\\Wizardry 8\\Engine Code\\stMeshModel.cpp", 0x6e8,
                     0);
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
// FUNCTION: WIZ8 0x004739e0
int stMeshModel::ReleaseDecompressedFrames()
{
    int released = 0;
    for (auto& frame : frames) {
        for (auto* cache : {&frame.locations, &frame.vertex_normals, &frame.polygon_normals}) {
            if (*cache) {
                released += (*cache)->capacity() * sizeof(srVector3T<float>);
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
// FUNCTION: WIZ8 0x00473BF0
unsigned char ReclaimDecompressedBytes(unsigned int needed, const stMeshModel* retained_model)
{
    unsigned int released = 0;
    while (released < needed) {
        stMeshModel* oldest = nullptr;
        for (stMeshModel* model : g_mesh_models) {
            if (model == retained_model) {
                continue;
            }
            bool resident = std::any_of(model->frames.begin(), model->frames.end(),
                                       [](const auto& frame) {
                                           return (frame.locations && !frame.locations->empty()) ||
                                                  (frame.vertex_normals && !frame.vertex_normals->empty()) ||
                                                  (frame.polygon_normals && !frame.polygon_normals->empty());
                                       });
            if (resident && (oldest == nullptr ||
                             model->last_decompress_release_tick < oldest->last_decompress_release_tick)) {
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

// FUNCTION: WIZ8 0x004736d0
int stMeshModel::FindSkinTable(std::string_view name)
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

// FUNCTION: WIZ8 0x00473720
std::shared_ptr<srTextureIFace>* stMeshModel::GetTextureTable(int table)
{
    auto skin = std::find_if(skins.begin(), skins.end(),
                            [table](const auto& skin) { return skin.id == table; });
    return skin != skins.end() ? skin->textures.data() : nullptr;
}

/* Clone one polygon-texture table under an owned name. Table ids are the lowest
   free non-negative integer and remain stable independently of vector position. */
// FUNCTION: WIZ8 0x00473260
int stMeshModel::CreateSkinTable(std::string_view name, int base_table)
{
    if (FindSkinTable(name) != -1) {
        return -1;
    }
    auto* source = GetTextureTable(base_table);
    if (source == nullptr) {
        source = getPolyTexture(0, 0, 0);
    }
    if (source == nullptr) {
        return -1;
    }
    int table = 0;
    while (std::any_of(skins.begin(), skins.end(),
                       [table](const auto& skin) { return skin.id == table; })) {
        ++table;
    }
    Skin skin{table, std::string(name), {source, source + polygon_count}, std::nullopt, false};
    skins.push_back(std::move(skin));
    return table;
}

// FUNCTION: WIZ8 0x00473830
void stMeshModel::RemoveSkinTable(int index)
{
    if (static_cast<unsigned int>(index) < skins.size()) {
        skins.erase(skins.begin() + index);
    }
}

/* Skin tables are named with the owning cycle plus a one-character suffix.
   Final teardown removes every table whose name has that cycle prefix. */
// FUNCTION: WIZ8 0x00473780
void stMeshModel::RemoveSkinTablesForCycle(std::string_view cycle_name)
{
    if (!cycle_name.empty()) {
        std::erase_if(skins, [cycle_name](const auto& skin) {
            std::string_view name = skin.name;
            return name.size() > 1 && MeshNameEquals(cycle_name, name.substr(0, name.size() - 1));
        });
    }
}

static void DecompressMeshNormals(const unsigned char* normals, int count,
                                  srVector3T<float>* destination)
{
    // The former lookup table (WIZ8 0x00659ce8, ready flag 0x0065a0ef) only decoded signed bytes.
    const auto expand = [](unsigned char component) {
        return std::clamp(static_cast<signed char>(component) * (1.0f / 127.0f), -1.0f, 1.0f);
    };
    for (int index = 0; index < count; ++index) {
        const unsigned char* source = &normals[index * 3];
        destination[index].Set(expand(source[0]), expand(source[1]), expand(source[2]));
    }
}

/* Expand one frame's compressed table into `destination`: bit 1 the vertex
   locations, bit 2 the vertex normals, bit 4 the polygon normals. */
// FUNCTION: WIZ8 0x00471930
unsigned char stMeshModel::DecompressFrame(int frame, unsigned char flags,
                                           srVector3T<float>* destination)
{
    if (static_cast<unsigned int>(frame) >= frames.size()) {
        return 0;
    }
    const auto& storage = frames[frame];
    if (flags & W8_MESH_FRAME_LOCATIONS) {
        for (int index = 0; index < vertex_location_count; ++index) {
            const short* source = &storage.compressed_locations[index * 3];
            destination[index].Set(source[0] * vertex_compression_scale,
                                   source[1] * vertex_compression_scale,
                                   source[2] * vertex_compression_scale);
        }
        return 1;
    }
    if (flags & W8_MESH_FRAME_VERTEX_NORMALS) {
        DecompressMeshNormals(storage.compressed_vertex_normals.data(), vertex_location_count,
                              destination);
        return 1;
    }
    if (flags & W8_MESH_FRAME_POLYGON_NORMALS) {
        DecompressMeshNormals(storage.compressed_polygon_normals.data(), polygon_count,
                              destination);
        return 1;
    }
    return 0;
}

/* Allocate one frame's decompressed caches for the tables named by `flags`
   (bit 0 locations, bit 1 vertex normals, bit 2 polygon normals), reclaiming
   least-recently-used frames when the byte budget would overflow. */
// FUNCTION: WIZ8 0x00471720
unsigned char stMeshModel::AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags)
{
    if (uiFrame >= frames.size()) {
        return 0;
    }
    auto& frame = frames[uiFrame];
    auto allocate = [&](auto& cache, int count, unsigned char bit) {
        if ((flags & bit) == 0 || cache) {
            return;
        }
        int needed = count * sizeof(srVector3T<float>);
        if (g_decompressed_mesh_byte_limit <= g_decompressed_mesh_bytes + needed) {
            // Never evict this model between returning positions and normals,
            // or between loading the two frames of an interpolation. The budget
            // is soft only when that working set alone cannot fit; drain other
            // models first even if they cannot supply the entire request.
            ReclaimDecompressedBytes(needed, this);
        }
        cache.emplace(count, srVector3T<float>(0.0f, 0.0f, 0.0f));
        g_decompressed_mesh_bytes += cache->capacity() * sizeof(srVector3T<float>);
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
// FUNCTION: WIZ8 0x00471AD0
srVector3T<float>* stMeshModel::GetVertexLocations(unsigned int frame, bool load,
                                                   float interpolation)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    if (g_float_zero < interpolation && frame + 1 < frames.size()) {
        const auto* current = GetVertexLocations(frame, true, 0.0f);
        const auto* next = GetVertexLocations(frame + 1, true, 0.0f);
        lerp_buffer.resize(vertex_location_count);
        if (vertex_location_count != 0) {
            if (interpolation == g_float_one) {
                std::copy_n(next, vertex_location_count, lerp_buffer.data());
            } else {
                for (int i = 0; i < vertex_location_count; ++i)
                    lerp_buffer[i] =
                        glm::mix(current[i].glmValue(), next[i].glmValue(), interpolation);
            }
        }
        return lerp_buffer.data();
    }
    auto& cache = frames[frame].locations;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_LOCATIONS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, cache->data());
        }
    }
    return cache->data();
}

/* Return frame `frame`'s vertex normals, decompressing on demand when `load`
   is set. */
// FUNCTION: WIZ8 0x00471CA0
srVector3T<float>* stMeshModel::GetVertexNormals(unsigned int frame, bool load)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    auto& cache = frames[frame].vertex_normals;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_VERTEX_NORMALS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS, cache->data());
        }
    }
    return cache->data();
}

// FUNCTION: WIZ8 0x00471D00
srVector3T<float>* stMeshModel::GetPolygonNormals(unsigned int frame, bool load)
{
    if (frame >= frames.size()) {
        return nullptr;
    }
    auto& cache = frames[frame].polygon_normals;
    if (!cache) {
        AllocateFrameBuffers(frame, W8_MESH_FRAME_POLYGON_NORMALS);
        if (load) {
            DecompressFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS, cache->data());
        }
    }
    return cache->data();
}

void stMeshModel::ClearVertexLights()
{
    std::ranges::fill(GetVertexLights(true, 1), glm::vec3(0));
    flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
}

// FUNCTION: WIZ8 0x00472990
void stMeshModel::SetAmbientColor(const glm::vec3& color)
{
    if (!(ambient_color == color)) {
        ambient_color = color;
        flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    }
}

static inline char CompressNormalByte(float value)
{
    // Invalid imported geometry must not trigger an undefined float-to-byte cast.
    if (!_finite(value)) {
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
// FUNCTION: WIZ8 0x004729F0
void stMeshModel::ComputeFrameNormals(int frame)
{
    if (static_cast<unsigned int>(frame) >= frames.size() || polygon_count == 0 ||
        vertex_location_count == 0) {
        return;
    }

    srVector3i* poly_vertex = getPolyVertex();
    if (poly_vertex == nullptr) {
        return;
    }
    std::vector<glm::vec3> pnorm(polygon_count);
    auto& storage = frames[frame];
    std::vector<srVector3T<float>> scratch;
    const srVector3T<float>* l;
    if (storage.locations) {
        l = storage.locations->data();
    } else {
        scratch.resize(vertex_location_count);
        DecompressFrame(frame, W8_MESH_FRAME_LOCATIONS, scratch.data());
        l = scratch.data();
    }

    for (int poly = 0; poly < polygon_count; ++poly) {
        const auto origin = l[poly_vertex[poly].x].glmValue();
        const auto edge_0 = l[poly_vertex[poly].y].glmValue() - origin;
        const auto edge_1 = l[poly_vertex[poly].z].glmValue() - origin;
        pnorm[poly] = glm::cross(edge_0, edge_1);
    }

    std::vector<glm::vec3> vnorm(vertex_location_count, glm::vec3(0));
    w8_ulong* shade_index = getVertexShadeIndex(0);
    if (shade_index == 0) {
        for (int corner_poly = 0; corner_poly < polygon_count; ++corner_poly) {
            vnorm[poly_vertex[corner_poly].x] += pnorm[corner_poly];
            vnorm[poly_vertex[corner_poly].y] += pnorm[corner_poly];
            vnorm[poly_vertex[corner_poly].z] += pnorm[corner_poly];
        }
    } else {
        std::vector<glm::vec3> shaded(vertex_location_count, glm::vec3(0));
        for (int corner_poly = 0; corner_poly < polygon_count; ++corner_poly) {
            shaded[shade_index[poly_vertex[corner_poly].x]] += pnorm[corner_poly];
            shaded[shade_index[poly_vertex[corner_poly].y]] += pnorm[corner_poly];
            shaded[shade_index[poly_vertex[corner_poly].z]] += pnorm[corner_poly];
        }
        for (int i = 0; i < vertex_location_count; ++i)
            vnorm[i] = shaded[shade_index[i]];
    }

    CompressMeshNormals(vnorm, storage.compressed_vertex_normals);
    CompressMeshNormals(pnorm, storage.compressed_polygon_normals);

    if (storage.vertex_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS, storage.vertex_normals->data());
    }
    if (storage.polygon_normals) {
        DecompressFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS, storage.polygon_normals->data());
    }
}

/* Thirteen-byte forwarder onto the per-frame normal builder. */
// FUNCTION: WIZ8 0x00472100
std::span<glm::vec3> stMeshModel::GetVertexLights(bool initialize, int table)
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

// FUNCTION: WIZ8 0x004721E0
std::span<float> stMeshModel::GetVertexSunlight(bool initialize)
{
    if (vertex_sunlight.empty() && initialize) {
        vertex_sunlight.resize(vertex_location_count, 1.0f);
        vertex_lighting_ready = true;
    }
    return vertex_sunlight;
}

// FUNCTION: WIZ8 0x00473180
void stMeshModel::FinalizeVertexFrame(int frame)
{
    ComputeFrameNormals(frame);
}

/* Retail ICF folds this empty thiscall onto W8OptionsGraphicsPanel::OnDragEnd
   at 0x005AA400 (OptionsScreen.cpp). This source function has no separately
   retained retail address, so it intentionally has no FUNCTION marker. */

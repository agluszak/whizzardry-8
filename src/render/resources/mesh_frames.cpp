#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/checked_integer.h"
#include "wiz8/math.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_math.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/spatial/bounding_box.h"

class Material;

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

BoundingBox Mesh::GetFrameBounds(int frame) const
{
    if (static_cast<unsigned int>(frame) >= frames.size() || vertex_location_count == 0) {
        return {};
    }
    const auto locations = decodeFrame(frame, W8_MESH_FRAME_LOCATIONS);
    return boundingBox(std::span<const glm::vec3>(locations));
}

/* Bake the sunlight, lights, ambient and environment terms into the colours of
   the render asset's first-pass vertices. */
void Mesh::updateBakedLighting()
{
    const auto& lights = vertex_lights;
    if (!render_asset_ || lights.empty() || vertex_sunlight.empty()) return;
    std::vector<glm::vec3> baked(static_cast<std::size_t>(vertex_location_count));
    const auto* materials = getVertexMaterialTable(0, SIDE_FRONT, false);
    const bool suppress_ambient = ambient_color == glm::vec3(0);
    const glm::vec3 base_ambient =
        materials || suppress_ambient
            ? glm::vec3(0)
            : ambient_color *
                  glm::vec3(static_cast<Material*>(getMaterial(0, SIDE_FRONT))->parms.ambient);
    const auto environment = environment_offset;
    const bool add_environment = environment != glm::vec3(0);
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
        baked[vertex] = color;
    }
    for (auto& pass : render_asset_->passes) {
        if (pass.pass != 0) continue;
        auto& colors = *pass.attributes[1];
        for (std::size_t vertex = 0; vertex < pass.source_vertices.size(); ++vertex) {
            const auto& color = baked[pass.source_vertices[vertex]];
            colors.at(vertex) = vsg::vec4(color.x, color.y, color.z, 0);
        }
        colors.dirty();
        pass.draw_flags |= render::HasColors;
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

/* Textures whose names start with "blank" omit their polygons.
   A null skin selects the authored textures. */
Mesh::PolygonSelection Mesh::GetActivePolygons(Skin* skin, bool check_blanking)
{
    auto& selection = skin ? skin->active_polygons : base_polygons;
    auto& checked = skin ? skin->blanking_checked : base_blanking_checked;
    if (!checked && check_blanking) {
        std::vector<std::uint32_t> active;
        active.reserve(polygon_count);
        auto* textures = skin ? &skin->textures : getPolygonTextureTable(0, 0, false);
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
    return std::span<const std::uint32_t>(*selection);
}

void Mesh::SetMappedVertex(int vertex, short key)
{
    mapped_vertices.insert_or_assign(key, vertex);
}

std::optional<int> Mesh::FindMappedIndex(short key) const
{
    if (key < 0) return {};
    const auto entry = mapped_vertices.find(key);
    if (entry == mapped_vertices.end()) return {};
    return entry->second;
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
        if (const auto index = part->FindMappedIndex(static_cast<short>(key)))
            return MappedVertex{part, *index};
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
    assert(count > 0);
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
    frames = std::move(storage);
    flags |= W8_MESH_HAS_FRAME_STORAGE;
}

Mesh::Skin* Mesh::findSkin(std::string_view name)
{
    for (auto& skin : skins)
        if (MeshNameEquals(name, skin.name)) return &skin;
    return nullptr;
}

/* A skin owns its cloned texture assignments. Appending another skin leaves
   existing skins and their polygon selections at stable addresses. */
Mesh::Skin* Mesh::createSkin(std::string_view name, const Skin* base)
{
    if (findSkin(name)) return nullptr;
    const auto* source = base ? &base->textures : getPolygonTextureTable(0, 0, false);
    if (!source) return nullptr;
    return &skins.emplace_back(Skin{.name = std::string(name), .textures = *source});
}

bool Mesh::Skin::replaceTexture(std::string_view old_name,
                                const std::shared_ptr<Texture>& replacement)
{
    bool matched = false;
    bool changed = false;
    for (auto& texture : textures) {
        if (!texture || (texture->source_path.empty() && !texture->playback) ||
            !MeshNameEquals(texture->getName(), old_name))
            continue;
        matched = true;
        if (texture != replacement) {
            texture = replacement;
            changed = true;
        }
    }
    if (changed) {
        active_polygons.reset();
        // A replacement changes the blanking selection, not the shared mesh batches.
        blanking_checked = false;
    }
    return matched;
}

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
bool Mesh::DecompressFrame(int frame, unsigned char flags, std::span<glm::vec3> destination) const
{
    if (static_cast<unsigned int>(frame) >= frames.size()) {
        return false;
    }
    const auto& storage = frames[frame];
    const auto count = flags & (W8_MESH_FRAME_LOCATIONS | W8_MESH_FRAME_VERTEX_NORMALS)
                           ? vertex_location_count
                           : polygon_count;
    if (destination.size() < static_cast<std::size_t>(count)) return false;
    if (flags & W8_MESH_FRAME_LOCATIONS) {
        for (int index = 0; index < vertex_location_count; ++index) {
            const short* source = &storage.compressed_locations[index * 3];
            destination[index] = glm::vec3(source[0] * vertex_compression_scale,
                                           source[1] * vertex_compression_scale,
                                           source[2] * vertex_compression_scale);
        }
        return true;
    }
    if (flags & W8_MESH_FRAME_VERTEX_NORMALS) {
        DecompressMeshNormals(storage.compressed_vertex_normals.data(), vertex_location_count,
                              destination.data());
        return true;
    }
    if (flags & W8_MESH_FRAME_POLYGON_NORMALS) {
        DecompressMeshNormals(storage.compressed_polygon_normals.data(), polygon_count,
                              destination.data());
        return true;
    }
    return false;
}

std::vector<glm::vec3> Mesh::decodeFrame(unsigned int frame, unsigned char kind) const
{
    if (frame >= frames.size()) return {};
    std::vector<glm::vec3> table(kind == W8_MESH_FRAME_POLYGON_NORMALS ? polygon_count
                                                                       : vertex_location_count);
    DecompressFrame(static_cast<int>(frame), kind, table);
    return table;
}

std::optional<glm::vec3> Mesh::framePosition(unsigned int frame, int vertex) const
{
    if (frame >= frames.size() || vertex < 0 || vertex >= vertex_location_count) return {};
    const short* source = &frames[frame].compressed_locations[vertex * 3];
    return glm::vec3(source[0], source[1], source[2]) * vertex_compression_scale;
}

Mesh::FrameGeometry Mesh::frameGeometry(unsigned int frame, float interpolation) const
{
    if (frame >= frames.size()) throw std::out_of_range("Mesh frame index out of range");
    FrameGeometry geometry{decodeFrame(frame, W8_MESH_FRAME_LOCATIONS),
                           decodeFrame(frame, W8_MESH_FRAME_VERTEX_NORMALS),
                           decodeFrame(frame, W8_MESH_FRAME_POLYGON_NORMALS)};
    if (0.0f < interpolation && frame + 1 < frames.size()) {
        const auto next = decodeFrame(frame + 1, W8_MESH_FRAME_LOCATIONS);
        if (interpolation == 1.0f)
            geometry.positions = next;
        else
            for (std::size_t i = 0; i < next.size(); ++i)
                geometry.positions[i] = glm::mix(geometry.positions[i], next[i], interpolation);
    }
    return geometry;
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

static inline signed char CompressNormalByte(float value)
{
    // Invalid imported geometry must not trigger an undefined float-to-byte cast.
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<signed char>(value);
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
    const auto locations = decodeFrame(frame, W8_MESH_FRAME_LOCATIONS);
    for (int poly = 0; poly < polygon_count; ++poly)
        pnorm[poly] = meshMath::polygonNormal(locations, poly_vertex[poly]);

    const auto* shade_index = getVertexShadeIndex(0);
    const auto vnorm = meshMath::vertexNormalSums(
        std::span<const glm::ivec3>(poly_vertex, polygon_count), std::span<const glm::vec3>(pnorm),
        vertex_location_count,
        shade_index ? std::span<const std::uint32_t>(shade_index, vertex_location_count)
                    : std::span<const std::uint32_t>{});

    CompressMeshNormals(vnorm, storage.compressed_vertex_normals);
    CompressMeshNormals(pnorm, storage.compressed_polygon_normals);
}

/* Thirteen-byte forwarder onto the per-frame normal builder. */
std::span<glm::vec3> Mesh::GetVertexLights(bool initialize)
{
    if (vertex_lights.empty() && initialize) {
        vertex_lights.resize(vertex_location_count, glm::vec3(0));
        if (!vertex_sunlight.empty()) {
            vertex_lighting_ready = true;
        }
    }
    return vertex_lights;
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

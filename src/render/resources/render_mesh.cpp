#include "wiz8/render/render_mesh.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/Data.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/VertexIndexDraw.h>

#include "wiz8/checked_integer.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"

namespace render {
namespace {

vsg::vec4 toVsg(const glm::vec4& value)
{
    return {value.x, value.y, value.z, value.w};
}

vsg::vec3 toVsg(const glm::vec3& value)
{
    return {value.x, value.y, value.z};
}

// Optional per-vertex and per-polygon tables are empty until a loader creates them.
template<class T> const T* tableOrNull(const std::vector<T>& table)
{
    return table.empty() ? nullptr : table.data();
}

std::uint32_t passFlags(const Mesh& mesh, int pass, int side)
{
    std::uint32_t flags = 0;
    if (!mesh.dig[pass].empty()) flags |= HasColors;
    if (!mesh.dcg[pass].empty()) flags |= HasDiffuseMultipliers;
    if (!mesh.scg[pass].empty()) flags |= HasSpecularMultipliers;
    if (!mesh.texcoords[pass][0].empty()) flags |= HasTexcoord0;
    if (!mesh.texcoords[pass][1].empty()) flags |= HasTexcoord1;
    if (!mesh.vertex_materials[pass][side].empty()) flags |= HasVertexMaterials;
    return flags;
}

MeshAsset::Pass buildPass(Mesh& mesh, int pass, int side)
{
    const auto control = mesh.render_control;
    const auto* texcoords0 = tableOrNull(mesh.texcoords[pass][0]);
    const auto* texcoords1 = tableOrNull(mesh.texcoords[pass][1]);
    const auto* dig = tableOrNull(mesh.dig[pass]);
    const auto* dcg = tableOrNull(mesh.dcg[pass]);
    const auto* scg = tableOrNull(mesh.scg[pass]);
    const auto* poly_shaders = tableOrNull(mesh.poly_shaders[pass]);

    MeshAsset::Pass result;
    result.pass = static_cast<unsigned>(pass);
    result.side = static_cast<unsigned>(side);
    const bool authored_faces = !mesh.frames.empty();
    result.reverse_normals = side != 0 && !authored_faces;
    result.cull =
        !authored_faces && (side != 0 || !(control & (1u << Mesh::CONTROL_NO_FRONT_CULL)));
    const int material_side =
        !authored_faces && side && (control & (1u << Mesh::CONTROL_REUSE_FRONT_MATERIAL)) ? 0
                                                                                          : side;
    result.draw_flags = passFlags(mesh, pass, material_side);
    result.has_texcoord0 = texcoords0 != nullptr;

    std::map<Material*, std::uint32_t> slots;
    const auto slotOf = [&](const std::shared_ptr<Material>& material) {
        const auto [entry, inserted] =
            slots.try_emplace(material.get(), wiz8::checked_integer<std::uint32_t>(slots.size()));
        if (inserted) result.materials.push_back(material);
        return entry->second;
    };
    const auto* vertex_materials = tableOrNull(mesh.vertex_materials[pass][material_side]);
    const auto pass_material = vertex_materials ? 0u : slotOf(mesh.materials[pass][material_side]);
    const auto* uv_table = tableOrNull(mesh.poly_uv_indices[pass]);
    std::vector<std::array<glm::vec4, 5>> attributes;
    std::vector<std::uint32_t> indices;
    std::map<std::pair<int, int>, std::uint32_t> vertices;
    const auto renderVertex = [&](int vertex, int uv) {
        if (vertex < 0 || vertex >= mesh.vertex_location_count)
            throw std::invalid_argument("Invalid mesh position index");
        const bool mapped = !uv_table || uv == vertex;
        const auto [entry, inserted] =
            vertices.try_emplace(std::pair{vertex, mapped ? -1 : uv},
                                 wiz8::checked_integer<std::uint32_t>(attributes.size()));
        if (!inserted) return entry->second;
        if (uv < 0 || uv >= mesh.uv_count)
            throw std::invalid_argument("Invalid mesh texture-coordinate index");
        const auto uv0 = texcoords0 ? texcoords0[mapped ? vertex : uv] : glm::vec2(0);
        const auto uv1 = texcoords1 ? texcoords1[vertex] : glm::vec2(0);
        const auto material = vertex_materials ? slotOf(vertex_materials[vertex]) : pass_material;
        attributes.push_back({glm::vec4(uv0, uv1), dig ? glm::vec4(dig[vertex], 0) : glm::vec4(0),
                              dcg ? dcg[vertex] : glm::vec4(1), scg ? scg[vertex] : glm::vec4(1),
                              glm::vec4(static_cast<float>(material), mapped ? 1.f : 0.f, 1.f, 0)});
        result.source_vertices.push_back(static_cast<std::uint32_t>(vertex));
        return entry->second;
    };
    const auto textureAt = [&](int polygon, int layer) {
        const auto& table = mesh.poly_textures[pass][layer];
        return table.empty() ? mesh.textures[pass][layer] : table[polygon];
    };
    const auto skinsMatch = [&](std::uint32_t left, std::uint32_t right) {
        if (pass != 0) return true;
        return std::all_of(mesh.skins.begin(), mesh.skins.end(), [&](const auto& skin) {
            return skin.textures.empty() || skin.textures[left] == skin.textures[right];
        });
    };
    const auto* triangles = mesh.getPolyVertex();
    for (int polygon = 0; polygon < mesh.polygon_count; ++polygon) {
        const auto triangle = triangles[polygon];
        const auto uv = uv_table ? uv_table[polygon] : triangle;
        for (int corner = 0; corner < 3; ++corner)
            indices.push_back(renderVertex(triangle[corner], uv[corner]));
        result.polygons.push_back(static_cast<std::uint32_t>(polygon));
        const std::array textures{textureAt(polygon, 0), textureAt(polygon, 1)};
        const auto shader = poly_shaders ? poly_shaders[polygon] : mesh.shaders[pass];
        if (!result.batches.empty()) {
            auto& previous = result.batches.back();
            if (previous.textures == textures && previous.shader.value == shader.value &&
                skinsMatch(previous.first_polygon, static_cast<std::uint32_t>(polygon))) {
                previous.index_count += 3;
                continue;
            }
        }
        result.batches.push_back({textures, shader,
                                  wiz8::checked_integer<std::uint32_t>(indices.size() - 3), 3,
                                  static_cast<std::uint32_t>(polygon), !poly_shaders});
    }
    for (std::size_t attribute = 0; attribute < result.attributes.size(); ++attribute) {
        auto array = vsg::vec4Array::create(attributes.size());
        for (std::size_t vertex = 0; vertex < attributes.size(); ++vertex)
            array->at(vertex) = toVsg(attributes[vertex][attribute]);
        // Baked light contributions can change while the topology stays shared.
        if (attribute == 1) array->properties.dataVariance = vsg::DYNAMIC_DATA;
        result.attributes[attribute] = std::move(array);
    }
    if (mesh.frames.empty()) {
        const auto* positions = mesh.getVertexLoc();
        const auto* normals = mesh.getVertexNormal();
        result.positions = vsg::vec3Array::create(result.source_vertices.size());
        result.normals = vsg::vec3Array::create(result.source_vertices.size());
        for (std::size_t vertex = 0; vertex < result.source_vertices.size(); ++vertex) {
            const auto original = result.source_vertices[vertex];
            result.positions->at(vertex) = toVsg(positions[original]);
            const auto normal = normals ? normals[original] : glm::vec3(0);
            result.normals->at(vertex) = toVsg(result.reverse_normals ? -normal : normal);
        }
    }
    result.indices = vsg::uintArray::create(indices.size());
    std::ranges::copy(indices, result.indices->begin());
    return result;
}

} // namespace

std::shared_ptr<MeshAsset> makeMeshAsset(Mesh& mesh)
{
    mesh.applyPendingLighting();
    auto asset = std::make_shared<MeshAsset>();
    if (mesh.vertex_location_count <= 0 || mesh.polygon_count <= 0) return asset;
    const auto* triangles = mesh.getPolyVertex();
    asset->triangles.assign(triangles, triangles + mesh.polygon_count);
    // VSG needs the actual all-frame model bound even where the draw controls
    // mark the mesh's own bounds as authored (the sky).
    const auto bounds = mesh.getBounds();
    asset->bounds_center = bounds.sphere.center;
    asset->bounds_radius = bounds.sphere.radius;
    asset->sort_bias = mesh.sort_bias;
    const auto control = mesh.render_control;
    asset->sorted = (control & (1u << Mesh::CONTROL_SORTED_RENDERING)) != 0;
    asset->animated = !mesh.frames.empty();
    for (int side = 1; side >= 0; --side) {
        if (!(control & (1u << side))) continue;
        for (int pass = 0; pass < mesh.pass_count; ++pass) {
            asset->passes.push_back(buildPass(mesh, pass, side));
            if (!mesh.poly_uv_indices[pass].empty()) asset->offset_pass_keys = false;
        }
    }
    return asset;
}

MeshPose::MeshPose(Mesh& mesh) : asset(mesh.renderAsset())
{
    makeDraws();
}

void MeshPose::makeDraws()
{
    passes.clear();
    selected_indices_.clear();
    passes.reserve(asset->passes.size());
    selected_indices_.resize(asset->passes.size());
    for (const auto& source : asset->passes) {
        Pass pass;
        if (asset->animated) {
            pass.positions = vsg::vec3Array::create(source.source_vertices.size());
            pass.normals = vsg::vec3Array::create(source.source_vertices.size());
            pass.positions->properties.dataVariance = vsg::DYNAMIC_DATA;
            pass.normals->properties.dataVariance = vsg::DYNAMIC_DATA;
            pass.authored_normals = vsg::vec3Array::create(source.polygons.size());
        } else {
            pass.positions = source.positions;
            pass.normals = source.normals;
        }
        pass.source_polygons = source.polygons;
        vsg::DataList arrays{pass.positions, pass.normals};
        arrays.insert(arrays.end(), source.attributes.begin(), source.attributes.end());
        for (const auto& batch : source.batches) {
            auto draw = vsg::VertexIndexDraw::create();
            draw->assignArrays(arrays);
            draw->assignIndices(source.indices);
            draw->indexCount = batch.index_count;
            draw->instanceCount = 1;
            draw->firstIndex = batch.first_index;
            pass.draws.push_back(std::move(draw));
        }
        passes.push_back(std::move(pass));
    }
    sampled_frame_ = ~0u;
    selection_initialized_ = false;
}

void MeshPose::update(Mesh& mesh, unsigned frame, float interpolation, int skin,
                      bool check_blanking)
{
    // Baking may allocate its first colour table and invalidate the asset.
    // Complete that work before testing whether the topology is still shared.
    mesh.applyPendingLighting();
    if (const auto current = mesh.renderAsset(); current != asset) {
        asset = current;
        makeDraws();
    }
    if (sampled_frame_ != frame || sampled_interpolation_ != interpolation) {
        const auto count = static_cast<std::size_t>(mesh.vertex_location_count);
        if (mesh.frames.empty()) {
            if (const auto* source = mesh.getVertexLoc())
                positions.assign(source, source + count);
            else
                positions.clear();
            if (const auto* source = mesh.getVertexNormal())
                normals.assign(source, source + count);
            else
                normals.clear();
            polygon_normals.clear();
        } else {
            const auto geometry = mesh.frameGeometry(frame, interpolation);
            positions.assign(geometry.positions.begin(), geometry.positions.end());
            normals.assign(geometry.normals.begin(), geometry.normals.end());
            polygon_normals.assign(geometry.polygon_normals.begin(),
                                   geometry.polygon_normals.end());
        }
        positions.resize(count, glm::vec3(0));
        normals.resize(count, glm::vec3(0));
        for (std::size_t i = 0; asset->animated && i < passes.size(); ++i) {
            const auto& source = asset->passes[i];
            auto& pass = passes[i];
            for (std::size_t vertex = 0; vertex < source.source_vertices.size(); ++vertex) {
                const auto original = source.source_vertices[vertex];
                pass.positions->at(vertex) = toVsg(positions[original]);
                pass.normals->at(vertex) =
                    toVsg(source.reverse_normals ? -normals[original] : normals[original]);
            }
            pass.positions->dirty();
            pass.normals->dirty();
        }
        sampled_frame_ = frame;
        sampled_interpolation_ = interpolation;
    }
    selectPolygons(mesh, skin, check_blanking);
    for (auto& pass : passes)
        if (pass.authored_normals)
            for (std::size_t triangle = 0; triangle < pass.source_polygons.size(); ++triangle)
                pass.authored_normals->at(triangle) =
                    toVsg(polygon_normals[pass.source_polygons[triangle]]);
    refreshColors(mesh);
}

void MeshPose::selectPolygons(Mesh& mesh, int skin, bool check_blanking)
{
    const auto polygon_count = static_cast<std::size_t>(mesh.polygon_count);
    Mesh::PolygonSelection selection;
    if (const auto* active = mesh.getActivePolygonTable(0))
        selection = std::span<const std::uint32_t>(active, mesh.getActivePolygonCount());
    else
        selection = mesh.GetActivePolygons(skin, check_blanking);
    std::vector<std::uint32_t> selected;
    if (selection)
        selected.assign(selection->begin(), selection->end());
    else {
        selected.resize(polygon_count);
        std::iota(selected.begin(), selected.end(), 0u);
    }
    if (selection_initialized_ && selected == selected_polygons_) return;
    selection_initialized_ = true;
    selected_polygons_ = std::move(selected);
    std::vector<bool> included(polygon_count, false);
    for (const auto polygon : selected_polygons_)
        if (polygon < included.size()) included[polygon] = true;
    const bool all = std::ranges::all_of(included, [](bool value) { return value; });
    for (std::size_t i = 0; i < passes.size(); ++i) {
        const auto& source = asset->passes[i];
        auto& pass = passes[i];
        pass.source_polygons = source.polygons;
        if (!all && !selected_indices_[i]) {
            selected_indices_[i] = vsg::uintArray::create(source.indices->size());
            selected_indices_[i]->properties.dataVariance = vsg::DYNAMIC_DATA;
        }
        for (std::size_t b = 0; b < source.batches.size(); ++b) {
            const auto& batch = source.batches[b];
            auto& draw = pass.draws[b];
            draw->assignIndices(all ? source.indices : selected_indices_[i]);
            draw->indexCount = all ? batch.index_count : 0;
            if (all) continue;
            for (std::uint32_t offset = 0; offset < batch.index_count; offset += 3) {
                const auto triangle = (batch.first_index + offset) / 3;
                if (!included[source.polygons[triangle]]) continue;
                pass.source_polygons[(batch.first_index + draw->indexCount) / 3] =
                    source.polygons[triangle];
                for (std::uint32_t corner = 0; corner < 3; ++corner)
                    selected_indices_[i]->at(batch.first_index + draw->indexCount + corner) =
                        source.indices->at(batch.first_index + offset + corner);
                draw->indexCount += 3;
            }
        }
        if (!all) selected_indices_[i]->dirty();
    }
}

void MeshPose::refreshColors(const Mesh& mesh)
{
    for (auto& pass : asset->passes) {
        const auto& colors = mesh.dig[pass.pass];
        if (colors.empty()) continue;
        auto& destination = *pass.attributes[1];
        bool changed = false;
        for (std::size_t vertex = 0; vertex < pass.source_vertices.size(); ++vertex) {
            const auto value = toVsg(glm::vec4(colors[pass.source_vertices[vertex]], 0));
            if (destination.at(vertex) == value) continue;
            destination.at(vertex) = value;
            changed = true;
        }
        if (changed) destination.dirty();
    }
}

std::shared_ptr<Texture> MeshPose::texture(const Mesh& mesh, std::size_t pass, std::size_t batch,
                                           int skin, unsigned layer) const
{
    const auto& source = asset->passes.at(pass);
    const auto& draw = source.batches.at(batch);
    if (source.pass == 0 && layer == 0 && skin >= 0) {
        const auto found = std::find_if(mesh.skins.begin(), mesh.skins.end(),
                                        [skin](const auto& table) { return table.id == skin; });
        if (found != mesh.skins.end() && !found->textures.empty())
            return found->textures[draw.first_polygon];
    }
    return draw.textures.at(layer);
}

bool authoredFaceVisible(const vsg::vec3Array& normals, const vsg::vec3Array& positions,
                         const vsg::uintArray& indices, std::uint32_t first, const glm::vec3& eye,
                         bool positive_winding)
{
    const auto& vertex = positions.at(indices.at(first + 1));
    const auto& normal = normals.at(first / 3);
    const float facing = glm::dot(eye - glm::vec3(vertex.x, vertex.y, vertex.z),
                                  glm::vec3(normal.x, normal.y, normal.z));
    return positive_winding ? facing >= 0 : facing <= 0;
}

} // namespace render

std::shared_ptr<render::MeshAsset> Mesh::renderAsset()
{
    if (!render_asset_) render_asset_ = render::makeMeshAsset(*this);
    return render_asset_;
}

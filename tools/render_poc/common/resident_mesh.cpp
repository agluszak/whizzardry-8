#include "resident_mesh.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_shading.h"

namespace render_poc {
namespace {

ResidentGeometry buildGeometry(Mesh& mesh, const Mesh::TriMesh& view)
{
    ResidentGeometry geometry;
    geometry.position_count = static_cast<int>(view.vertex_count);
    geometry.frame_count = mesh.frames.empty() ? 1 : static_cast<int>(mesh.frames.size());
    geometry.has_normals = view.normals != nullptr;
    const auto count = static_cast<std::size_t>(geometry.position_count);
    const auto frames = static_cast<std::size_t>(geometry.frame_count);
    const auto total = count * frames * 2;
    geometry.height = static_cast<int>((total + kGeometryTextureWidth - 1) / kGeometryTextureWidth);
    geometry.texels.assign(static_cast<std::size_t>(geometry.height) * kGeometryTextureWidth,
                           glm::vec4(0));
    const auto store = [&](std::size_t base, const glm::vec3* values, float w) {
        if (!values) return;
        for (std::size_t vertex = 0; vertex < count; ++vertex)
            geometry.texels[base + vertex] = glm::vec4(values[vertex], w);
    };
    if (mesh.frames.empty()) {
        store(0, view.positions, 1);
        store(count * frames, view.normals, 0);
        return geometry;
    }
    // Decode each frame once with the mesh's own codec; the GPU keeps them all.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto index = static_cast<unsigned>(frame);
        store(frame * count, mesh.GetVertexLocations(index, true, 0), 1);
        store((frames + frame) * count, mesh.GetVertexNormals(index, true), 0);
    }
    geometry.has_normals = true;
    mesh.ReleaseDecompressedFrames();
    return geometry;
}

std::uint32_t passDrawFlags(const Mesh::TriMesh& view, int pass)
{
    std::uint32_t flags = 0;
    if (!view.vertex_alpha.empty()) flags |= DrawMaterial::HAS_ALPHA;
    if (view.dig[pass]) flags |= DrawMaterial::HAS_COLORS;
    if (view.dcg[pass]) flags |= DrawMaterial::HAS_DIFFUSE_MULTIPLIERS;
    if (view.scg[pass]) flags |= DrawMaterial::HAS_SPECULAR_MULTIPLIERS;
    if (view.vertex_materials[pass][0]) flags |= DrawMaterial::HAS_VERTEX_MATERIALS;
    if (view.texcoords[pass][0]) flags |= DrawMaterial::HAS_TEXCOORD0;
    if (view.texcoords[pass][1]) flags |= DrawMaterial::HAS_TEXCOORD1;
    return flags;
}

ResidentPass buildPass(const Mesh::TriMesh& view, int pass)
{
    ResidentPass result;
    result.draw_flags = passDrawFlags(view, pass);
    result.has_texcoord0 = view.texcoords[pass][0] != nullptr;

    // Material slots: the pass material, or each distinct per-vertex material.
    std::map<Material*, float> slots;
    const auto slotOf = [&](Material* material) {
        const auto [found, inserted] = slots.try_emplace(material, float(result.materials.size()));
        if (inserted) result.materials.push_back(material);
        return found->second;
    };
    const auto* vertex_materials = view.vertex_materials[pass][0];
    const float pass_material = vertex_materials ? 0 : slotOf(view.materials[pass][0].get());

    const auto* uv_table = view.poly_uv[pass];
    const auto* stage0 = view.texcoords[pass][0];
    const auto* stage1 = view.texcoords[pass][1];
    // A corner whose UV index differs from its vertex becomes a separate render
    // vertex carrying the raw table coordinate, as the native dedup path does.
    std::map<std::pair<int, int>, std::uint32_t> render_vertices;
    const auto renderVertex = [&](int vertex, int uv) {
        const bool follows = uv_table == nullptr || uv == vertex;
        const auto key = std::pair{vertex, follows ? -1 : uv};
        const auto [found, inserted] =
            render_vertices.try_emplace(key, static_cast<std::uint32_t>(result.vertices.size()));
        if (!inserted) return found->second;
        ResidentVertex out;
        const float material =
            vertex_materials ? slotOf((*vertex_materials)[vertex].get()) : pass_material;
        const float alpha = view.vertex_alpha.empty() ? 1.f : view.vertex_alpha[vertex];
        out.index_material_alpha = {float(vertex), material, follows ? 1.f : 0.f, alpha};
        const auto uv0 = stage0 ? stage0[follows ? vertex : uv] : glm::vec2(0);
        const auto uv1 = stage1 ? stage1[vertex] : glm::vec2(0);
        out.texcoords = {uv0, uv1};
        if (view.dig[pass]) out.color = glm::vec4(view.dig[pass][vertex], 0);
        if (view.dcg[pass]) out.diffuse_multiplier = view.dcg[pass][vertex];
        if (view.scg[pass]) out.specular_multiplier = view.scg[pass][vertex];
        result.vertices.push_back(out);
        return found->second;
    };

    const auto* polygon_textures = view.poly_textures[pass][0];
    const auto* polygon_shaders = view.poly_shaders[pass];
    const auto polygons = view.active_polygons;
    const auto triangle_count =
        polygons ? polygons->size() : static_cast<std::size_t>(view.polygon_count);
    for (std::size_t order = 0; order < triangle_count; ++order) {
        const auto polygon = polygons ? (*polygons)[order] : static_cast<w8_ulong>(order);
        const auto& triangle = view.poly_vertices[polygon];
        const auto uv = uv_table ? uv_table[polygon] : triangle;
        for (int corner = 0; corner < 3; ++corner)
            result.indices.push_back(renderVertex(triangle[corner], uv[corner]));
        result.polygons.push_back(static_cast<std::uint32_t>(polygon));

        auto* texture =
            polygon_textures ? (*polygon_textures)[polygon].get() : view.textures[pass][0].get();
        if (texture) texture = texture->resolve();
        const auto shader = polygon_shaders ? polygon_shaders[polygon] : view.shaders[pass];
        const auto first = static_cast<std::uint32_t>(result.indices.size() - 3);
        if (!result.batches.empty() && result.batches.back().texture == texture &&
            result.batches.back().shader.value == shader.value) {
            result.batches.back().index_count += 3;
            continue;
        }
        // Per-shader channels. The native path ORs the channels of every
        // polygon shader in its CPU batch, so mixed-shader passes could shade
        // a triangle with a neighbour's channels; batching per shader removes
        // that dependency on batch boundaries.
        const auto channels = ~shaderDisabledChannels(shader);
        result.batches.push_back({texture, shader, channels, first, 3});
    }
    return result;
}

} // namespace

ResidentMesh buildResidentMesh(Mesh& mesh)
{
    const auto view = mesh.getTriMesh();
    if (view.vertex_count <= 0 || view.polygon_count <= 0)
        throw std::invalid_argument("Cannot make an empty mesh resident");
    ResidentMesh result;
    result.geometry = buildGeometry(mesh, view);
    result.triangles.assign(view.poly_vertices, view.poly_vertices + view.polygon_count);
    for (int pass = 0; pass < view.pass_count; ++pass) {
        result.passes.push_back(buildPass(view, pass));
        if (view.poly_uv[pass]) result.offset_pass_keys = false;
    }
    result.bounds_center = view.bounds_center;
    result.bounds_radius = view.bounds_radius;
    result.sort_bias = view.sort_bias;
    result.sorted = (view.control_flags & (1u << Mesh::CONTROL_SORTED_RENDERING)) != 0;
    return result;
}

FrameSelection selectFrame(const ResidentGeometry& geometry, unsigned frame, float interpolation)
{
    const auto frames = static_cast<unsigned>(geometry.frame_count);
    if (frame >= frames) frame = frames - 1;
    // Mesh::GetVertexLocations only interpolates towards an existing next frame.
    if (interpolation > 0 && frame + 1 < frames)
        return {static_cast<int>(frame), static_cast<int>(frame + 1), interpolation};
    return {static_cast<int>(frame), -1, 0};
}

std::vector<glm::vec3> framePositions(const ResidentGeometry& geometry, unsigned frame,
                                      float interpolation)
{
    const auto selection = selectFrame(geometry, frame, interpolation);
    const auto count = static_cast<std::size_t>(geometry.position_count);
    const auto at = [&](int selected, std::size_t vertex) {
        return glm::vec3(geometry.texels[static_cast<std::size_t>(selected) * count + vertex]);
    };
    std::vector<glm::vec3> positions(count);
    for (std::size_t vertex = 0; vertex < count; ++vertex) {
        const auto current = at(selection.frame, vertex);
        if (selection.next < 0) {
            positions[vertex] = current;
        } else if (selection.interpolation == 1.0f) {
            positions[vertex] = at(selection.next, vertex);
        } else {
            positions[vertex] =
                glm::mix(current, at(selection.next, vertex), selection.interpolation);
        }
    }
    return positions;
}

} // namespace render_poc

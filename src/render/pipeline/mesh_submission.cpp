#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_culling.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/spatial/bounding_box.h"

MeshDrawPass makeMeshDrawPass(const Mesh::TriMesh& mesh, std::int32_t pass,
                              std::int32_t material_side)
{
    MeshDrawPass result;
    auto* current_record = &result.vertex;
    auto* current_pass = &result.render;
    current_record->flags = 0;
    current_record->diffuse_scale = mesh.diffuse_scale;
    current_record->emissive_override = mesh.emissive_override;
    if (!mesh.vertex_alpha.empty()) {
        current_record->flags |= DrawMaterial::HAS_ALPHA;
        current_record->alpha_source = mesh.vertex_alpha.data();
    }
    current_pass->shaders = 0;
    current_pass->texture_tables[0] = 0;
    current_pass->texture_tables[1] = 0;

    if (mesh.dig[pass] != 0) {
        current_record->color_source.colors = mesh.dig[pass];
        current_record->color_source.format = DrawMaterial::ColorSource::FORMAT_VECTOR3;
        current_record->flags |= DrawMaterial::HAS_COLORS;
    }
    if (mesh.dcg[pass] != 0) {
        current_record->spec_for_diffuse = mesh.dcg[pass];
        current_record->flags |= DrawMaterial::HAS_DIFFUSE_MULTIPLIERS;
    }
    if (mesh.scg[pass] != 0) {
        current_record->spec_for_specular = mesh.scg[pass];
        current_record->flags |= DrawMaterial::HAS_SPECULAR_MULTIPLIERS;
    }

    if (mesh.vertex_materials[pass][material_side] == 0) {
        current_record->material = mesh.materials[pass][material_side].get();
    } else {
        current_record->materials = mesh.vertex_materials[pass][material_side];
        current_record->flags |= DrawMaterial::HAS_VERTEX_MATERIALS;
    }

    if (mesh.poly_uv[pass] != 0) {
        current_pass->poly_uv = mesh.poly_uv[pass];
    }

    if (mesh.poly_shaders[pass] == 0) {
        current_pass->shader = mesh.shaders[pass];
    } else {
        current_pass->shaders = mesh.poly_shaders[pass];
    }

    if (mesh.texcoords[pass][0] != 0) {
        current_record->st_source[0] = mesh.texcoords[pass][0];
        current_record->flags |= DrawMaterial::HAS_TEXCOORD0;
    }
    if (mesh.texcoords[pass][1] != 0) {
        current_record->flags |= DrawMaterial::HAS_TEXCOORD1;
        current_record->st_source[1] = mesh.texcoords[pass][1];
    }

    for (std::int32_t layer = 0; layer < 2; ++layer) {
        if (mesh.poly_textures[pass][layer] == 0) {
            Texture* texture = mesh.textures[pass][layer].get();
            current_pass->textures[layer] = texture;
        } else {
            current_pass->texture_tables[layer] = mesh.poly_textures[pass][layer];
        }
    }

    return result;
}

void GpuRenderer::submitMesh(const Mesh::TriMesh& mesh, const MeshState& state,
                             std::int32_t material_side)
{
    if (mesh.vertex_count < 0 || mesh.polygon_count < 0 || mesh.pass_count < 0 ||
        mesh.pass_count > Mesh::MAX_PASSES)
        throw std::invalid_argument("Invalid mesh submission counts");
    if (!mesh.vertex_count || !mesh.polygon_count || !mesh.pass_count) return;
    if (!mesh.vertex_alpha.empty() &&
        mesh.vertex_alpha.size() < static_cast<std::size_t>(mesh.vertex_count))
        throw std::invalid_argument("Incomplete mesh alpha channel");
    const auto triangle_count = static_cast<std::uint32_t>(mesh.polygon_count);
    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertex_count);
    const auto slot_count = static_cast<std::uint32_t>(mesh.pass_count);
    const auto active_triangles = mesh.active_polygons;
    const auto* triangles = mesh.poly_vertices;
    const auto* positions = mesh.positions;
    const auto* vertex_extras = mesh.normals;
    const auto* projected_vertices = mesh.poly_equations;
    const auto sort_bias = mesh.sort_bias;
    std::array<DrawMaterial, Mesh::MAX_PASSES> records;
    std::array<MeshPass, Mesh::MAX_PASSES> passes;
    std::array<ShadedVertexArrays, Mesh::MAX_PASSES> output_arrays{};
    for (std::uint32_t pass = 0; pass < slot_count; ++pass) {
        const auto draw = makeMeshDrawPass(mesh, pass, material_side);
        records[pass] = draw.vertex;
        passes[pass] = draw.render;
    }

    if (triangle_count == 0 || (active_triangles && active_triangles->empty())) {
        return;
    }

    glm::vec3 bounds_center = mesh.bounds_center;
    float bounds_radius = mesh.bounds_radius;
    if ((mesh.control_flags & (1u << Mesh::CONTROL_SKIP_AUTO_SPHERE)) != 0) {
        auto minimum = mesh.bounds_minimum, maximum = mesh.bounds_maximum;
        if ((mesh.control_flags & (1u << Mesh::CONTROL_SKIP_AUTO_BOX)) != 0) {
            const auto box = boundingBox(std::span<const glm::vec3>(positions, vertex_count));
            minimum = box.minimum;
            maximum = box.maximum;
        }
        bounds_center = (minimum + maximum) * .5f;
        bounds_radius = glm::distance(bounds_center, minimum) * 1.001f;
    }
    const auto& transform = state.transform;
    const auto eye_bounds = transform.eyeSpaceBounds({bounds_center, bounds_radius});
    const auto& model_view = transform.model_view;
    const auto inverse_model_view = transform.inverseModelView();
    const auto clip_planes = transform.clipPlanes();
    const auto project_clip_near = transform.projectClipNear();
    auto normal_matrix = transform.normalMatrix();
    if (state.reverse_normals) normal_matrix = glm::mat4(-glm::mat3(normal_matrix));

    const auto cull_mode = state.cull;
    const auto winding = state.winding;

    meshCulling::Input culler_input(model_view, inverse_model_view);
    if (cull_mode == FaceCull::None) {
        culler_input.cull_mode = 2;
    } else if (cull_mode == FaceCull::Front) {
        culler_input.cull_mode = winding == Winding::Standard;
    } else {
        culler_input.cull_mode = winding != Winding::Standard;
    }

    culler_input.vertices = {positions, vertex_count};
    culler_input.clip_planes = clip_planes.planes;

    {
        const auto clip = meshCulling::getClipMask(eye_bounds.center, eye_bounds.radius,
                                                   clip_planes.planes, clip_planes.mask);
        const bool retain_clip_mask =
            clip.mask != 0 && ((clip.mask & 0xffffffc0UL) != 0 ||
                               (slot_count * triangle_count > 45 && clip.depth > 0.23f));
        culler_input.clip_mask = retain_clip_mask ? clip.mask : 0;
    }

    const auto total =
        active_triangles ? static_cast<std::uint32_t>(active_triangles->size()) : triangle_count;
    std::uint32_t batch_limit = total;
    {
        double ratio = static_cast<double>(vertex_count) / triangle_count;
        if (ratio > 3.0f) {
            ratio = 3.0f;
        }
        batch_limit = static_cast<std::uint32_t>(1300.0f / (slot_count * ratio));
        if (cull_mode == FaceCull::None) {
            batch_limit >>= 1;
        }
        if (batch_limit > total) {
            batch_limit = total;
        }
    }

    std::uint32_t processed = 0;
    while (processed < total) {
        std::uint32_t batch_count = total - processed;
        if (batch_count > batch_limit) {
            batch_count = batch_limit;
        }

        if (!active_triangles) {
            culler_input.active_triangles.reset();
            culler_input.polygon_planes =
                projected_vertices
                    ? std::span<const glm::vec4>(projected_vertices + processed, batch_count)
                    : std::span<const glm::vec4>{};
            culler_input.triangles = {triangles + processed, batch_count};
        } else {
            culler_input.active_triangles = active_triangles->subspan(processed, batch_count);
            culler_input.polygon_planes =
                projected_vertices ? std::span<const glm::vec4>(projected_vertices, triangle_count)
                                   : std::span<const glm::vec4>{};
            culler_input.triangles = {triangles, triangle_count};
        }

        auto culler_output = meshCulling::cull(culler_input);
        if (!culler_output.indices.empty()) {

            auto* renderer = &this->batchRenderer(state.sorted);

            {
                ShadedVertexArrays* vertex_arrays = output_arrays.data();
                vertex_arrays[0] =
                    renderer->allocVertexArray(slot_count * culler_output.vertexCount());
                for (std::uint32_t slot = 1; slot < slot_count; ++slot) {
                    const auto offset = renderer->first_vertex + slot * culler_output.vertexCount();
                    vertex_arrays[slot] = renderer->vertices.bind(offset);
                }

                const auto& scene = state.scene;
                VertexShadingInput pipe_input(model_view, normal_matrix);
                pipe_input.active_vertices = culler_output.avt;
                pipe_input.direct_vertex_indices = false;
                pipe_input.positions = {positions, vertex_count};
                if (vertex_extras) pipe_input.normals = {vertex_extras, vertex_count};
                pipe_input.eye_bounds = eye_bounds;
                pipe_input.vertex_arrays = std::span{output_arrays}.first(slot_count);
                pipe_input.exclusion_mask = scene.exclusion_mask;
                pipe_input.ambient_light = scene.ambient_light;
                pipe_input.records = std::span{records}.first(slot_count);
                pipe_input.lights = scene.lights;
                pipe_input.fogs = scene.fogs;
                pipe_input.frame_time = scene.frame.time;
                pipe_input.environment_minimum = scene.depth_cue.minimum;
                pipe_input.environment_maximum = scene.depth_cue.maximum;
                pipe_input.environment_scale = scene.depth_cue.near_scale;
                pipe_input.environment_inverse_scale = scene.depth_cue.far_scale;

                if (!active_triangles && processed != 0) {
                    for (auto& index : culler_output.indices) index += processed;
                }

                for (std::uint32_t pass_index = 0; pass_index < slot_count; ++pass_index) {
                    passes[pass_index].texcoords = records[pass_index].st_source[0];

                    std::uint32_t disable_mask;
                    if (passes[pass_index].shaders != 0) {
                        disable_mask = shaderDisabledChannels(passes[pass_index].shaders,
                                                              culler_output.indices.data(),
                                                              culler_output.triangleCount());
                    } else {
                        disable_mask = shaderDisabledChannels(passes[pass_index].shader);
                    }
                    records[pass_index].channels = disable_mask;
                }

                shadeVertices(pipe_input);

                GpuRenderer::Renderer::TriInput render_input;
                render_input.record_count = slot_count;
                render_input.vertex_count = culler_output.vertexCount();
                render_input.indices = culler_output.indices;
                render_input.triangles = {triangles, triangle_count};
                render_input.vertices = culler_output.vertex_remap;
                render_input.passes = &passes[0];
                render_input.direct_vertex_indices = false;
                render_input.project_clip_near = &project_clip_near;
                render_input.sort_bias = sort_bias;
                // Sorted batches draw when the scene pass flushes them, after any
                // caller's depth bias has ended.
                render_input.polygon_offset = state.sorted ? 0 : state.polygon_offset;
                render_input.fog_color = scene.fog_color;
                render_input.texture_reduction = scene.frame.texture_reduction;
                render_input.picks = scene.picks;
                render_input.pick_owner = scene.pick_owner;
                renderer->render(render_input);
            }
            if (renderer->isBatchFull()) renderer->submit();
        }

        processed += batch_limit;
    }
}

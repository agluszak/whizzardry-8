#include "lighting_check.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <utility>

#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/vertex_shading.h"

namespace render_poc {
namespace {

void shadeReference(ProbeCase& probe, std::span<Light* const> lights,
                    std::span<FogParams* const> fogs, const ShadingEnvironment& environment)
{
    const auto& instance = *probe.source.instance;
    const auto& resident = *probe.source.resident;
    const auto& pass = resident.passes[probe.pass];
    auto view = instance.getModel()->getTriMesh();
    // MeshInstance::RenderMeshes applies the instance overrides to the TriMesh.
    if (instance.diffuse_scale_enabled) view.diffuse_scale = instance.diffuse_scale;
    if (instance.emissive_override_enabled) view.emissive_override = instance.emissive_override;

    const auto positions =
        framePositions(resident.geometry, instance.frame_index, instance.frame_interpolation);
    std::vector<glm::vec3> normals;
    if (resident.geometry.has_normals) {
        const auto count = static_cast<std::size_t>(resident.geometry.position_count);
        const auto frames =
            selectFrame(resident.geometry, instance.frame_index, instance.frame_interpolation);
        const auto base = (static_cast<std::size_t>(resident.geometry.frame_count) +
                           static_cast<std::size_t>(frames.frame)) *
                          count;
        for (std::size_t vertex = 0; vertex < count; ++vertex)
            normals.push_back(glm::vec3(resident.geometry.texels[base + vertex]));
    }

    // One shaded vertex per render vertex, addressed by its position index.
    std::vector<w8_ulong> active;
    for (const auto& vertex : pass.vertices)
        active.push_back(static_cast<w8_ulong>(vertex.index_material_alpha.x));
    const auto count = active.size();
    std::vector<glm::vec4> eye(count), diffuse(count, glm::vec4(1)), specular(count, glm::vec4(0));
    std::vector<glm::vec2> st0(count), st1(count);
    std::vector<float> q0(count), q1(count);
    std::vector<unsigned char> attributes(count);
    ShadedVertexArrays arrays{eye.data(), diffuse.data(), specular.data(), st0.data(),
                              st1.data(), q0.data(),      q1.data(),       attributes.data()};

    auto record = makeMeshDrawPass(view, probe.pass, Mesh::SIDE_FRONT).vertex;
    DrawShader shader;
    shader.value = probe.shader;
    record.channels = shaderDisabledChannels(shader);
    const auto& model_view = probe.source.model_view;
    glm::mat3 basis(model_view);
    const glm::vec3 lengths(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
    for (int column = 0; column < 3; ++column) basis[column] /= lengths[column];
    const glm::mat4 normal_matrix(glm::transpose(glm::inverse(basis)));
    VertexShadingInput input(model_view, normal_matrix);
    input.active_vertices = active;
    input.positions = positions;
    input.normals = normals;
    input.eye_bounds = {glm::vec3(model_view * glm::vec4(resident.bounds_center, 1)),
                        resident.bounds_radius * std::max({lengths.x, lengths.y, lengths.z})};
    input.vertex_arrays = std::span(&arrays, 1);
    input.exclusion_mask = instance.getExclusionMask();
    input.ambient_light = instanceAmbient(instance, environment.ambient_light);
    input.records = std::span(&record, 1);
    input.lights = lights;
    input.fogs = fogs;
    input.frame_time = environment.frame_time;
    input.environment_minimum = environment.environment_minimum;
    input.environment_maximum = environment.environment_maximum;
    input.environment_scale = environment.environment_scale;
    input.environment_inverse_scale = environment.environment_inverse_scale;
    shadeVertices(input);

    // Unwritten streams take the defaults the native GPU backend fetches.
    for (std::size_t vertex = 0; vertex < count; ++vertex) {
        if (!(attributes[vertex] & ShadedVertexArrays::ATTRIBUTE_DIFFUSE))
            diffuse[vertex] = glm::vec4(1);
        if (!(attributes[vertex] & (ShadedVertexArrays::ATTRIBUTE_SPECULAR |
                                    ShadedVertexArrays::ATTRIBUTE_SPECULAR_ALPHA)))
            specular[vertex] = glm::vec4(0);
    }
    probe.expected_diffuse = std::move(diffuse);
    probe.expected_specular = std::move(specular);
}

} // namespace

ProbePlan planProbes(std::span<const ProbeSource> sources, std::span<Light* const> lights,
                     std::span<FogParams* const> fogs, const ShadingEnvironment& environment)
{
    ProbePlan plan;
    std::uint32_t slots = 0;
    for (const auto& source : sources) {
        const auto& passes = source.resident->passes;
        for (std::uint32_t pass = 0; pass < passes.size(); ++pass) {
            std::set<std::uint32_t> shaders;
            for (std::uint32_t batch = 0; batch < passes[pass].batches.size(); ++batch) {
                const auto shader =
                    effectiveShader(*source.instance, passes[pass].batches[batch].shader.value);
                if (!shaders.insert(shader).second) continue;
                ProbeCase probe{.source = source,
                                .pass = pass,
                                .batch = batch,
                                .shader = shader,
                                .first_slot = slots};
                shadeReference(probe, lights, fogs, environment);
                slots += static_cast<std::uint32_t>(passes[pass].vertices.size());
                plan.cases.push_back(std::move(probe));
            }
        }
    }
    plan.height = 2 * static_cast<int>((slots + plan.width - 1) / plan.width);
    return plan;
}

glm::vec4 probeSelector(const ProbePlan& plan, const ProbeCase& probe, int mode)
{
    return {float(mode), float(probe.first_slot), float(plan.width), float(plan.height)};
}

ProbeReport compareProbes(const ProbePlan& plan, std::span<const std::uint8_t> rgba)
{
    ProbeReport report;
    const auto half = plan.height / 2;
    for (const auto& probe : plan.cases) {
        for (std::size_t vertex = 0; vertex < probe.expected_diffuse.size(); ++vertex) {
            const auto slot = probe.first_slot + vertex;
            const auto x = slot % plan.width;
            const auto y = slot / plan.width;
            const std::pair<const glm::vec4&, std::size_t> values[] = {
                {probe.expected_diffuse[vertex], y}, {probe.expected_specular[vertex], y + half}};
            for (const auto& [expected, row] : values) {
                const auto* pixel = &rgba[(row * plan.width + x) * 4];
                float error = 0;
                for (int channel = 0; channel < 4; ++channel) {
                    const float reference = std::clamp(expected[channel], 0.f, 1.f) * 255.f;
                    error = std::max(error, std::abs(reference - float(pixel[channel])));
                }
                report.max_error = std::max(report.max_error, error);
                // Allow 8-bit rounding plus float evaluation differences.
                if (error > 1.5f) ++report.mismatches;
            }
            ++report.vertices;
        }
    }
    report.summary =
        std::format("{} probe cases, {} vertices, max error {:.2f}/255, {} mismatched values",
                    plan.cases.size(), report.vertices, report.max_error, report.mismatches);
    return report;
}

} // namespace render_poc

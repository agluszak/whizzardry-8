#include "lighting_check.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <utility>

#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/vertex_shading.h"

namespace render_poc {
namespace {

void shadeReference(DemoScene& scene, const FramePlan& frame, ProbeCase& probe)
{
    const auto& instance = *probe.draw->instance;
    const auto& resident = instance.resident;
    const auto& pass = resident.passes[probe.pass];
    const auto& batch = pass.batches[probe.batch];
    const auto view = instance.mesh->getTriMesh();

    const auto positions =
        framePositions(resident.geometry, instance.frame, instance.interpolation);
    std::vector<glm::vec3> normals;
    if (resident.geometry.has_normals) {
        const auto count = static_cast<std::size_t>(resident.geometry.position_count);
        const auto frames = selectFrame(resident.geometry, instance.frame, instance.interpolation);
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
    record.channels = ~batch.final_channels;
    const glm::mat4 normal_matrix(probe.draw->normal_matrix);
    VertexShadingInput input(probe.draw->model_view, normal_matrix);
    input.active_vertices = active;
    input.positions = positions;
    input.normals = normals;
    const glm::mat3 basis(probe.draw->model_view);
    input.eye_bounds = {
        glm::vec3(probe.draw->model_view * glm::vec4(resident.bounds_center, 1)),
        resident.bounds_radius *
            std::max({glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])})};
    input.vertex_arrays = std::span(&arrays, 1);
    input.exclusion_mask = instance.exclusion_mask;
    input.ambient_light = scene.environment.ambient_light;
    input.records = std::span(&record, 1);
    std::vector<Light*> lights;
    for (auto& light : scene.lights) lights.push_back(light.get());
    std::vector<FogParams*> fogs;
    for (auto& fog : scene.fogs) fogs.push_back(fog.get());
    input.lights = lights;
    input.fogs = fogs;
    input.frame_time = scene.environment.frame_time;
    input.environment_minimum = scene.environment.environment_minimum;
    input.environment_maximum = scene.environment.environment_maximum;
    input.environment_scale = scene.environment.environment_scale;
    input.environment_inverse_scale = scene.environment.environment_inverse_scale;
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

ProbePlan planProbes(DemoScene& scene, const FramePlan& frame)
{
    ProbePlan plan;
    std::uint32_t slots = 0;
    const auto addDraw = [&](const PlannedDraw& draw) {
        const auto& passes = draw.instance->resident.passes;
        for (std::uint32_t pass = 0; pass < passes.size(); ++pass) {
            std::set<std::uint32_t> shaders;
            for (std::uint32_t batch = 0; batch < passes[pass].batches.size(); ++batch) {
                if (!shaders.insert(passes[pass].batches[batch].shader.value).second) continue;
                ProbeCase probe{.draw = &draw, .pass = pass, .batch = batch, .first_slot = slots};
                shadeReference(scene, frame, probe);
                slots += static_cast<std::uint32_t>(passes[pass].vertices.size());
                plan.cases.push_back(std::move(probe));
            }
        }
    };
    for (const auto& draw : frame.opaque) addDraw(draw);
    for (const auto& draw : frame.sorted) addDraw(draw);
    plan.height = 2 * static_cast<int>((slots + plan.width - 1) / plan.width);
    return plan;
}

DrawBlock probeDrawBlock(const ProbePlan& plan, const ProbeCase& probe, int mode)
{
    auto block = probe.draw->draw_blocks[probe.pass][probe.batch];
    block[5] = {float(mode), float(probe.first_slot), float(plan.width), float(plan.height)};
    return block;
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

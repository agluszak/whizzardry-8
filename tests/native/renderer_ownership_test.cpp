#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "catch2/catch_message.hpp"
#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/fog.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/render/vertex_stream.h"
#include "wiz8/render/world_scene.h"

namespace {
// Exercise real material, light and fog output across the 64-vertex batch boundary.
bool coverageFinalization()
{
    constexpr unsigned count = 65;
    std::array<glm::vec3, count> positions{};
    std::array<w8_ulong, count> indices;
    std::array<glm::vec4, count> eye, diffuse, colors;
    std::array<float, count> alpha;
    std::array<unsigned char, count> attributes{};
    const std::array values{-2.f, 0.f, .25f, 1.f, 2.f};
    for (unsigned i = 0; i < count; ++i) {
        indices[i] = i;
        colors[i] = glm::vec4(.5f);
        alpha[i] = values[i % values.size()];
    }
    Material material;
    material.setEmissive(glm::vec4(0));
    DrawMaterial record{};
    record.material = &material;
    record.flags = DrawMaterial::HAS_COLORS | DrawMaterial::HAS_ALPHA;
    record.color_source.colors = colors.data();
    record.alpha_source = alpha.data();
    ShadedVertexArrays arrays{};
    arrays.eye_locations = eye.data();
    arrays.diffuse = diffuse.data();
    arrays.attributes = attributes.data();
    const glm::mat4 identity(1);
    VertexShadingInput input(identity, identity);
    input.positions = positions;
    input.active_vertices = indices;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};

    for (bool color : {false, true})
        for (float scale : {-.5f, .5f, 2.f}) {
            material.setOpacity(scale);
            record.channels = ~((1u << VertexChannels::CHANNEL_ALPHA) |
                                (color ? 1u << VertexChannels::CHANNEL_DIFFUSE : 0));
            shadeVertices(input);
            for (unsigned i = 0; i < count; ++i) {
                const float expected = color ? std::clamp(scale * alpha[i], 0.f, 1.f)
                                             : scale * std::clamp(alpha[i], 0.f, 1.f);
                CAPTURE(color, scale, i);
                REQUIRE(diffuse[i].w == expected);
            }
        }
    return true;
}

bool surfaceLighting()
{
    constexpr unsigned count = 65;
    std::array<glm::vec3, count> positions, normals;
    std::array<w8_ulong, count> indices;
    std::array<glm::vec4, count> eye, diffuse, specular;
    std::array<unsigned char, count> attributes{};
    for (unsigned i = 0; i < count; ++i) {
        positions[i] = {0, 0, -10};
        normals[i] = {0, 0, 1};
        indices[i] = i;
    }
    ShadedVertexArrays arrays{};
    arrays.eye_locations = eye.data();
    arrays.diffuse = diffuse.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    Material material;
    material.setDiffuse(glm::vec4(.5f));
    material.setSpecular(glm::vec4(.25f));
    material.setEmissive(glm::vec4(.125f));
    DrawMaterial record{};
    record.material = &material;
    record.channels =
        ~((1u << VertexChannels::CHANNEL_DIFFUSE) | (1u << VertexChannels::CHANNEL_LIGHT_DIFFUSE) |
          (1u << VertexChannels::CHANNEL_SPECULAR));
    const glm::mat4 identity(1);
    VertexShadingInput input(identity, identity);
    input.positions = positions;
    input.normals = normals;
    input.active_vertices = indices;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    Light light;
    Light* borrowed = &light;
    input.lights = {&borrowed, 1};
    light.channel_mask =
        (1u << VertexChannels::CHANNEL_LIGHT_DIFFUSE) | (1u << VertexChannels::CHANNEL_SPECULAR);
    light.scaled_diffuse = glm::vec4(.5f);
    light.scaled_specular = glm::vec4(.25f);
    for (bool directional : {false, true})
        for (unsigned attenuation : {0u, 1u, 2u, 3u, 4u}) {
            light.derived_flags = directional        ? Light::DERIVED_DIRECTIONAL
                                  : attenuation == 1 ? Light::DERIVED_OPENGL_ATTENUATION |
                                                           Light::DERIVED_CONSTANT_ATTENUATION
                                                     : 0;
            light.eye_location = directional ? glm::vec3(0, 0, 1) : glm::vec3(0);
            light.attenuation_model = attenuation == 0   ? Light::ATTENUATION_NONE
                                      : attenuation == 1 ? Light::ATTENUATION_OPENGL
                                                         : Light::ATTENUATION_3DSTUDIO_MAX;
            light.enable_flags =
                (attenuation == 2 || attenuation == 4 ? 1u << Light::ENABLE_RANGE_NEAR : 0) |
                (attenuation == 3 || attenuation == 4 ? 1u << Light::ENABLE_RANGE_FAR : 0);
            light.scaled_near_start = 0;
            light.scaled_far_end = 20;
            light.near_attenuation = light.far_attenuation = .05f;
            light.opengl_attenuation = {2, 0, 0};

            shadeVertices(input);
            const float scale = directional || attenuation == 0 ? 1.f
                                : attenuation == 4              ? .25f
                                                                : .5f;
            for (unsigned i = 0; i < count; ++i) {
                CAPTURE(directional, attenuation, i);
                REQUIRE(diffuse[i].x == .125f + .25f * scale);
                REQUIRE(specular[i].x == .0625f * scale);
            }
        }
    // Reusing the workspace must not retain lights from a previous submission.
    input.lights = {};

    shadeVertices(input);
    for (const auto& value : diffuse) REQUIRE(value.x == .125f);
    return true;
}

bool distanceProfiles()
{
    constexpr unsigned count = 65;
    std::array<glm::vec3, count> positions;
    std::array<w8_ulong, count> indices;
    std::array<glm::vec4, count> eye, specular;
    std::array<unsigned char, count> attributes{};
    for (unsigned i = 0; i < count; ++i) {
        positions[i] = {0, 0, float(i + 1)};
        indices[i] = i;
    }
    ShadedVertexArrays arrays{};
    arrays.eye_locations = eye.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    Material material;
    DrawMaterial record{};
    record.material = &material;
    record.channels = ~(1u << VertexChannels::CHANNEL_FOG);
    const glm::mat4 identity(1);
    VertexShadingInput input(identity, identity);
    input.positions = positions;
    input.active_vertices = indices;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.eye_bounds = {glm::vec3(0, 0, 33), 33};
    FogParams fog;
    FogParams* borrowed = &fog;
    input.fogs = {&borrowed, 1};
    for (auto range : {std::pair{0.f, 100.f}, std::pair{17.f, 43.f}, std::pair{32.f, 32.f}}) {
        fog.setRange(range.first, range.second);
        for (float density : {0.f, .5f, 1.f}) {
            fog.setDensity(density);

            shadeVertices(input);
            for (unsigned i = 0; i < count; ++i) {
                const float distance = float(i + 1);
                const float normalized =
                    range.first == range.second
                        ? (distance <= range.first ? 0.f : 1.f)
                        : std::clamp((distance - range.first) / (range.second - range.first), 0.f,
                                     1.f);
                REQUIRE(std::abs(specular[i].w - density * normalized) < 1e-6f);
            }
        }
    }
    return true;
}

bool materialChannelOperations()
{
    constexpr unsigned count = 65;
    std::array<glm::vec3, count> positions;
    std::array<w8_ulong, count> indices;
    std::array<glm::vec4, count> eye, diffuse, specular;
    std::array<unsigned char, count> attributes{};
    for (unsigned i = 0; i < count; ++i) {
        positions[i] = {0, 0, float(i + 1)};
        indices[i] = i;
    }
    ShadedVertexArrays arrays{};
    arrays.eye_locations = eye.data();
    arrays.diffuse = diffuse.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    Material material;
    material.setEmissive(glm::vec4(.25f));
    material.setSpecular(glm::vec4(1));
    material.setOpacity(.8f);
    DrawMaterial record{};
    record.material = &material;
    record.channels =
        ~((1u << VertexChannels::CHANNEL_DIFFUSE) | (1u << VertexChannels::CHANNEL_SPECULAR) |
          (1u << VertexChannels::CHANNEL_ALPHA) | (1u << VertexChannels::CHANNEL_LIGHT_DIFFUSE) |
          (1u << VertexChannels::CHANNEL_LIGHT_AMBIENT));
    const glm::mat4 identity(1);
    VertexShadingInput input(identity, identity);
    input.positions = positions;
    input.active_vertices = indices;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.environment_maximum = 100;
    input.environment_inverse_scale = 1;

    for (unsigned operations : {0u, 1u, 2u, 4u, 8u, 16u, 9u, 18u}) {
        material.operations = operations;
        shadeVertices(input);
        float diffuse_value = .25f, specular_value = 0;
        if (operations & (1u << Material::OPER_DIFFUSE_SPECULAR))
            std::swap(diffuse_value, specular_value);
        else if (operations & (1u << Material::OPER_DIFFUSE))
            specular_value = diffuse_value;
        else if (operations & (1u << Material::OPER_SPECULAR))
            diffuse_value = specular_value;
        for (unsigned i = 0; i < count; ++i) {
            const float depth = 1 - float(i + 1) / 100;
            const float color_scale = operations & (1u << Material::OPER_DEPTH_CUE) ? depth : 1;
            const float alpha_scale = operations & (1u << Material::OPER_ALPHA) ? depth : 1;
            CAPTURE(operations, i);
            REQUIRE(std::abs(diffuse[i].x - diffuse_value * color_scale) < 1e-6f);
            REQUIRE(std::abs(specular[i].x - specular_value * color_scale) < 1e-6f);
            REQUIRE(std::abs(diffuse[i].w - .8f * alpha_scale) < 1e-6f);
        }
    }
    return true;
}

bool rendererOwnership()
{
    // These checks exercise CPU ownership without a window or GPU context.
    GpuRenderer gerd;
    auto* immediate = &gerd.batchRenderer(false);
    immediate->submit();
    REQUIRE(immediate->getStatistics().device_calls == 1);
    auto* renderer = &gerd.batchRenderer(true);
    REQUIRE(renderer != immediate);
    renderer->submit();
    REQUIRE(renderer->getStatistics().device_calls == 1);
    gerd.resetStatistics();
    REQUIRE(immediate->getStatistics().device_calls == 0);
    REQUIRE(renderer->getStatistics().device_calls == 0);
    REQUIRE(&gerd.batchRenderer(false) == immediate);

    renderer->indices.alloc(2);
    renderer->vertices.alloc(3);
    const auto index_capacity = renderer->indices.triangles.capacity();
    const auto vertex_capacity = renderer->vertices.positions.capacity();
    renderer->reset(0);
    REQUIRE((renderer->indices.count == 0 && renderer->vertices.count == 0));
    REQUIRE(renderer->indices.triangles.capacity() == index_capacity);
    REQUIRE(renderer->vertices.positions.capacity() == vertex_capacity);
    renderer->reset(1); // real eviction remains distinct from ordinary reset
    REQUIRE(renderer->indices.triangles.capacity() == 0);
    REQUIRE(renderer->vertices.positions.capacity() == 0);
    return true;
}

bool rendererTextureStages()
{
    GpuRenderer gerd;
    for (unsigned stages : {1u, 2u}) {
        GpuRenderer::Renderer renderer({&gerd, 0, 256, stages});
        auto arrays = renderer.allocVertexArray(8);
        for (unsigned stage = 0; stage < 2; ++stage)
            for (unsigned i = 0; i < 8; ++i) {
                renderer.vertices.st[stage][i] = glm::vec2(10 * stage + i, 20 + i);
                renderer.vertices.q[stage][i] = 0.25f * (stage + i + 1);
            }
        for (unsigned count : {1u, 3u, 4u, 5u, 8u}) {
            for (unsigned lane = 0; lane < count; ++lane) {
                for (unsigned attributes = 0; attributes < 128; ++attributes) {
                    std::fill(renderer.vertices.attributes.begin(),
                              renderer.vertices.attributes.end(), 0);
                    renderer.vertices.attributes[lane] = attributes;
                    const auto mask = renderer.vertexArrays(arrays, count).mask;
                    for (unsigned stage = 0; stage < 2; ++stage) {
                        const bool enabled =
                            stage < stages &&
                            (attributes & (ShadedVertexArrays::ATTRIBUTE_ST0 << stage));
                        REQUIRE(bool(mask & (1u << (vertexStream::VERTEX_ARRAY_TEXCOORD0 +
                                                    stage))) == enabled);
                    }
                }
            }
        }
    }
    return true;
}

bool rendererCornerRemapping()
{
    // All combinations of reused/new corners, mixed UV and flat passes, and
    // a nonzero batch base exercise allocation order and copied vertex streams.
    for (unsigned changed = 0; changed < 8; ++changed) {
        for (int sorted : {0, 1}) {
            GpuRenderer::Renderer renderer({nullptr, sorted, 256, 2});
            renderer.vertices.alloc(5);
            renderer.allocVertexArray(6);
            for (unsigned i = 5; i < 11; ++i) {
                renderer.vertices.positions[i] = glm::vec4(float(i), 2, 3, 1);
                renderer.vertices.diffuse[i] = glm::vec4(float(i) / 20);
                renderer.vertices.specular[i] = glm::vec4(float(i) / 30);
                renderer.vertices.st[1][i] = glm::vec2(float(i), 4);
                renderer.vertices.q[1][i] = float(i) / 10;
                renderer.vertices.attributes[i] = i;
            }
            std::array<w8_ulong, 1> indices{0};
            std::array<w8_ulong, 3> vertices{2, 0, 1};
            std::array<glm::ivec3, 1> triangles{{{0, 1, 2}}};
            std::array<int, 3> uv;
            for (unsigned corner = 0; corner < 3; ++corner)
                uv[corner] = corner + ((changed & (1u << corner)) ? 3 : 0);
            std::array<glm::ivec3, 1> poly_uv{{{uv[0], uv[1], uv[2]}}};
            std::array<glm::vec2, 6> texcoords;
            for (unsigned i = 0; i < texcoords.size(); ++i) texcoords[i] = glm::vec2(i, i + 10);
            std::array<MeshPass, 2> passes{};
            passes[0].poly_uv = poly_uv.data();
            passes[0].texcoords = texcoords.data();
            GpuRenderer::Renderer::TriInput input{};
            input.record_count = 2;
            input.vertex_count = 3;
            input.indices = indices;
            input.vertices = vertices;
            input.triangles = triangles;
            input.passes = passes.data();
            renderer.expandTriangles(input, sorted);
            const auto& result = renderer.indices.triangles[0];
            const std::array corners{result.x, result.y, result.z};
            int next = 11;
            for (unsigned corner = 0; corner < 3; ++corner) {
                const int source = 5 + static_cast<int>(vertices[corner]);
                if ((changed & (1u << corner)) == 0) {
                    REQUIRE(corners[corner] == source);
                    continue;
                }
                REQUIRE(corners[corner] == next);
                REQUIRE(renderer.vertices.positions[next] == renderer.vertices.positions[source]);
                REQUIRE(renderer.vertices.diffuse[next] == renderer.vertices.diffuse[source]);
                REQUIRE(renderer.vertices.specular[next] == renderer.vertices.specular[source]);
                REQUIRE(renderer.vertices.st[1][next] == renderer.vertices.st[1][source]);
                REQUIRE(renderer.vertices.q[1][next] == renderer.vertices.q[1][source]);
                REQUIRE(renderer.vertices.attributes[next] == renderer.vertices.attributes[source]);
                REQUIRE(renderer.vertices.st[0][next] == texcoords[uv[corner]]);
                REQUIRE(renderer.vertices.q[0][next] == 1.0f);
                ++next;
            }
            REQUIRE(renderer.vertices.count == next);
            const auto& flat = renderer.indices.triangles[1];
            REQUIRE((flat.x == 10 && flat.y == 8 && flat.z == 9));
            if (sorted) REQUIRE(renderer.indices.sort_key[0] == renderer.indices.sort_key[1]);
        }
    }
    return true;
}

bool meshPassTexcoords()
{
    // Consume the producer's actual records, including a second record and
    // both UV channels. A layout/stride mismatch used to crash menu rendering.

    std::array<DrawMaterial, 2> records;
    Mesh::TriMesh mesh;
    std::array<glm::vec3, 3> positions{{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}}};
    std::array<w8_ulong, 3> indices{2, 0, 1};
    glm::mat4 identity;
    identity = glm::mat4(1);
    std::array<std::array<glm::vec2, 3>, 4> source;
    std::array<std::array<glm::vec2, 3>, 4> output;
    std::array<std::array<float, 3>, 4> q;
    std::array<std::array<glm::vec4, 3>, 2> eyes;
    std::array<std::array<unsigned char, 3>, 2> attributes;
    std::array<ShadedVertexArrays, 2> arrays{};
    for (unsigned record = 0; record < arrays.size(); ++record) {
        for (unsigned layer = 0; layer < 2; ++layer) {
            const unsigned channel = record * 2 + layer;
            for (unsigned vertex = 0; vertex < positions.size(); ++vertex)
                source[channel][vertex] =
                    glm::vec2(float(10 * channel + vertex), float(20 + vertex));
            mesh.texcoords[record][layer] = source[channel].data();
        }
        arrays[record].eye_locations = eyes[record].data();
        arrays[record].attributes = attributes[record].data();
        arrays[record].st0 = output[record * 2].data();
        arrays[record].st1 = output[record * 2 + 1].data();
        arrays[record].q0 = q[record * 2].data();
        arrays[record].q1 = q[record * 2 + 1].data();
        records[record] = makeMeshDrawPass(mesh, record, Mesh::SIDE_FRONT).vertex;
        records[record].channels =
            ~((1u << VertexChannels::CHANNEL_ST0) | (1u << VertexChannels::CHANNEL_ST1));
    }
    VertexShadingInput input(identity, identity);
    input.active_vertices = indices;
    input.positions = positions;
    input.vertex_arrays = arrays;
    input.records = records;
    shadeVertices(input);
    for (unsigned channel = 0; channel < output.size(); ++channel)
        for (unsigned vertex = 0; vertex < positions.size(); ++vertex) {
            REQUIRE(output[channel][vertex].x == source[channel][indices[vertex]].x);
            REQUIRE(output[channel][vertex].y == source[channel][indices[vertex]].y);
            REQUIRE(q[channel][vertex] == 1.0f);
        }
    Material scrolling;
    scrolling.uv_mapping = Material::UvMapping::Scroll;
    scrolling.uv_velocity = {.002f, .004f};
    for (auto& draw : records) draw.material = &scrolling;
    input.frame_time = 125;
    shadeVertices(input);
    for (unsigned channel = 0; channel < output.size(); ++channel)
        for (unsigned vertex = 0; vertex < positions.size(); ++vertex) {
            const auto offset = channel % 2 == 0 ? glm::vec2(.25f, .5f) : glm::vec2(0);
            REQUIRE(glm::length(output[channel][vertex] - source[channel][indices[vertex]] -
                                offset) < 1e-5f);
        }
    return true;
}

bool countedGeometryTransforms()
{
    std::array<glm::vec3, 65> positions, normals;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eyes;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index] = glm::vec3(index, 2, 3);
        normals[index] = glm::vec3(1, index, 2);
        indices[index] = positions.size() - index - 1;
    }
    const glm::mat4 model_view = glm::translate(glm::mat4(1), glm::vec3(4, 5, 6));
    const glm::mat4 normal_matrix = glm::scale(model_view, glm::vec3(2, 3, 4));
    VertexShadingInput input(model_view, normal_matrix);
    input.positions = positions;
    input.normals = normals;
    input.active_vertices = indices;
    ShadedVertexArrays array{};
    array.eye_locations = eyes.data();
    array.attributes = attributes.data();
    input.vertex_arrays = {&array, 1};
    Material material;
    DrawMaterial record{};
    record.material = &material;
    record.channels = 0xffffffff;
    input.records = {&record, 1};
    material.uv_mapping = Material::UvMapping::EyeNormals;
    std::array<glm::vec2, 65> uv;
    std::array<float, 65> q;
    array.st0 = uv.data();
    array.q0 = q.data();
    record.channels = ~(1u << VertexChannels::CHANNEL_ST0);

    for (bool direct : {false, true}) {
        input.direct_vertex_indices = direct;
        shadeVertices(input);
        for (unsigned index = 0; index < positions.size(); ++index) {
            const auto source = input.geometryIndex(index);
            REQUIRE(eyes[index] == model_view * glm::vec4(positions[source], 1));
            const auto normal = normal_matrix * glm::vec4(normals[source], 1);
            REQUIRE(uv[index] == (glm::vec2(normal) + 1.f) * .5f);
        }
    }
    input.normals = {};
    shadeVertices(input);
    for (const auto& value : uv) REQUIRE(value == glm::vec2(.5f));
    input.positions = std::span{positions}.first(64);
    eyes.fill(glm::vec4(-99));
    REQUIRE_THROWS_AS(shadeVertices(input), std::out_of_range);
    for (const auto& eye : eyes) REQUIRE(eye == glm::vec4(-99));
    input.positions = positions;
    input.normals = std::span{normals}.first(64);
    REQUIRE_THROWS_AS(shadeVertices(input), std::out_of_range);
    return true;
}

} // namespace

TEST_CASE("Typed scene membership preserves ownership and world poses", "[renderer][ownership]")
{
    MeshInstance instance;
    instance.transform.setLocation(4, 5, 6);
    {
        WorldScene first, second;
        first.attach(instance);
        first.attach(instance);
        REQUIRE(first.dynamic_meshes.size() == 1);
        second.attach(instance);
        REQUIRE(first.dynamic_meshes.empty());
        REQUIRE(second.dynamic_meshes.size() == 1);
        REQUIRE(instance.transform.getLocation() == glm::dvec3(4, 5, 6));
    }
    REQUIRE(instance.scene == nullptr);
    REQUIRE(instance.transform.getLocation() == glm::dvec3(4, 5, 6));
}

TEST_CASE("renderer_ownership_test")
{
    for (int cycle = 0; cycle < 2; ++cycle) {
        REQUIRE(countedGeometryTransforms());
        REQUIRE(rendererOwnership());
        REQUIRE(materialChannelOperations());
        REQUIRE(coverageFinalization());
        REQUIRE(distanceProfiles());
        REQUIRE(surfaceLighting());
        REQUIRE(meshPassTexcoords());
        REQUIRE(rendererTextureStages());
        REQUIRE(rendererCornerRemapping());
        REQUIRE(GpuRenderer::getActive() == nullptr);
    }
}

TEST_CASE("Picking honors counted selections, direct indices, nearest depth and atomic validation")
{
    GpuRenderer renderer;
    MeshInstance selected;
    renderer.setPickKey(reinterpret_cast<w8_ulong_ptr>(&selected));
    REQUIRE_FALSE(renderer.popPick());
    const std::array<glm::vec4, 3> positions{{{-1, -1, .4f, 1}, {1, -1, .4f, 1}, {0, 1, .4f, 1}}};
    const std::array<glm::ivec3, 2> triangles{{{0, 0, 0}, {1, 2, 0}}};
    const std::array<w8_ulong, 1> indices{1};
    const std::array<w8_ulong, 3> remap{2, 0, 1};
    GpuRenderer::Renderer batch({&renderer, 0, 256, 1});
    const glm::mat4 projection(1);
    const MeshPass pass{};
    GpuRenderer::Renderer::TriInput input{};
    input.indices = indices;
    input.triangles = triangles;
    input.vertices = remap;
    input.vertex_count = positions.size();
    input.record_count = 1;
    input.passes = &pass;
    input.project_clip_near = &projection;
    const auto performPick = [&] {
        batch.reset(0);
        batch.allocVertexArray(input.vertex_count);
        std::copy_n(positions.begin(), input.vertex_count,
                    batch.vertices.positions.begin() + batch.first_vertex);
        batch.render(input);
    };
    renderer.pushPick({{0, 0, 1}, nullptr, 0});
    renderer.pushPick({{2, 0, 1}, nullptr, 0});
    performPick();
    const auto outside = renderer.popPick().value();
    REQUIRE(outside.selected_model == nullptr);
    REQUIRE(outside.position.z == 1);
    const auto hit = renderer.popPick().value();
    REQUIRE(hit.selected_model == &selected);
    REQUIRE(hit.triangle_index == 1);
    REQUIRE(std::abs(hit.position.z - .4f) < 1e-6f);

    input.vertices = {};
    input.direct_vertex_indices = true;
    renderer.pushPick({{0, 0, .3f}, nullptr, 9});
    performPick();
    const auto nearer = renderer.popPick().value();
    REQUIRE(nearer.position.z == .3f);
    REQUIRE(nearer.triangle_index == 9);
    renderer.pushPick({{0, 0, 1}, nullptr, 0});
    performPick();
    REQUIRE(renderer.popPick().value().selected_model == &selected);

    const std::array<w8_ulong, 2> invalid_selection{1, 2};
    input.indices = invalid_selection;
    renderer.pushPick({{0, 0, 1}, nullptr, 9});
    REQUIRE_THROWS_AS(performPick(), std::invalid_argument);
    const auto unchanged = renderer.popPick().value();
    REQUIRE(unchanged.position.z == 1);
    REQUIRE(unchanged.triangle_index == 9);
    input.indices = indices;
    input.vertex_count = 2;
    renderer.pushPick({{0, 0, 1}, nullptr, 9});
    REQUIRE_THROWS_AS(performPick(), std::invalid_argument);
    REQUIRE(renderer.popPick().value().triangle_index == 9);
    REQUIRE_FALSE(renderer.popPick());
}

TEST_CASE("Draw material overrides stay local across passes sharing one material", "[renderer]")
{
    Material material;
    material.setOpacity(.8f);
    material.setEmissive(glm::vec4(.125f));
    std::array<DrawMaterial, 3> records;
    for (auto& record : records) {
        record.material = &material;
        record.channels =
            ~((1u << VertexChannels::CHANNEL_DIFFUSE) | (1u << VertexChannels::CHANNEL_ALPHA));
    }
    records[0].diffuse_scale = .25f;
    records[0].emissive_override = .75f;
    records[1].emissive_override = .5f;
    const glm::mat4 identity(1);
    const std::array<glm::vec3, 1> positions{glm::vec3(0, 0, 1)};
    const std::array<w8_ulong, 1> indices{0};
    std::array<glm::vec4, 3> eye, diffuse;
    std::array<unsigned char, 3> attributes{};
    std::array<ShadedVertexArrays, 3> arrays{};
    for (std::size_t pass = 0; pass < arrays.size(); ++pass) {
        arrays[pass].eye_locations = &eye[pass];
        arrays[pass].diffuse = &diffuse[pass];
        arrays[pass].attributes = &attributes[pass];
    }
    VertexShadingInput input(identity, identity);
    input.positions = positions;
    input.active_vertices = indices;
    input.records = records;
    input.vertex_arrays = arrays;

    shadeVertices(input);
    REQUIRE(diffuse[0] == glm::vec4(.75f, .75f, .75f, .2f));
    REQUIRE(diffuse[1] == glm::vec4(.5f, .5f, .5f, .8f));
    REQUIRE(diffuse[2] == glm::vec4(.125f, .125f, .125f, .8f));
    REQUIRE(material.getMaterialInfo().diffuse.w == .8f);
    REQUIRE(material.getMaterialInfo().emissive == glm::vec4(.125f));
}

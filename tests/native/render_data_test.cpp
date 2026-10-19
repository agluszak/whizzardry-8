#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat3x3.hpp> // IWYU pragma: keep - public generic-matrix entry point.

#include "compat/compiler.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_culling.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/transform.h"
#include "wiz8/render/triangle_selection.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/render/world_scene.h"

template<class T>
constexpr bool noncopyable = !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;

static_assert(std::is_destructible_v<Light>);

using ModelInstance = MeshInstance;
struct Node {
    Transform3D transform;
};

template<class T> static bool rotations()
{
    constexpr std::array angles = {
        0.0,   1e-12, -1e-7, std::numbers::pi / 2, -std::numbers::pi / 2, std::numbers::pi,
        0.731, -2.193};
    std::mt19937 random(0x55D40);
    for (int sample = 0; sample < 256; ++sample) {
        const glm::dvec3 axis = glm::normalize(glm::dvec3(int(random() % 2000001) - 1000000,
                                                          int(random() % 2000001) - 1000000,
                                                          int(random() % 2000001) - 1000000));
        for (double angle : angles) {
            Node node;
            node.transform.setRotation(angle, glm::dvec3(axis));
            // Include-cleaner attributes this public template to GLM's private qualifier.hpp.
            glm::mat<3, 3, T> actual; // NOLINT(misc-include-cleaner)
            actual = node.transform.getRotation<glm::mat<3, 3, T>>();
            // Independent Rodrigues reference checks angle sign and matrix orientation.
            const long double components[] = {axis.x, axis.y, axis.z};
            const long double sine = std::sin(static_cast<long double>(angle));
            const long double cosine = std::cos(static_cast<long double>(angle));
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    long double reference = components[row] * components[column] * (1 - cosine);
                    if (row == column)
                        reference += cosine;
                    else {
                        const int remaining = 3 - row - column;
                        reference +=
                            (column == (row + 1) % 3 ? -1 : 1) * components[remaining] * sine;
                    }
                    REQUIRE(std::abs(actual[column][row] - reference) <=
                            8 * std::numeric_limits<T>::epsilon());
                }
                for (int other = 0; other < 3; ++other)
                    REQUIRE(std::abs(glm::dot(actual[row], actual[other]) - T(row == other)) <=
                            16 * std::numeric_limits<T>::epsilon());
            }
        }
    }
    return true;
}

static bool transforms()
{
    Transform3D pose;
    pose.setLocation(4, -2, 1);
    pose.setScale(glm::dvec3(2, 3, 4));
    pose.setRotation(0, 0, std::numbers::pi / 2);
    const auto matrix = pose.matrix<glm::dmat4>();
    REQUIRE(glm::length(glm::dvec3(matrix * glm::dvec4(1, 0, 0, 1)) - glm::dvec3(4, 0, 1)) < 1e-12);
    Transform3D copy;
    copy.setMatrix(matrix);
    REQUIRE(glm::length(copy.position - pose.position) < 1e-12);
    REQUIRE(glm::length(copy.scale - pose.scale) < 1e-12);
    copy.setScale(glm::dvec3(0, 2, 3));
    REQUIRE(copy.orientation<glm::dmat3>()[0] == glm::dvec3(0));
    return true;
}

static bool interfaces()
{
    {
        Node rotated;
        rotated.transform.setRotation(0.71, glm::dvec3{3, 4, 0});
        const auto reference =
            glm::dmat3(glm::rotate(glm::dmat4(1), 0.71, glm::dvec3(0.6, 0.8, 0)));
        auto actual_rotation = rotated.transform.getRotation<glm::dmat3>();
        const auto delta = actual_rotation - reference;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row) REQUIRE(std::abs(delta[col][row]) < 1e-14);
        WorldScene scene;
        scene.setAmbientLight(0.3f, 0.4f, 0.5f);
        REQUIRE(scene.getAmbientLight() == glm::vec3(0.3f, 0.4f, 0.5f));
        scene.setAmbientLight(glm::vec3(0.6f, 0.7f, 0.8f));
        REQUIRE(scene.getAmbientLight() == glm::vec3(0.6f, 0.7f, 0.8f));
        scene.setFogColor(0.1f, 0.2f, 0.3f);
        REQUIRE(scene.getFogColor() == glm::vec3(0.1f, 0.2f, 0.3f));
        scene.setFogColor(glm::vec3(0.4f, 0.5f, 0.6f));
        REQUIRE(scene.getFogColor() == glm::vec3(0.4f, 0.5f, 0.6f));

        Light light;
        light.setAmbient(glm::vec3(0.1f, 0.2f, 0.3f));
        REQUIRE(light.getAmbient() == glm::vec3(0.1f, 0.2f, 0.3f));
        light.setSpotDirection(glm::vec3(3, 0, 4));
        REQUIRE(glm::length(light.getSpotDirection() - glm::vec3(0.6f, 0, 0.8f)) < 1e-6f);
        light.setSpotDirection(glm::vec3(0));
        REQUIRE(light.getSpotDirection() == glm::vec3(0));
        light.setAttenuation(glm::vec3(-1, 0, 2));
        REQUIRE(light.getAttenuation() == glm::vec3(0.0001f, 0.0001f, 2));

        Camera3D camera;
        camera.setClipRange(1, 100);
        camera.setViewPlane(Camera3D::Rect{-1, -1, 1, 1}, 1);
        camera.transform.setLocation(3, 4, 5);
        camera.transform.setRotation(0, std::numbers::pi / 2, 0);
        const auto projected = camera.project({13, 4, 5});
        REQUIRE(projected.result == Camera3D::PROJECTION_RESULT_ACCEPTED);
        REQUIRE(glm::length(projected.position - glm::vec3(0.5f, 0.5f, 10)) < 1e-5f);
        const auto restored = camera.unproject({0.5, 0.5, 10});
        REQUIRE(restored.has_value());
        REQUIRE(glm::length(*restored - glm::vec3(13, 4, 5)) < 1e-5f);
        REQUIRE(camera.project({13, 4, -15}).result == Camera3D::PROJECTION_RESULT_OUTSIDE_VIEW);
        const auto clipped = camera.project({203, 4, 5});
        REQUIRE(clipped.result == Camera3D::PROJECTION_RESULT_OUTSIDE_CLIP_RANGE);
        REQUIRE(clipped.position == glm::vec3(0));
        REQUIRE(!camera.unproject({2, 0.5, 10}));
        camera.flipHorizontal();
        const auto mirrored = camera.unproject({0.25, 0.5, 10});
        REQUIRE(mirrored.has_value());
        const auto roundtrip = camera.project(glm::dvec3(*mirrored));
        REQUIRE(roundtrip.result == Camera3D::PROJECTION_RESULT_ACCEPTED);
        REQUIRE(std::abs(roundtrip.position.x - 0.25f) < 1e-5f);
        REQUIRE(!camera.projectsAny({}));
        const std::array points{glm::vec3(203, 4, 5), glm::vec3(13, 4, 5)};
        REQUIRE(camera.projectsAny(points));

        ModelInstance model;
        REQUIRE(!model.isAligned());
        model.setAlignAxis(glm::vec3(3, 0, 4));
        REQUIRE(model.isAligned());
        REQUIRE(model.getAlignAxis() == glm::vec3(0.6f, 0, 0.8f));
        model.setAlignment(0);
        REQUIRE(!model.isAligned());
        model.setAlignment(2);
        REQUIRE(model.isAligned());
        model.setAlignAxis(glm::vec3(0, 0, 0));
        REQUIRE(model.getAlignAxis() == glm::vec3(0, 0, 0));
        REQUIRE(model.isAligned());
        model.setExclusionMask(0x12345678);
        REQUIRE(model.getExclusionMask() == 0x12345678);

        ClipPlane clip;
        glm::vec4 plane;
        plane = glm::vec4(1, 2, 3, 4);
        clip.setClipPlane(plane);
        const auto returned = clip.getClipPlane();
        REQUIRE((returned.x == 1 && returned.y == 2 && returned.z == 3 && returned.w == 4));
        clip.setClipType(ClipPlane::CLIP_POSITIONAL_0);
        REQUIRE(clip.getClipType() == ClipPlane::CLIP_POSITIONAL_0);
    }

    return true;
}

TEST_CASE("render_data_test")
{
    REQUIRE(rotations<float>());
    REQUIRE(rotations<double>());
    REQUIRE(transforms());
    REQUIRE(interfaces());
}

TEST_CASE("Triangle culling owns ordered indices and vertex remapping", "[render][culling]")
{
    const std::array<glm::ivec3, 4> triangles{{{5, 1, 3}, {0, 2, 4}, {4, 3, 1}, {5, 0, 2}}};
    const std::array<glm::vec3, 6> vertices{
        {{-2, 0, 0}, {2, 0, 0}, {-3, 0, 0}, {3, 0, 0}, {-4, 0, 0}, {4, 0, 0}}};
    const std::array<glm::vec4, 4> equations{
        {{0, 0, 0, 1}, {0, 0, 0, -1}, {0, 0, 0, 1}, {0, 0, 0, -1}}};
    std::array<glm::vec4, 32> planes{};
    planes[7] = {1, 0, 0, 0};
    const glm::mat4 identity(1);
    meshCulling::Input input(identity, identity);
    input.triangles = triangles;
    input.vertices = vertices;
    input.polygon_planes = equations;
    input.clip_planes = planes;
    input.cull_mode = 0;
    input.clip_mask = 1u << 7;
    auto result = meshCulling::cull(input);
    REQUIRE(result.indices == std::vector<w8_ulong>{0, 2});
    REQUIRE(result.avt == std::vector<w8_ulong>{1, 3, 4, 5});
    REQUIRE(result.vertex_remap == std::vector<w8_ulong>{0, 0, 0, 1, 2, 3});

    // Active lists retain their order and bypass the already-applied clip test.
    const std::array<w8_ulong, 3> active{3, 1, 3};
    input.active_triangles = active;
    input.cull_mode = 2;
    const auto active_result = meshCulling::cull(input);
    REQUIRE(active_result.indices == std::vector<w8_ulong>{3, 1, 3});
    REQUIRE(active_result.avt == std::vector<w8_ulong>{0, 2, 4, 5});
    REQUIRE(result.indices == std::vector<w8_ulong>{0, 2});

    input.active_triangles.reset();
    input.cull_mode = 1;
    const auto opposite = meshCulling::cull(input);
    REQUIRE(opposite.indices == std::vector<w8_ulong>{3});
    input.clip_mask = 0;
    REQUIRE(meshCulling::cull(input).indices == std::vector<w8_ulong>{1, 3});
    input.triangles = {};
    const auto empty = meshCulling::cull(input);
    REQUIRE(empty.indices.empty());
    REQUIRE(empty.avt.empty());
    REQUIRE(empty.vertex_remap.empty());
}

TEST_CASE("Renderer triangle clipping uses direct and remapped vertices", "[render][culling]")
{
    const std::array<glm::ivec3, 3> triangles{{{0, 1, 2}, {1, 2, 3}, {3, 4, 5}}};
    const std::array<unsigned char, 6> flags{1, 1, 1, 2, 2, 2};
    const std::array<w8_ulong, 6> remap{3, 4, 5, 0, 1, 2};
    std::array<w8_ulong, 4> indices{2, 0, 1, 2};
    const auto count = clipMath::selectTriangles(indices, std::span{indices}, [&](w8_ulong index) {
        return clipMath::triangleVisible(triangles[index], flags.data(), remap.data());
    });
    REQUIRE(count == 1);
    REQUIRE(indices[0] == 1);
    REQUIRE_FALSE(clipMath::triangleVisible(triangles[0], flags.data()));
    REQUIRE(clipMath::triangleVisible(triangles[1], flags.data()));
}

TEST_CASE("Triangle culling rejects inconsistent spans and retains empty selection semantics",
          "[render][culling]")
{
    std::array<glm::ivec3, 1> triangles{{{0, 1, 2}}};
    const std::array<glm::vec3, 3> vertices{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    const std::array<glm::vec4, 1> planes{{{1, 0, 0, 1}}};
    const glm::mat4 identity(1);
    meshCulling::Input input(identity, identity);
    input.triangles = triangles;
    input.vertices = vertices;
    input.clip_planes = planes;
    input.cull_mode = 2;
    input.clip_mask = 1u << 31;
    REQUIRE_THROWS_AS(meshCulling::cull(input), std::invalid_argument);
    REQUIRE_THROWS_AS(meshCulling::getClipMask({0, 0, 0}, 1, planes, 1u << 31),
                      std::invalid_argument);
    const std::array<w8_ulong, 1> active{0};
    input.active_triangles = active;
    // Explicit selections have already been clipped, so the unused plane mask is ignored.
    REQUIRE(meshCulling::cull(input).indices == std::vector<w8_ulong>{0});
    const std::array<w8_ulong, 1> invalid{1};
    input.active_triangles = invalid;
    REQUIRE_THROWS_AS(meshCulling::cull(input), std::invalid_argument);
    input.active_triangles = active;
    triangles[0].x = -1;
    REQUIRE_THROWS_AS(meshCulling::cull(input), std::invalid_argument);
    triangles[0].x = 0;
    input.cull_mode = 0;
    REQUIRE_THROWS_AS(meshCulling::cull(input), std::invalid_argument);
    input.active_triangles = std::span<const w8_ulong>{};
    REQUIRE(meshCulling::cull(input).indices.empty());
    input.active_triangles.reset();
    input.clip_mask = 0;
    input.cull_mode = 2;
    REQUIRE(meshCulling::cull(input).indices == std::vector<w8_ulong>{0});
}

TEST_CASE("Triangle clipping retains high plane bits across large meshes", "[render][culling]")
{
    std::array<glm::vec3, 513> vertices;
    std::array<glm::ivec3, 171> triangles;
    for (int vertex = 0; vertex < 513; ++vertex)
        vertices[vertex] = {vertex % 7 < 4 ? -1.0f : 1.0f, 0, 0};
    for (int triangle = 0; triangle < 171; ++triangle)
        triangles[triangle] = {3 * triangle, 3 * triangle + 1, 3 * triangle + 2};
    std::array<glm::vec4, 32> planes{};
    planes[31] = {1, 0, 0, 0};
    planes[5] = {0, 1, 0, 1};
    const glm::mat4 identity(1);
    meshCulling::Input input(identity, identity);
    input.triangles = triangles;
    input.vertices = vertices;
    input.clip_planes = planes;
    input.cull_mode = 2;
    input.clip_mask = (1u << 31) | (1u << 5);
    const auto result = meshCulling::cull(input);
    std::vector<w8_ulong> expected;
    for (w8_ulong index = 0; index < triangles.size(); ++index) {
        const auto& triangle = triangles[index];
        if (vertices[triangle.x].x >= 0 || vertices[triangle.y].x >= 0 ||
            vertices[triangle.z].x >= 0)
            expected.push_back(index);
    }
    REQUIRE(result.indices == expected);
    REQUIRE(result.indices.size() < triangles.size());
}

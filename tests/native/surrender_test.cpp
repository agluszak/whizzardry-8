#include <array>
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <type_traits>
#include <cstdlib>

#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include <glm/mat3x3.hpp> // IWYU pragma: keep - public generic-matrix entry point.
#include "surrender/srClipPlane.h"
#include "surrender/srCamera.h"
#include "surrender/srCore.h"
#include "surrender/srIlluminator.h"
#include "surrender/srLight.h"
#include "surrender/srModelInstance.h"
#include "surrender/srScene.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "glm/fwd.hpp"


template <class T>
constexpr bool noncopyable = !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;

static_assert(noncopyable<srVertexPipe>);
static_assert(std::is_destructible_v<srIlluminator>);
static_assert(std::is_destructible_v<srLight>);

struct ModelInstance : srModelInstance {};
struct Node : srNode {
    using srNode::srNode;
};

template <class T> static bool rotations()
{
    constexpr std::array angles = {0.0, 1e-12, -1e-7, std::numbers::pi / 2, -std::numbers::pi / 2, std::numbers::pi, 0.731, -2.193};
    std::mt19937 random(0x55D40);
    for (int sample = 0; sample < 256; ++sample) {
        const glm::dvec3 axis = glm::normalize(glm::dvec3(int(random() % 2000001) - 1000000,
                                                          int(random() % 2000001) - 1000000,
                                                          int(random() % 2000001) - 1000000));
        for (double angle : angles) {
            Node node;
            node.setRotation(angle, srVector3T<double>(axis));
            // Include-cleaner attributes this public template to GLM's private qualifier.hpp.
            glm::mat<3, 3, T> actual; // NOLINT(misc-include-cleaner)
            actual = node.getRotation<glm::mat<3, 3, T>>();
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
                        reference += (column == (row + 1) % 3 ? -1 : 1) * components[remaining] * sine;
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
    Node parent;
    parent.setLocation(4, -2, 1);
    parent.setScale(srVector3T<double>(2, 3, 4));
    parent.setRotation(0, 0, std::numbers::pi / 2);
    Node child(&parent);
    child.setLocation(1, 2, 3);
    auto world = child.getWorldSpaceMatrix<glm::dmat4>();
    REQUIRE(glm::length(glm::dvec3(world[3]) - glm::dvec3(-2, 0, 13)) < 1e-12);
    REQUIRE(glm::length(glm::dvec3(world * glm::dvec4(1, 0, 0, 1)) - glm::dvec3(-2, 2, 13)) < 1e-12);
    auto affine = child.getWorldSpaceMatrix<glm::dmat4x3>();
    REQUIRE(glm::dmat4(affine) == world);
    REQUIRE(child.getWorldSpaceMatrix<glm::mat4>() == glm::mat4(world));
    REQUIRE(child.getWorldSpaceMatrix<glm::mat4x3>() == glm::mat4x3(world));
    const auto coordinates = child.getWorldSpaceCoordinates();
    const auto float_coordinates = child.getWorldSpaceCoordinates<float>();
    REQUIRE(coordinates.location == glm::dvec3(world[3]));
    REQUIRE(float_coordinates.location == glm::vec3(world[3]));
    REQUIRE(glm::length(coordinates.scale - glm::dvec3(2, 3, 4)) < 1e-12);
    REQUIRE(glm::length(float_coordinates.scale - glm::vec3(2, 3, 4)) < 1e-6f);
    for (int column = 0; column < 3; ++column) {
        REQUIRE(glm::length(coordinates.rotation[column] -
                            glm::normalize(glm::dvec3(world[column]))) < 1e-12);
        REQUIRE(glm::length(float_coordinates.rotation[column] -
                            glm::normalize(glm::vec3(world[column]))) < 1e-6f);
    }

    const glm::dmat4 desired = glm::translate(glm::dmat4(1), glm::dvec3(7, 8, 9));
    child.setWorldSpaceMatrix(desired);
    world = child.getWorldSpaceMatrix<glm::dmat4>();
    for (int column = 0; column < 4; ++column)
        REQUIRE(glm::length(world[column] - desired[column]) < 1e-12);

    Node aiming;
    aiming.setLocation(4, -2, 1);
    aiming.yawAt(glm::dvec3(5, -2, 1), 1);
    REQUIRE(glm::length(aiming.getWorldSpaceDOF() - glm::dvec3(1, 0, 0)) < 1e-12);
    aiming.setRotation(glm::dmat3(1));
    aiming.pitchAt(glm::dvec3(4, -1, 1), 1);
    REQUIRE(glm::length(aiming.getWorldSpaceDOF() - glm::dvec3(0, 1, 0)) < 1e-12);
    aiming.setRotation(glm::dmat3(1));
    aiming.rollAt(glm::dvec3(1, 1, 0), .5);
    const auto roll = glm::dmat3(glm::rotate(glm::dmat4(1), -std::numbers::pi / 8,
                                           glm::dvec3(0, 0, 1)));
    for (int column = 0; column < 3; ++column)
        REQUIRE(glm::length(aiming.getRotation()[column] - roll[column]) < 1e-12);

    const glm::mat4 transform =
        glm::scale(glm::translate(glm::mat4(1), glm::vec3(3, 2, 0)), glm::vec3(2, 3, 4));
    const auto plane_matrix = glm::transpose(glm::inverse(transform));
    const auto plane = srTriangleCuller::transformClipPlane(glm::vec4(1, 0, 0, -1), plane_matrix);
    REQUIRE(glm::length(plane - glm::vec4(1, 0, 0, -5)) < 1e-6f);
    REQUIRE(glm::dot(plane, glm::vec4(6, 0, 0, 1)) > 0);
    REQUIRE(glm::dot(plane, glm::vec4(4, 0, 0, 1)) < 0);
    parent.setRotation(0.3, srVector3T<double>(0, 0, 0));
    auto rotation = parent.getRotation<glm::dmat3>();
    REQUIRE(rotation == glm::dmat3(1));
    child.setScale(srVector3T<double>(0, 2, 3));
    REQUIRE(child.getWorldSpaceRotation<glm::mat3>()[0] == glm::vec3(0));
    REQUIRE(child.getWorldSpaceRotation<glm::dmat3>()[0] == glm::dvec3(0));

    return true;
}

static bool interfaces()
{
    {
        Node rotated;
        rotated.setRotation(0.71, srVector3T<double>{3, 4, 0});
        const auto reference =
            glm::dmat3(glm::rotate(glm::dmat4(1), 0.71, glm::dvec3(0.6, 0.8, 0)));
        auto actual_rotation = rotated.getRotation<glm::dmat3>();
        const auto delta = actual_rotation - reference;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row)
                REQUIRE(std::abs(delta[col][row]) < 1e-14);
        srScene scene;
        scene.setAmbientLight(0.3f, 0.4f, 0.5f);
        REQUIRE(scene.getAmbientLight() == glm::vec3(0.3f, 0.4f, 0.5f));
        scene.setAmbientLight(glm::vec3(0.6f, 0.7f, 0.8f));
        REQUIRE(scene.getAmbientLight() == glm::vec3(0.6f, 0.7f, 0.8f));
        scene.setFogColor(0.1f, 0.2f, 0.3f);
        REQUIRE(scene.getFogColor() == glm::vec3(0.1f, 0.2f, 0.3f));
        scene.setFogColor(glm::vec3(0.4f, 0.5f, 0.6f));
        REQUIRE(scene.getFogColor() == glm::vec3(0.4f, 0.5f, 0.6f));

        srLight light;
        light.setAmbient(glm::vec3(0.1f, 0.2f, 0.3f));
        REQUIRE(light.getAmbient() == glm::vec3(0.1f, 0.2f, 0.3f));
        light.setSpotDirection(glm::vec3(3, 0, 4));
        REQUIRE(glm::length(light.getSpotDirection() - glm::vec3(0.6f, 0, 0.8f)) < 1e-6f);
        light.setSpotDirection(glm::vec3(0));
        REQUIRE(light.getSpotDirection() == glm::vec3(0));
        light.setAttenuation(glm::vec3(-1, 0, 2));
        REQUIRE(light.getAttenuation() == glm::vec3(0.0001f, 0.0001f, 2));

        srCamera camera;
        camera.setClipRange(1, 100);
        camera.setViewPlane(srCamera::Rect{-1, -1, 1, 1}, 1);
        camera.setLocation(3, 4, 5);
        camera.setRotation(0, std::numbers::pi / 2, 0);
        const auto projected = camera.project({13, 4, 5});
        REQUIRE(projected.result == srCamera::PROJECTION_RESULT_ACCEPTED);
        REQUIRE(glm::length(projected.position - glm::vec3(0.5f, 0.5f, 10)) < 1e-5f);
        const auto restored = camera.unproject({0.5, 0.5, 10});
        REQUIRE(restored.has_value());
        REQUIRE(glm::length(*restored - glm::vec3(13, 4, 5)) < 1e-5f);
        REQUIRE(camera.project({13, 4, -15}).result == srCamera::PROJECTION_RESULT_OUTSIDE_VIEW);
        const auto clipped = camera.project({203, 4, 5});
        REQUIRE(clipped.result == srCamera::PROJECTION_RESULT_OUTSIDE_CLIP_RANGE);
        REQUIRE(clipped.position == glm::vec3(0));
        REQUIRE(!camera.unproject({2, 0.5, 10}));
        camera.flipHorizontal();
        const auto mirrored = camera.unproject({0.25, 0.5, 10});
        REQUIRE(mirrored.has_value());
        const auto roundtrip = camera.project(glm::dvec3(*mirrored));
        REQUIRE(roundtrip.result == srCamera::PROJECTION_RESULT_ACCEPTED);
        REQUIRE(std::abs(roundtrip.position.x - 0.25f) < 1e-5f);
        REQUIRE(!camera.projectsAny({}));
        const std::array points{glm::vec3(203, 4, 5), glm::vec3(13, 4, 5)};
        REQUIRE(camera.projectsAny(points));

        ModelInstance model;
        REQUIRE(!model.isAligned());
        model.setAlignAxis(srVector3T<float>(3, 0, 4));
        REQUIRE(model.isAligned());
        REQUIRE(model.getAlignAxis() == srVector3T<float>(0.6f, 0, 0.8f));
        model.setAlignment(0);
        REQUIRE(!model.isAligned());
        model.setAlignment(2);
        REQUIRE(model.isAligned());
        model.setAlignAxis(srVector3T<float>(0, 0, 0));
        REQUIRE(model.getAlignAxis() == srVector3T<float>(0, 0, 0));
        REQUIRE(model.isAligned());
        model.setExclusionMask(0x12345678);
        REQUIRE(model.getExclusionMask() == 0x12345678);

        srClipPlane clip;
        glm::vec4 plane;
        plane = glm::vec4(1, 2, 3, 4);
        clip.setClipPlane(plane);
        const auto returned = clip.getClipPlane();
        REQUIRE((returned.x == 1 && returned.y == 2 && returned.z == 3 && returned.w == 4));
        clip.setClipType(srClipPlane::CLIP_POSITIONAL_0);
        REQUIRE(clip.getClipType() == srClipPlane::CLIP_POSITIONAL_0);

        srIlluminator illuminator;
    }
    REQUIRE(srExit() == 1);

    return true;
}

TEST_CASE("surrender_test")
{
    REQUIRE(srInit() == 1);
    REQUIRE(rotations<float>());
    REQUIRE(rotations<double>());
    REQUIRE(transforms());
    REQUIRE(interfaces());
}

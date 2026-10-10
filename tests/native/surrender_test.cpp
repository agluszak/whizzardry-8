#include "surrender/srClipPlane.h"
#include "surrender/srCore.h"
#include "surrender/srIlluminator.h"
#include "surrender/srLight.h"
#include "surrender/srModelInstance.h"
#include "surrender/srScene.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srVertexPipe.h"
#include <algorithm>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <random>
#include <type_traits>
#include <glm/gtc/matrix_transform.hpp>

#define CHECK(expression)                                                                          \
    do {                                                                                          \
        if (!(expression)) {                                                                      \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                               \
            return false;                                                                         \
        }                                                                                         \
    } while (0)

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
            glm::mat<3, 3, T> actual;
            node.getRotation(actual);
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
                    CHECK(std::abs(actual[column][row] - reference) <=
                          8 * std::numeric_limits<T>::epsilon());
                }
                for (int other = 0; other < 3; ++other)
                    CHECK(std::abs(glm::dot(actual[row], actual[other]) - T(row == other)) <=
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
    glm::dmat4 world;
    child.getWorldSpaceMatrix(world);
    CHECK(glm::length(glm::dvec3(world[3]) - glm::dvec3(-2, 0, 13)) < 1e-12);
    CHECK(glm::length(glm::dvec3(world * glm::dvec4(1, 0, 0, 1)) - glm::dvec3(-2, 2, 13)) < 1e-12);
    glm::dmat4x3 affine;
    child.getWorldSpaceMatrix(affine);
    CHECK(glm::dmat4(affine) == world);
    const glm::dmat4 desired = glm::translate(glm::dmat4(1), glm::dvec3(7, 8, 9));
    child.setWorldSpaceMatrix(desired);
    child.getWorldSpaceMatrix(world);
    for (int column = 0; column < 4; ++column)
        CHECK(glm::length(world[column] - desired[column]) < 1e-12);

    const glm::mat4 transform =
        glm::scale(glm::translate(glm::mat4(1), glm::vec3(3, 2, 0)), glm::vec3(2, 3, 4));
    const auto plane = srTriangleCuller::transformClipPlane(glm::vec4(1, 0, 0, -1), transform,
                                                            srRendererDefs::SCALE_TYPE_NON_UNIFORM);
    CHECK(glm::length(plane - glm::vec4(1, 0, 0, -5)) < 1e-6f);
    CHECK(glm::dot(plane, glm::vec4(6, 0, 0, 1)) > 0);
    CHECK(glm::dot(plane, glm::vec4(4, 0, 0, 1)) < 0);
    parent.setRotation(0.3, srVector3T<double>(0, 0, 0));
    glm::dmat3 rotation;
    parent.getRotation(rotation);
    CHECK(rotation == glm::dmat3(1));
    return true;
}

static bool interfaces()
{
    {
        Node rotated;
        rotated.setRotation(0.71, srVector3T<double>{3, 4, 0});
        const auto reference =
            glm::dmat3(glm::rotate(glm::dmat4(1), 0.71, glm::dvec3(0.6, 0.8, 0)));
        glm::dmat3 actual_rotation;
        rotated.getRotation(actual_rotation);
        const auto delta = actual_rotation - reference;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row)
                CHECK(std::abs(delta[col][row]) < 1e-14);
        srScene scene;
        scene.setAmbientLight(0.3f, 0.4f, 0.5f);
        CHECK(scene.getAmbientLight() == srVector3T<float>(0.3f, 0.4f, 0.5f));
        scene.setAmbientLight(srVector3T<float>(0.6f, 0.7f, 0.8f));
        srVector3T<float> color;
        scene.getAmbientLight(color);
        CHECK(color == srVector3T<float>(0.6f, 0.7f, 0.8f));
        scene.setFogColor(0.1f, 0.2f, 0.3f);
        CHECK(scene.getFogColor() == srVector3T<float>(0.1f, 0.2f, 0.3f));
        scene.setFogColor(srVector3T<float>(0.4f, 0.5f, 0.6f));
        scene.getFogColor(color);
        CHECK(color == scene.getFogColor());

        ModelInstance model;
        CHECK(!model.isAligned());
        model.setAlignAxis(srVector3T<float>(3, 0, 4));
        CHECK(model.isAligned());
        CHECK(model.getAlignAxis() == srVector3T<float>(0.6f, 0, 0.8f));
        model.setAlignment(0);
        CHECK(!model.isAligned());
        model.setAlignment(2);
        CHECK(model.isAligned());
        model.setAlignAxis(srVector3T<float>(0, 0, 0));
        CHECK(model.getAlignAxis() == srVector3T<float>(0, 0, 0));
        CHECK(model.isAligned());
        model.setExclusionMask(0x12345678);
        CHECK(model.getExclusionMask() == 0x12345678);

        srClipPlane clip;
        glm::vec4 plane;
        plane = glm::vec4(1, 2, 3, 4);
        clip.setClipPlane(plane);
        glm::vec4 output;
        clip.getClipPlane(output);
        CHECK(output.x == 1 && output.y == 2 && output.z == 3 && output.w == 4);
        const auto returned = clip.getClipPlane();
        CHECK(returned.x == 1 && returned.y == 2 && returned.z == 3 && returned.w == 4);
        clip.setClipType(srClipPlane::CLIP_POSITIONAL_0);
        CHECK(clip.getClipType() == srClipPlane::CLIP_POSITIONAL_0);

        srIlluminator illuminator;
        srLight light;
    }
    CHECK(srExit() == 1);

    return true;
}

int main()
{
    if (srInit() != 1)
        return 1;
    if (!rotations<float>() || !rotations<double>() || !transforms() ||
        !interfaces()) return 1;
    puts("ok: SurRender rotations, transforms, clipping and accessors");
}

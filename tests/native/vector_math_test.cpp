#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstddef>
#include <iterator>
#include <span>

#include "glm/geometric.hpp"
#include "surrender/srClip.h"
#include "surrender/srLight.h"
#include "surrender/srMath.h"
#include "glm/fwd.hpp"


static void CheckFloats(float actual, float expected)
{
    REQUIRE(std::bit_cast<unsigned>(actual) == std::bit_cast<unsigned>(expected));
}

static void ClippingAndLighting()
{
    std::array<glm::vec4, 513> vertices;
    std::array<SRDWORD, 513> culled;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = glm::vec4(i % 3 ? 1 : -1, 0, 0, 1);
    glm::vec4 plane;
    plane = glm::vec4(1, 0, 0, 0);
    const auto count = srRender::collectOutside(culled, plane, vertices);
    REQUIRE(count == 171);
    for (SRDWORD i = 0; i < count; ++i)
        REQUIRE(culled[i] == i * 3);
    glm::vec4 boundary[] = {glm::vec4(-2, 0, 0, 1), glm::vec4(2, 0, 0, 1),  glm::vec4(0, -2, 0, 1),
                            glm::vec4(0, 2, 0, 1),  glm::vec4(0, 0, -2, 1), glm::vec4(0, 0, 2, 1),
                            glm::vec4(1, -1, 1, 1)};
    SRBYTE flags[7];
    srRender::clipFlags(flags, boundary);
    for (int i = 0; i < 6; ++i)
        REQUIRE(flags[i] == (1 << i));
    REQUIRE(flags[6] == 0);
    glm::mat4 identity;
    identity = glm::mat4(1);
    REQUIRE(srRender::testBoundingBox(identity, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    REQUIRE(!srRender::testBoundingBox(identity, {2, 2, 2}, {3, 3, 3}));

    const auto shifted = glm::translate(identity, glm::vec3(3, 0, 0));
    REQUIRE(!srRender::testBoundingBox(shifted, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    REQUIRE(srRender::testBoundingBox(shifted, {-3.5f, -0.5f, -0.5f}, {-2.5f, 0.5f, 0.5f}));

    float light[] = {0, 0.01f, 0.5f, 1}, power[4], clamped[4];
    srLight::specularPower(power, {light, std::size(power)}, 1);
    REQUIRE((power[0] == 0 && power[1] > 0 && power[2] == 0.5f && power[3] == 1));
    srLight::specularPower(power, {light, std::size(power)}, 2);
    REQUIRE((power[0] == 0 && power[1] == 0 && power[2] == 0.25f && power[3] == 1));
    srLight::specularPower(power, {light, std::size(power)}, 127);
    srLight::specularPower(clamped, {light, std::size(clamped)}, 1000);
    for (int i = 0; i < 4; ++i)
        CheckFloats(power[i], clamped[i]);
}

static void PackedStorage()
{
    struct PackedPoint {
        unsigned char tag;
        srVector3 point;
    };
    static_assert(offsetof(PackedPoint, point) == 1);
    PackedPoint stored{0x5a, {2, -3, 4}};
    const glm::vec3 value = stored.point.glmValue();
    REQUIRE((value.x == 2 && value.y == -3 && value.z == 4));
    stored.point = value + glm::vec3(1, 2, 3);
    REQUIRE((stored.tag == 0x5a && stored.point.x == 3 && stored.point.y == -1 && stored.point.z == 7));

    stored.point.Normalize();
    REQUIRE((stored.tag == 0x5a && std::abs(stored.point.Length() - 1) < 1e-6f));
}

static void VectorMethods()
{
    srVector3 point(3, 4, 0);
    REQUIRE((point.Length() == 5 && point.LengthSquared() == 25));
    REQUIRE(point.SetLength(10) == &point);
    REQUIRE(point == srVector3(6, 8, 0));
    REQUIRE(point.Normalize() == &point);
    REQUIRE(std::abs(point.Length() - 1) < 1e-6f);
    point.SetLength(-5);
    REQUIRE((std::abs(point.x + 3) < 1e-6f && std::abs(point.y + 4) < 1e-6f));

    srVector3 zero(-0.0f, 0, -0.0f);
    zero.Normalize()->SetLength(5);
    CheckFloats(zero.x, -0.0f);
    CheckFloats(zero.y, 0.0f);
    CheckFloats(zero.z, -0.0f);
    srVector3 tiny(std::numeric_limits<float>::denorm_min(), 0, 0);
    const auto before = tiny.x;
    tiny.Normalize();
    CheckFloats(tiny.x, before);

    const auto horizontal = normalizedOrZero(glm::dvec2(3, 4));
    REQUIRE(std::abs(glm::length(horizontal) - 1) < 1e-14);
    REQUIRE(normalizedOrZero(glm::vec3(0)) == glm::vec3(0));
    point.Set(1, 0, 0);
    point.RotateAboutZ(1, 0);
    REQUIRE((std::abs(point.x) < 1e-6f && std::abs(point.y - 1) < 1e-6f));
}

TEST_CASE("vector_math_test")
{

    PackedStorage();
    VectorMethods();
    ClippingAndLighting();
    std::puts("ok: packed storage, guarded vector methods, rotations, clipping and lighting");
}

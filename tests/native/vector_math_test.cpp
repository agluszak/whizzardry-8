#include "wiz8/navigation_math.h"
#include "compat/compiler.h"
#include "surrender/triangle_runs.h"
#include <vector>
#include "surrender/bounding_radius.h"
#include "surrender/stream_scope.h"
#include "surrender/srDebug.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include <sstream>
#include <stdexcept>
#include <ios>
#include "surrender/channel_math.h"
#include "surrender/lighting_math.h"
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

TEST_CASE("Lighting kernels share bounded ramps and preserve normal-dot clamp policies", "[math][lighting]")
{
    using srChannelMath::RampDirection;
    std::array<float, 5> distances{-1, 0, 5, 10, 20}, ramp{};
    srChannelMath::distanceRamp(ramp, distances, 0, .1f, .5f, RampDirection::increasing);
    REQUIRE(ramp == std::array<float, 5>{0, 0, .25f, .5f, .5f});
    srChannelMath::distanceRamp(ramp, distances, 10, .1f, .5f, RampDirection::decreasing);
    REQUIRE(ramp == std::array<float, 5>{.5f, .5f, .25f, 0, 0});
    srChannelMath::distanceRamp(distances, distances, 0, .1f, 2, RampDirection::increasing);
    REQUIRE(distances == std::array<float, 5>{0, 0, 1, 2, 2});
    srChannelMath::multiply(ramp, distances);
    REQUIRE(ramp == std::array<float, 5>{0, 0, .25f, 0, 0});
    srChannelMath::add(ramp, .5f);
    REQUIRE(ramp[2] == .75f);
    srChannelMath::distanceRamp({}, {}, 0, 0, 0, RampDirection::increasing);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<glm::vec3, 4> normals{glm::vec3(0, 0, -1), glm::vec3(0),
                                         glm::vec3(0, 0, 2), glm::vec3(0, 0, nan)};
    const glm::vec3 direction(0, 0, 1);
    std::array<float, 4> dots{};
    srLightingMath::normalDots(dots, normals, {&direction, 1}, srLightingMath::DotRange::nonnegative);
    REQUIRE(dots[0] == 0);
    REQUIRE(dots[1] == 0);
    REQUIRE(dots[2] == 2);
    REQUIRE(std::isnan(dots[3]));
    srLightingMath::normalDots(dots, normals, {&direction, 1}, srLightingMath::DotRange::unit);
    REQUIRE(dots[2] == 1);
    const std::array<glm::vec3, 4> directions{direction, direction, -direction, direction};
    srLightingMath::normalDots(dots, normals, directions, srLightingMath::DotRange::nonnegative);
    REQUIRE(dots[2] == 0);
    srLightingMath::normalDots({}, {}, {}, srLightingMath::DotRange::unit);
    std::array<glm::vec4, 2> colors{glm::vec4(1), glm::vec4(2)};
    const std::array<float, 2> weights{.5f, 2}, attenuation{.25f, .5f};
    srLightingMath::accumulateLight(std::span<glm::vec4>(colors), glm::vec4(4), weights, attenuation);
    REQUIRE(colors[0] == glm::vec4(1.5f));
    REQUIRE(colors[1] == glm::vec4(6));
    srLightingMath::accumulateLight(std::span<glm::vec4>(colors), glm::vec4(1));
    REQUIRE(colors[0] == glm::vec4(2.5f));
    srLightingMath::accumulateLight({}, glm::vec4(1));
    std::array<glm::vec3, 2> rgb{glm::vec3(1), glm::vec3(2)};
    srLightingMath::accumulateLight(std::span<glm::vec3>(rgb), glm::vec3(4), weights, attenuation);
    REQUIRE(rgb[0] == glm::vec3(1.5f));
    REQUIRE(rgb[1] == glm::vec3(6));
}

TEST_CASE("Bounds radii preserve scalar precision and caller-selected centers")
{
    const std::array<glm::vec3, 2> points{glm::vec3(-3, 4, 0), glm::vec3(0, 0, 0)};
    REQUIRE(boundingRadius(points, glm::vec3(0)) == 5);
    REQUIRE(boundingRadius(points, glm::vec3(-3, 0, 0)) == 4);
    REQUIRE(boundingRadius(std::span<const glm::vec3>{}, glm::vec3(12)) == 0);
    const std::array<glm::vec3, 1> large{glm::vec3(1e20f, 0, 0)};
    REQUIRE(std::isfinite(boundingRadius(large, glm::dvec3(0))));
    REQUIRE(boundingRadius(large, glm::dvec3(0)) == double(large[0].x));
}

TEST_CASE("Renderer dump formatting restores native flags on failed output")
{
    std::ostringstream stream;
    stream.setf(std::ios_base::right, std::ios_base::adjustfield);
    stream.setf(std::ios_base::hex, std::ios_base::basefield);
    stream.setf(std::ios_base::showbase);
    const auto original = stream.flags();
    REQUIRE_THROWS_AS(([&] {
        const srDumpStreamScope outer(stream);
        REQUIRE((stream.flags() & std::ios_base::adjustfield) == std::ios_base::left);
        const auto inner_flags = stream.flags();
        { const srDumpStreamScope inner(stream); }
        REQUIRE(stream.flags() == inner_flags);
        throw std::runtime_error("failed diagnostic output");
    }()), std::runtime_error);
    REQUIRE(stream.flags() == original);
    stream.setf(std::ios_base::dec, std::ios_base::basefield);
    stream.unsetf(std::ios_base::showbase);
    srDumpFlags(stream, 0, nullptr);
    srDumpFlags(stream, 0b101, "first,unused,last");
    srDumpFlags(stream, 8, nullptr);
    REQUIRE(stream.str() == "[NONE][first,last][3]");
}

TEST_CASE("Triangle texture runs follow depth order and retain submission limits")
{
    const std::array<w8_ulong, 5> textures{7, 7, 8, 7, 7};
    std::vector<srTriangleRun> runs;
    const auto record = [&](const srTriangleRun& run) { runs.push_back(run); };
    visitTriangleRuns(textures, {}, textures.size(), record);
    REQUIRE(runs.size() == 3);
    REQUIRE((runs[0].first == 0 && runs[0].count == 2 && runs[0].texture_set == 7));
    REQUIRE((runs[1].first == 2 && runs[1].count == 1 && runs[1].texture_set == 8));
    REQUIRE((runs[2].first == 3 && runs[2].count == 2 && runs[2].texture_set == 7));
    std::array<w8_ulong, 5> keys{0xffffffffu, 0, 128, 0, 0};
    const auto order = sortedTriangleOrder(keys);
    REQUIRE(order == std::vector<w8_ulong>{1, 3, 4, 2, 0});
    REQUIRE(keys == std::array<w8_ulong, 5>{0, 0, 0, 128, 0xffffffffu});
    runs.clear();
    visitTriangleRuns(textures, order, 512, record);
    REQUIRE(runs.size() == 3);
    REQUIRE((runs[0].count == 3 && runs[0].texture_set == 7));
    REQUIRE((runs[1].count == 1 && runs[1].texture_set == 8));
    REQUIRE((runs[2].count == 1 && runs[2].texture_set == 7));
    std::vector<w8_ulong> uniform(1025, 7), uniform_keys(1025, 0);
    const auto uniform_order = sortedTriangleOrder(uniform_keys);
    runs.clear();
    visitTriangleRuns(uniform, uniform_order, 512, record);
    REQUIRE(runs.size() == 3);
    REQUIRE((runs[0].first == 0 && runs[0].count == 512));
    REQUIRE((runs[1].first == 512 && runs[1].count == 512));
    REQUIRE((runs[2].first == 1024 && runs[2].count == 1));
    runs.clear();
    visitTriangleRuns(uniform, {}, uniform.size(), record);
    REQUIRE((runs.size() == 1 && runs.front().count == 1025));
    runs.clear();
    visitTriangleRuns({}, {}, 512, record);
    REQUIRE(runs.empty());
    REQUIRE(sortedTriangleOrder({}).empty());
}

TEST_CASE("Navigation slide math returns complete values for blocked headings")
{
    REQUIRE_FALSE(slideDirection(0, {1, 0, 1}));
    REQUIRE_FALSE(slideDirection(255, {1, 0, 1}));
    REQUIRE_FALSE(slideDirection(1, {0, 0, 0}));
    const std::array<glm::vec3, 8> directions{{
        {0, 0, -1}, {-1, 0, -1}, {-1, 0, 0}, {-1, 0, 1},
        {0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {1, 0, -1}}};
    for (unsigned bit = 0; bit < directions.size(); ++bit) {
        const auto slide = slideDirection(255u & ~(1u << bit), -directions[bit]);
        REQUIRE(slide);
        REQUIRE(glm::length(*slide - glm::normalize(directions[bit])) < 1e-6f);
        REQUIRE(slide->y == 0);
    }
    // Opposing blocked neighbors still yield an engaged zero direction.
    const auto cancelled = slideDirection(255u & ~((1u << 3) | (1u << 7)), {1, 0, 1});
    REQUIRE(cancelled);
    REQUIRE(*cancelled == glm::vec3(0));
}

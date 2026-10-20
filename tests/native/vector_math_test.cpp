#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "wiz8/math.h"
#include "wiz8/navigation_math.h"
#include "wiz8/render/bounding_radius.h"
#include "wiz8/render/channel_math.h"
#include "wiz8/render/clip.h"
#include "wiz8/render/lighting_math.h"
#include "wiz8/render/picking_math.h"
#include "wiz8/render/triangle_projection.h"
#include "wiz8/render/triangle_runs.h"

static void CheckFloats(float actual, float expected)
{
    REQUIRE(std::bit_cast<unsigned>(actual) == std::bit_cast<unsigned>(expected));
}

static void ClippingAndLighting()
{
    std::array<glm::vec4, 513> vertices;
    std::array<std::uint32_t, 513> culled;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = glm::vec4(i % 3 ? 1 : -1, 0, 0, 1);
    glm::vec4 plane;
    plane = glm::vec4(1, 0, 0, 0);
    const auto count = clipMath::collectOutside(culled, plane, vertices);
    REQUIRE(count == 171);
    for (std::uint32_t i = 0; i < count; ++i) REQUIRE(culled[i] == i * 3);
    glm::vec4 boundary[] = {glm::vec4(-2, 0, 0, 1), glm::vec4(2, 0, 0, 1),  glm::vec4(0, -2, 0, 1),
                            glm::vec4(0, 2, 0, 1),  glm::vec4(0, 0, -2, 1), glm::vec4(0, 0, 2, 1),
                            glm::vec4(1, -1, 1, 1)};
    std::uint8_t flags[7];
    clipMath::clipFlags(flags, boundary);
    for (int i = 0; i < 6; ++i) REQUIRE(flags[i] == (1 << i));
    REQUIRE(flags[6] == 0);
    glm::mat4 identity;
    identity = glm::mat4(1);
    REQUIRE(clipMath::testBoundingBox(identity, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    REQUIRE(!clipMath::testBoundingBox(identity, {2, 2, 2}, {3, 3, 3}));

    const auto shifted = glm::translate(identity, glm::vec3(3, 0, 0));
    REQUIRE(!clipMath::testBoundingBox(shifted, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    REQUIRE(clipMath::testBoundingBox(shifted, {-3.5f, -0.5f, -0.5f}, {-2.5f, 0.5f, 0.5f}));

    float light[] = {0, 0.01f, 0.5f, 1}, power[4], clamped[4];
    lightingMath::power(power, {light, std::size(power)}, 1);
    REQUIRE((power[0] == 0 && power[1] > 0 && power[2] == 0.5f && power[3] == 1));
    lightingMath::power(power, {light, std::size(power)}, 2);
    REQUIRE((power[0] == 0 && std::abs(power[1] - .0001f) < 1e-8f && power[2] == 0.25f &&
             power[3] == 1));
    lightingMath::power(power, {light, std::size(power)}, 127);
    lightingMath::power(clamped, {light, std::size(clamped)}, 1000);
    for (int i = 0; i < 4; ++i) CheckFloats(power[i], clamped[i]);
}

static void GuardedNormalization()
{
    const glm::vec3 zero(-0.0f, 0, -0.0f);
    const auto normalized = normalizedOrZero(zero);
    CheckFloats(normalized.x, -0.0f);
    CheckFloats(normalized.y, 0.0f);
    CheckFloats(normalized.z, -0.0f);
    const glm::vec3 tiny(std::numeric_limits<float>::denorm_min(), 0, 0);
    REQUIRE(normalizedOrZero(tiny) == tiny);

    const auto horizontal = normalizedOrZero(glm::dvec2(3, 4));
    REQUIRE(std::abs(glm::length(horizontal) - 1) < 1e-14);
    REQUIRE(normalizedOrZero(glm::vec3(0)) == glm::vec3(0));
}

TEST_CASE("vector_math_test")
{

    GuardedNormalization();
    ClippingAndLighting();
    std::puts("ok: guarded normalization, clipping and lighting");
}

TEST_CASE("Lighting kernels share bounded ramps and preserve normal-dot clamp policies",
          "[math][lighting]")
{
    using channelMath::RampDirection;
    std::array<float, 5> distances{-1, 0, 5, 10, 20}, ramp{};
    channelMath::distanceRamp(ramp, distances, 0, .1f, .5f, RampDirection::increasing);
    REQUIRE(ramp == std::array<float, 5>{0, 0, .25f, .5f, .5f});
    channelMath::distanceRamp(ramp, distances, 10, .1f, .5f, RampDirection::decreasing);
    REQUIRE(ramp == std::array<float, 5>{.5f, .5f, .25f, 0, 0});
    channelMath::distanceRamp(distances, distances, 0, .1f, 2, RampDirection::increasing);
    REQUIRE(distances == std::array<float, 5>{0, 0, 1, 2, 2});
    channelMath::multiply(ramp, distances);
    REQUIRE(ramp == std::array<float, 5>{0, 0, .25f, 0, 0});
    channelMath::add(ramp, .5f);
    REQUIRE(ramp[2] == .75f);
    channelMath::distanceRamp({}, {}, 0, 0, 0, RampDirection::increasing);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<glm::vec3, 4> normals{glm::vec3(0, 0, -1), glm::vec3(0), glm::vec3(0, 0, 2),
                                           glm::vec3(0, 0, nan)};
    const glm::vec3 direction(0, 0, 1);
    std::array<float, 4> dots{};
    lightingMath::normalDots(dots, normals, {&direction, 1}, lightingMath::DotRange::nonnegative);
    REQUIRE(dots[0] == 0);
    REQUIRE(dots[1] == 0);
    REQUIRE(dots[2] == 2);
    REQUIRE(std::isnan(dots[3]));
    lightingMath::normalDots(dots, normals, {&direction, 1}, lightingMath::DotRange::unit);
    REQUIRE(dots[2] == 1);
    const std::array<glm::vec3, 4> directions{direction, direction, -direction, direction};
    lightingMath::normalDots(dots, normals, directions, lightingMath::DotRange::nonnegative);
    REQUIRE(dots[2] == 0);
    lightingMath::normalDots({}, {}, {}, lightingMath::DotRange::unit);
    std::array<glm::vec4, 2> colors{glm::vec4(1), glm::vec4(2)};
    const std::array<float, 2> weights{.5f, 2}, attenuation{.25f, .5f};
    lightingMath::accumulateLight(std::span<glm::vec4>(colors), glm::vec4(4), weights, attenuation);
    REQUIRE(colors[0] == glm::vec4(1.5f));
    REQUIRE(colors[1] == glm::vec4(6));
    lightingMath::accumulateLight(std::span<glm::vec4>(colors), glm::vec4(1));
    REQUIRE(colors[0] == glm::vec4(2.5f));
    lightingMath::accumulateLight({}, glm::vec4(1));
    std::array<glm::vec3, 2> rgb{glm::vec3(1), glm::vec3(2)};
    lightingMath::accumulateLight(std::span<glm::vec3>(rgb), glm::vec3(4), weights, attenuation);
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
TEST_CASE("Triangle texture runs follow depth order and retain submission limits")
{
    const std::array<std::uint32_t, 5> textures{7, 7, 8, 7, 7};
    std::vector<TriangleRun> runs;
    const auto record = [&](const TriangleRun& run) { runs.push_back(run); };
    visitTriangleRuns(textures, {}, textures.size(), record);
    REQUIRE(runs.size() == 3);
    REQUIRE((runs[0].first == 0 && runs[0].count == 2 && runs[0].texture_set == 7));
    REQUIRE((runs[1].first == 2 && runs[1].count == 1 && runs[1].texture_set == 8));
    REQUIRE((runs[2].first == 3 && runs[2].count == 2 && runs[2].texture_set == 7));
    std::array<std::uint32_t, 5> keys{0xffffffffu, 0, 128, 0, 0};
    const auto order = sortedTriangleOrder(keys);
    REQUIRE(order == std::vector<std::uint32_t>{1, 3, 4, 2, 0});
    REQUIRE(keys == std::array<std::uint32_t, 5>{0, 0, 0, 128, 0xffffffffu});
    runs.clear();
    visitTriangleRuns(textures, order, 512, record);
    REQUIRE(runs.size() == 3);
    REQUIRE((runs[0].count == 3 && runs[0].texture_set == 7));
    REQUIRE((runs[1].count == 1 && runs[1].texture_set == 8));
    REQUIRE((runs[2].count == 1 && runs[2].texture_set == 7));
    std::vector<std::uint32_t> uniform(1025, 7), uniform_keys(1025, 0);
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
    const std::array<glm::vec3, 8> directions{{{0, 0, -1},
                                               {-1, 0, -1},
                                               {-1, 0, 0},
                                               {-1, 0, 1},
                                               {0, 0, 1},
                                               {1, 0, 1},
                                               {1, 0, 0},
                                               {1, 0, -1}}};
    for (unsigned bit = 0; bit < directions.size(); ++bit) {
        const auto slide = slideDirection(255u & ~(1u << bit), -directions[bit]);
        REQUIRE(slide);
        REQUIRE(glm::length(slide.value() - glm::normalize(directions[bit])) < 1e-6f);
        REQUIRE(slide.value().y == 0);
    }
    // Opposing blocked neighbors still yield an engaged zero direction.
    const auto cancelled = slideDirection(255u & ~((1u << 3) | (1u << 7)), {1, 0, 1});
    REQUIRE(cancelled);
    REQUIRE(cancelled.value() == glm::vec3(0));
}

TEST_CASE("Picking edge tests retain signed W, winding and clipping depth policy")
{
    const std::array<glm::vec4, 3> triangle{{{-1, -1, .25f, 2}, {1, -1, .25f, 2}, {0, 1, .25f, 2}}};
    for (const float sign : {1.f, -1.f}) {
        std::array<glm::vec4, 3> projected;
        for (unsigned corner = 0; corner < projected.size(); ++corner) {
            auto position = triangle[corner];
            position.w *= sign;
            projected[corner] = pickingMath::projectedPosition(position);
        }
        const auto depth = pickingMath::triangleDepth(projected, glm::vec3(0, 0, 1));
        REQUIRE(depth);
        REQUIRE(std::abs(*depth - sign * .125f) < 1e-6f);
        REQUIRE_FALSE(pickingMath::triangleDepth(projected, glm::vec3(2, 0, 1)));
        std::swap(projected[0], projected[1]);
        REQUIRE(pickingMath::triangleDepth(projected, glm::vec3(0, 0, 1)));
        REQUIRE_FALSE(pickingMath::triangleDepth(projected, glm::vec3(0, 0, -1)));
    }
    const std::array<glm::vec4, 3> degenerate{};
    REQUIRE_FALSE(pickingMath::triangleDepth(degenerate, glm::vec3(0, 0, 1)));
    std::array<glm::vec4, 3> near_plane{{{-1, -1, -1, 1}, {1, -1, -1, 1}, {0, 1, -1, 1}}};
    REQUIRE(pickingMath::triangleDepth(near_plane, glm::vec3(0, 0, 1)) == -1);
    for (auto& corner : near_plane) corner.z = -1.1f;
    REQUIRE_FALSE(pickingMath::triangleDepth(near_plane, glm::vec3(0, 0, 1)));
}

TEST_CASE("Lighting direction results preserve caller zero policies and native power handles "
          "sub-unit exponents")
{
    const auto sample = lightingMath::directionAndDistance({3, 0, 4});
    REQUIRE(sample.distance == 5);
    REQUIRE(glm::length(sample.direction - glm::vec3(.6f, 0, .8f)) < 1e-7f);
    const auto zero = lightingMath::directionAndDistance(glm::vec3(0));
    REQUIRE(zero.distance == 0);
    REQUIRE(zero.direction == glm::vec3(0));
    const auto divided =
        lightingMath::directionAndDistance(glm::vec3(0), lightingMath::ZeroLengthDirection::divide);
    REQUIRE(std::isnan(divided.direction.x));
    REQUIRE(std::isnan(divided.direction.y));
    REQUIRE(std::isnan(divided.direction.z));

    std::array<float, 4> values{0, .01f, .25f, 1};
    lightingMath::power(values, values, .5f);
    REQUIRE(values[0] == 0);
    REQUIRE(std::abs(values[1] - .1f) < 1e-7f);
    REQUIRE(values[2] == .5f);
    REQUIRE(values[3] == 1);
    lightingMath::power({}, {}, 127);
}

TEST_CASE("Projected area keeps orientation and closed triangle edge coverage")
{
    using triangleProjection::orientedArea;
    REQUIRE(orientedArea(glm::vec2(0), glm::vec2(4, 0), glm::vec2(1, 3)) == 12);
    REQUIRE(orientedArea(glm::vec2(4, 0), glm::vec2(0), glm::vec2(1, 3)) == -12);
    REQUIRE(orientedArea(glm::dvec2(1e8), glm::dvec2(1e8 + 4, 1e8), glm::dvec2(1e8 + 1, 1e8 + 3)) ==
            12);
    std::array<glm::vec2, 3> triangle{{{0, 0}, {2, 0}, {0, 2}}};
    REQUIRE(triangleProjection::containsClosed(triangle, {.5f, .5f}));
    REQUIRE(triangleProjection::containsClosed(triangle, {1, 1}));
    REQUIRE(triangleProjection::containsClosed(triangle, {.5f, 0}));
    REQUIRE_FALSE(triangleProjection::containsClosed(triangle, {-1, 1}));
    std::swap(triangle[0], triangle[2]);
    REQUIRE(triangleProjection::containsClosed(triangle, {.5f, .5f}));
    REQUIRE_FALSE(triangleProjection::containsClosed(triangle, {-1, 1}));
}

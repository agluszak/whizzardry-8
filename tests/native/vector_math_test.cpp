#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "wiz8/math.h"
#include "wiz8/navigation_math.h"
#include "wiz8/render/bounding_radius.h"
#include "wiz8/render/triangle_projection.h"

static void CheckFloats(float actual, float expected)
{
    REQUIRE(std::bit_cast<unsigned>(actual) == std::bit_cast<unsigned>(expected));
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

TEST_CASE("Projected area keeps orientation")
{
    using triangleProjection::orientedArea;
    REQUIRE(orientedArea(glm::vec2(0), glm::vec2(4, 0), glm::vec2(1, 3)) == 12);
    REQUIRE(orientedArea(glm::vec2(4, 0), glm::vec2(0), glm::vec2(1, 3)) == -12);
    REQUIRE(orientedArea(glm::dvec2(1e8), glm::dvec2(1e8 + 4, 1e8), glm::dvec2(1e8 + 1, 1e8 + 3)) ==
            12);
}

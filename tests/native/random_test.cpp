#include <cstdint>
#include <cstdlib>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "random.h"

TEST_CASE("Random covers the whole requested range", "[random]")
{
    SeedRandom(1);
    for (const auto expected : {41u, 18467u, 6334u, 26500u, 19169u})
        REQUIRE(Random15() == expected);
    SeedRandom(1);
    REQUIRE(Random(0) == 0); // Empty ranges do not consume a draw.
    REQUIRE(Random15() == 41);
    SeedRandom(1);
    std::set<std::uint32_t> seen;
    for (int roll = 0; roll < 20000; ++roll) {
        const auto value = Random(10);
        REQUIRE(value < 10);
        seen.insert(value);
    }
    REQUIRE(seen.size() == 10);
    REQUIRE(Random(0) == 0);
    for (int roll = 0; roll < 1000; ++roll) REQUIRE(Random(100000) < 100000);
}

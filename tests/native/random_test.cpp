#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <set>

#include "random.h"

TEST_CASE("Random covers the whole requested range", "[random]")
{
    std::srand(1);
    std::set<std::uint32_t> seen;
    for (int roll = 0; roll < 20000; ++roll) {
        const auto value = Random(10);
        REQUIRE(value < 10);
        seen.insert(value);
    }
    REQUIRE(seen.size() == 10);
    REQUIRE(Random(0) == 0);
    for (int roll = 0; roll < 1000; ++roll) {
        REQUIRE(Random(100000) < 100000);
    }
}

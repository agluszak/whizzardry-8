#include <cstdint>

#include <SDL3/SDL_init.h>
#include <catch2/catch_test_macros.hpp>

#include "wiz8/file_time.h"

using namespace wiz8;
TEST_CASE("file_time_test")
{
    REQUIRE(SDL_Init(0));
    REQUIRE(file_time_from_sdl(0).ticks() == 116444736000000000ULL);
    REQUIRE(file_time_from_sdl(-1).ticks() == 116444735999999999ULL);
    REQUIRE(file_time_from_sdl(-100).ticks() == 116444735999999999ULL);
    REQUIRE(file_time_from_sdl(199).ticks() == 116444736000000001ULL);
    const auto last = DiskFileTime::from_ticks(2650467743999999999ULL);
    const auto positive = file_time_with_legacy_local_bias(last, 19800);
    REQUIRE(positive.ticks() == last.ticks() + 198000000000ULL);
    REQUIRE(file_time_with_legacy_local_bias(positive, -19800).ticks() == last.ticks());
    REQUIRE(file_time_with_legacy_local_bias({}, -1).ticks() == std::uint64_t(0) - 10000000);
    const auto local = current_local_time();
    REQUIRE((local.year >= 2025 && local.month >= 1 && local.month <= 12));
    REQUIRE(current_utc_offset_seconds() == local.utc_offset);
    SDL_Quit();
}

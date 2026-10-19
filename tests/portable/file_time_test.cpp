#include <cstdint>
#include <limits>
#include <optional>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "wiz8/file_time.h"

using namespace wiz8;
TEST_CASE("file_time_test")
{
    REQUIRE(SDL_Init(0));
    const auto epoch = file_time_to_utc(DiskFileTime{});
    REQUIRE((epoch.year == 1601 && epoch.month == 1 && epoch.day == 1 && epoch.day_of_week == 1));
    REQUIRE(file_time_from_sdl(0).ticks() == 116444736000000000ULL);
    REQUIRE(file_time_from_sdl(-1).ticks() == 116444735999999999ULL);
    REQUIRE(file_time_from_sdl(-100).ticks() == 116444735999999999ULL);
    REQUIRE(file_time_from_sdl(199).ticks() == 116444736000000001ULL);
    REQUIRE(file_time_to_sdl(file_time_from_sdl(-1)).value() == -100);
    const auto last = DiskFileTime::from_ticks(2650467743999999999ULL);
    REQUIRE(!file_time_to_sdl(last));
    REQUIRE(!file_time_to_sdl(DiskFileTime{}));
    const auto decoded = file_time_to_utc(last);
    REQUIRE((decoded.year == 9999 && decoded.month == 12 && decoded.day == 31));
    REQUIRE((decoded.hour == 23 && decoded.minute == 59 && decoded.second == 59));
    REQUIRE(decoded.fraction_100ns == 9999999);
    const auto max = DiskFileTime::from_ticks(std::numeric_limits<std::uint64_t>::max());
    REQUIRE(file_time_to_utc(max).year == 60056);
    const auto sdl_max = std::numeric_limits<SDL_Time>::max();
    const auto sdl_min = std::numeric_limits<SDL_Time>::min();
    REQUIRE(file_time_to_sdl(file_time_from_sdl(sdl_max)).value() == sdl_max / 100 * 100);
    // Rounding the smallest SDL nanosecond down exceeds SDL's range.
    REQUIRE(!file_time_to_sdl(file_time_from_sdl(sdl_min)));
    const auto positive = file_time_with_legacy_local_bias(last, 19800);
    REQUIRE(positive.ticks() == last.ticks() + 198000000000ULL);
    REQUIRE(file_time_with_legacy_local_bias(positive, -19800).ticks() == last.ticks());
    REQUIRE(file_time_with_legacy_local_bias({}, -1).ticks() == std::uint64_t(0) - 10000000);
    const auto local = current_local_time();
    REQUIRE((local.year >= 2025 && local.month >= 1 && local.month <= 12));
    REQUIRE(current_utc_offset_seconds() == local.utc_offset);
    SDL_Quit();
}

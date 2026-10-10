#include "wiz8/file_time.h"

#include <SDL3/SDL_init.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <stdexcept>

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #test); std::exit(1); } } while (0)

using namespace wiz8;
template <typename Function> void rejects(Function function)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
}
int main()
{
    CHECK(SDL_Init(0));
    const auto epoch = file_time_to_utc(DiskFileTime{});
    CHECK(epoch.year == 1601 && epoch.month == 1 && epoch.day == 1 && epoch.day_of_week == 1);
    CHECK(file_time_from_sdl(0).ticks() == 116444736000000000ULL);
    CHECK(file_time_from_sdl(-1).ticks() == 116444735999999999ULL);
    CHECK(file_time_from_sdl(-100).ticks() == 116444735999999999ULL);
    CHECK(file_time_from_sdl(199).ticks() == 116444736000000001ULL);
    CHECK(*file_time_to_sdl(file_time_from_sdl(-1)) == -100);
    const auto last = file_time_from_utc({9999, 12, 31, 23, 59, 59, 9999999, 0});
    CHECK(last.ticks() == 2650467743999999999ULL);
    CHECK(!file_time_to_sdl(last));
    CHECK(!file_time_to_sdl(DiskFileTime{}));
    const auto decoded = file_time_to_utc(last);
    CHECK(decoded.year == 9999 && decoded.month == 12 && decoded.day == 31);
    CHECK(decoded.hour == 23 && decoded.minute == 59 && decoded.second == 59);
    CHECK(decoded.fraction_100ns == 9999999);
    const auto max = DiskFileTime::from_ticks(std::numeric_limits<std::uint64_t>::max());
    CHECK(file_time_to_utc(max).year == 60056);
    CHECK(file_time_from_utc(file_time_to_utc(max)).ticks() == max.ticks());
    CHECK(file_time_from_utc({2000, 2, 29, 0, 0, 0, 1, 0}).ticks() == 125962560000000001ULL);
    CHECK(file_time_to_utc(file_time_from_utc({2400, 2, 29, 1, 2, 3, 4567890, 0})).year == 2400);
    rejects([] { file_time_from_utc({1900, 2, 29, 0, 0, 0, 0, 0}); });
    rejects([] { file_time_from_utc({1600, 12, 31, 0, 0, 0, 0, 0}); });
    rejects([] { file_time_from_utc({2020, 257, 1, 0, 0, 0, 0, 0}); });
    rejects([] { file_time_from_utc({2020, 1, 257, 0, 0, 0, 0, 0}); });
    rejects([] { file_time_from_utc({60056, 12, 31, 23, 59, 59, 0, 0}); });
    std::mt19937_64 random(8);
    for (unsigned i = 0; i < 50000; ++i)
    {
        const auto ticks = random();
        CHECK(file_time_from_utc(file_time_to_utc(DiskFileTime::from_ticks(ticks))).ticks() == ticks);
    }
    const auto sdl_max = std::numeric_limits<SDL_Time>::max();
    const auto sdl_min = std::numeric_limits<SDL_Time>::min();
    CHECK(*file_time_to_sdl(file_time_from_sdl(sdl_max)) == sdl_max / 100 * 100);
    // Rounding the smallest SDL nanosecond down exceeds SDL's range.
    CHECK(!file_time_to_sdl(file_time_from_sdl(sdl_min)));
    const auto positive = file_time_with_legacy_local_bias(last, 19800);
    CHECK(positive.ticks() == last.ticks() + 198000000000ULL);
    CHECK(file_time_with_legacy_local_bias(positive, -19800).ticks() == last.ticks());
    CHECK(file_time_with_legacy_local_bias({}, -1).ticks() == std::uint64_t(0) - 10000000);
    const auto local = current_local_time();
    CHECK(local.year >= 2025 && local.month >= 1 && local.month <= 12);
    CHECK(current_utc_offset_seconds() == local.utc_offset);
    SDL_Quit();
}

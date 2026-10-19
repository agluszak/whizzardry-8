#include "wiz8/file_time.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>

namespace wiz8 {
namespace {
constexpr std::uint64_t ticks_per_second = 10000000;
constexpr std::uint64_t unix_epoch_ticks = 116444736000000000;
SDL_DateTime local_now()
{
    SDL_Time now;
    SDL_DateTime local{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &local, true))
        throw std::runtime_error(std::string("local time: ") + SDL_GetError());
    return local;
}
} // namespace
DiskFileTime file_time_from_sdl(SDL_Time time) noexcept
{
    const auto ticks = time / 100 - (time < 0 && time % 100 != 0 ? 1 : 0);
    return DiskFileTime::from_ticks(unix_epoch_ticks + ticks);
}
SDL_DateTime current_local_time()
{
    return local_now();
}
int current_utc_offset_seconds()
{
    return local_now().utc_offset;
}
DiskFileTime file_time_with_legacy_local_bias(DiskFileTime utc, int seconds_east) noexcept
{
    const auto shift = std::int64_t(seconds_east) * std::int64_t(ticks_per_second);
    return DiskFileTime::from_ticks(utc.ticks() + std::uint64_t(shift));
}
} // namespace wiz8

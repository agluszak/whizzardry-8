#include "wiz8/file_time.h"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>

namespace wiz8
{
namespace
{
constexpr std::uint64_t ticks_per_second = 10000000;
constexpr std::uint64_t ticks_per_day = ticks_per_second * 86400;
constexpr std::uint64_t unix_epoch_ticks = 116444736000000000;
constexpr std::uint64_t days_per_era = 146097;
constexpr auto epoch = std::chrono::sys_days{std::chrono::year{1601} / 1 / 1};
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
std::optional<SDL_Time> file_time_to_sdl(DiskFileTime time) noexcept
{
    const auto value = time.ticks();
    if (value >= unix_epoch_ticks)
    {
        const auto delta = value - unix_epoch_ticks;
        if (delta > std::uint64_t(std::numeric_limits<SDL_Time>::max()) / 100)
            return std::nullopt;
        return SDL_Time(delta * 100);
    }
    const auto delta = unix_epoch_ticks - value;
    if (delta > (std::uint64_t(std::numeric_limits<SDL_Time>::max()) + 1) / 100)
        return std::nullopt;
    return -SDL_Time(delta * 100);
}
CivilTime file_time_to_utc(DiskFileTime time) noexcept
{
    using namespace std::chrono;
    const auto ticks = time.ticks();
    const auto day_count = ticks / ticks_per_day;
    const auto era = day_count / days_per_era;
    const auto date = year_month_day{epoch + days{day_count % days_per_era}};
    const auto seconds = (ticks % ticks_per_day) / ticks_per_second;
    return {int(date.year()) + int(era * 400), unsigned(date.month()), unsigned(date.day()),
            unsigned(seconds / 3600), unsigned((seconds % 3600) / 60), unsigned(seconds % 60),
            unsigned(ticks % ticks_per_second),
            weekday{epoch + days{day_count}}.c_encoding()};
}
DiskFileTime file_time_from_utc(const CivilTime& time)
{
    using namespace std::chrono;
    if (time.year < 1601 || time.year > 60056 || time.hour > 23 || time.minute > 59 ||
        time.second > 59 || time.fraction_100ns >= ticks_per_second ||
        time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31)
        throw std::invalid_argument("invalid disk civil time");
    const auto era = (time.year - 1601) / 400;
    const year_month_day date{year{1601 + (time.year - 1601) % 400}, month{time.month}, day{time.day}};
    if (!date.ok())
        throw std::invalid_argument("invalid disk calendar date");
    const auto days = std::uint64_t((sys_days{date} - epoch).count()) + era * days_per_era;
    const auto remainder = (std::uint64_t(time.hour) * 3600 + time.minute * 60 + time.second) *
                           ticks_per_second + time.fraction_100ns;
    if (days > (std::numeric_limits<std::uint64_t>::max() - remainder) / ticks_per_day)
        throw std::out_of_range("civil time exceeds disk tick range");
    return DiskFileTime::from_ticks(days * ticks_per_day + remainder);
}
SDL_DateTime current_local_time() { return local_now(); }
int current_utc_offset_seconds() { return local_now().utc_offset; }
DiskFileTime file_time_with_legacy_local_bias(DiskFileTime utc, int seconds_east) noexcept
{
    const auto shift = std::int64_t(seconds_east) * std::int64_t(ticks_per_second);
    return DiskFileTime::from_ticks(utc.ticks() + std::uint64_t(shift));
}
} // namespace wiz8

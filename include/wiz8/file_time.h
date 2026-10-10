#pragma once

#include <SDL3/SDL_time.h>

#include <cstdint>
#include <optional>

namespace wiz8
{
// Value codec only: consumers explicitly copy low/high to packed save/SLF
// records. This is not a runtime system-API emulation or a new disk record.
struct DiskFileTime
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    constexpr std::uint64_t ticks() const noexcept
    {
        return (std::uint64_t(high) << 32) | low;
    }
    static constexpr DiskFileTime from_ticks(std::uint64_t ticks) noexcept
    {
        return {std::uint32_t(ticks), std::uint32_t(ticks >> 32)};
    }
};

struct CivilTime
{
    int year;
    unsigned month, day, hour, minute, second;
    unsigned fraction_100ns;
    unsigned day_of_week; // Sunday = 0. Ignored when encoding.
};

// 1601-based 100ns ticks. Sub-100ns SDL times round down, including before 1970.
DiskFileTime file_time_from_sdl(SDL_Time time) noexcept;
// Out-of-range disk values do not silently narrow through SDL's nanosecond range.
std::optional<SDL_Time> file_time_to_sdl(DiskFileTime time) noexcept;
// Full uint64 disk range, with all 100ns digits retained (years 1601..60056).
// Chrono calendar arithmetic is used in 400-year eras, not nanosecond durations.
CivilTime file_time_to_utc(DiskFileTime time) noexcept;
// Invalid calendar values and values beyond uint64 ticks throw.
DiskFileTime file_time_from_utc(const CivilTime& time);

SDL_DateTime current_local_time();
int current_utc_offset_seconds(); // SDL local offset, seconds EAST of UTC.
// FileMan's historical boundary shifts UTC ticks using CURRENT timezone/DST,
// not the offset at the file's historical date. Unsigned wrap is intentional,
// matching the format boundary; precision is preserved. Pass the offset once
// for a batch of serialized times rather than applying it to disk records twice.
DiskFileTime file_time_with_legacy_local_bias(DiskFileTime utc, int seconds_east) noexcept;
} // namespace wiz8

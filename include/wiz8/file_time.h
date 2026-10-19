#pragma once

#include <cstdint>

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>

namespace wiz8 {
// Value codec only: consumers explicitly copy low/high to packed save/SLF
// records. This is not a runtime system-API emulation or a new disk record.
struct DiskFileTime {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    constexpr std::uint64_t ticks() const noexcept { return (std::uint64_t(high) << 32) | low; }
    static constexpr DiskFileTime from_ticks(std::uint64_t ticks) noexcept
    {
        return {std::uint32_t(ticks), std::uint32_t(ticks >> 32)};
    }
};

// 1601-based 100ns ticks. Sub-100ns SDL times round down, including before 1970.
DiskFileTime file_time_from_sdl(SDL_Time time) noexcept;
SDL_DateTime current_local_time();
int current_utc_offset_seconds(); // SDL local offset, seconds EAST of UTC.
// FileMan's historical boundary shifts UTC ticks using CURRENT timezone/DST,
// not the offset at the file's historical date. Unsigned wrap is intentional,
// matching the format boundary; precision is preserved. Pass the offset once
// for a batch of serialized times rather than applying it to disk records twice.
DiskFileTime file_time_with_legacy_local_bias(DiskFileTime utc, int seconds_east) noexcept;
} // namespace wiz8

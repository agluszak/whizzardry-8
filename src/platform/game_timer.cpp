#include "wiz8/platform/game_timer.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>

#include "wiz8/chunk.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/timer.h"

std::uint32_t GameClockTicks()
{
    return static_cast<std::uint32_t>(MonotonicMicroseconds() /
                                      (1'000'000 / W8_SHARED_TIMER_TICKS_PER_SECOND));
}

std::uint32_t GameClockMilliseconds()
{
    return GameInProgress() ? static_cast<std::uint32_t>(CurrentGame().time.count()) : 0;
}

std::optional<int> DurationTicks(float seconds, float scale)
{
    const double ticks = static_cast<double>(seconds) * scale * W8_SHARED_TIMER_TICKS_PER_SECOND;
    if (!(ticks >= 0.0 && ticks <= std::numeric_limits<int>::max())) return std::nullopt;
    return static_cast<int>(ticks);
}

/* The ticks `progress` covers of a `duration`-tick interval. Progress past an int (or
   not a number, as from an empty interval) has no defined conversion; it counts as none. */
static std::uint32_t ProgressTicks(int duration, float progress)
{
    const double ticks = static_cast<double>(static_cast<std::uint32_t>(duration)) * progress;
    if (!(ticks >= 0.0 && ticks <= std::numeric_limits<std::int32_t>::max())) return 0;
    return static_cast<std::uint32_t>(ticks);
}

std::uint32_t g_shared_timer_pause_base;
std::uint32_t g_shared_timer_pause_time;
bool g_shared_timer_paused;
bool g_shared_timer_flag0;
bool g_level_motion_resume_pending;

void PauseSharedGameTimers(void)
{
    g_shared_timer_paused = true;
    g_shared_timer_pause_time = GameClockTicks() - g_shared_timer_pause_base;

    GameTimeAccumulator().m_flags.paused = true;
}

void ResumeSharedGameTimers(void)
{
    g_shared_timer_paused = false;
    g_shared_timer_flag0 = false;
    g_shared_timer_pause_base = GameClockTicks() - g_shared_timer_pause_time;
    g_shared_timer_pause_time = 0;

    GameTimeAccumulator().m_flags.paused = false;
}

std::uint32_t W8GameTimer::GetTime()
{
    return ReadClock();
}

W8GameTimer::W8GameTimer() : W8GameTimer(1.0f, false) {}

/* The startup status object uses the duration-bearing constructor at
   0x004397F0.  It differs from the default constructor only in the raw-time
   flag and in converting seconds to the timer's 1/10000-second units. */
W8GameTimer::W8GameTimer(float duration, bool raw_time)
{
    Flags initial_flags = {false, false, 0, false, false, 0, 0};
    m_clock_mode = W8_TIMER_CLOCK_SHARED;
    m_flags = initial_flags;
    m_flags.raw_time = raw_time;
    m_start = 0;
    m_end = 0;
    m_duration_seconds = 0;
    m_duration_scale = 1.0f;

    m_start = ReadClock();
    m_duration_seconds = duration;
    m_duration = static_cast<int>(duration * 10000.0f);
    m_end = m_start + m_duration;
}

void W8GameTimer::SetDuration(float duration)
{
    if (duration > 0.0f) {
        m_duration_seconds = duration;
    }
    m_duration = static_cast<int>(m_duration_scale * m_duration_seconds * 10000.0f);
    m_end = m_start + m_duration;
}

void W8GameTimer::SetMode(W8TimerClock mode)
{
    m_clock_mode = mode;
    Restart();
}

void W8GameTimer::Restart()
{
    m_start = ReadClock();
    m_end = m_start + m_duration;
}

void W8GameTimer::SetDurationScale(float scale)
{
    m_duration_scale = scale;
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    float progress = GetProgress();
    m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
    SetProgress(progress);
}

void W8GameTimer::ResetDurationScale()
{
    m_flags.slow_scale = false;
    m_duration_scale = 1.0f;
    float progress = GetProgress();
    m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
    SetProgress(progress);
}

float W8GameTimer::GetProgress()
{
    const std::uint32_t sample = ReadClock();
    const std::uint32_t start = m_start;
    const std::uint32_t end = m_end;
    float progress = (sample - start) / static_cast<float>(end - start);
    // An empty interval's progress is infinite or NaN and completes no whole interval.
    const int completed =
        progress < static_cast<float>(std::numeric_limits<int>::max()) && progress >= 1.0f
            ? static_cast<int>(progress)
            : 0;

    if (completed > 0) {
        m_start =
            static_cast<std::uint32_t>(completed - 1) * static_cast<std::uint32_t>(m_duration) +
            end;
        m_end = m_start + m_duration;
    }

    if (!m_flags.paused && (!g_shared_timer_paused || m_flags.raw_time) && !g_shared_timer_flag0 &&
        (!g_level_motion_resume_pending || m_flags.raw_time)) {
        return progress;
    }
    return 0.0f;
}

void W8GameTimer::SetProgress(float progress)
{
    const std::uint32_t sample = ReadClock();
    m_start = sample - ProgressTicks(m_duration, progress);
    m_end = m_start + m_duration;
}

bool W8GameTimer::Load(W8Chunk& handle)
try {
    const auto progress = handle.ReadValue<float>();
    const auto scale = handle.ReadValue<float>();
    const auto duration = DurationTicks(m_duration_seconds, scale);
    if (!duration || !(scale > 0.0f)) throw std::runtime_error("invalid saved timer scale");
    m_duration_scale = scale;
    m_duration = *duration;
    const std::uint32_t sample = ReadClock();
    const std::uint32_t elapsed = ProgressTicks(m_duration, progress);
    m_start = sample < elapsed ? 0 : sample - elapsed;
    m_end = m_start + m_duration;
    return true;
} catch (const std::exception&) {
    return false;
}

float W8GameTimer::GetElapsedSeconds()
{
    const std::uint32_t sample = ReadClock();
    return ((sample - m_start) / static_cast<float>(m_end - m_start)) * m_duration_seconds;
}

std::uint32_t W8GameTimer::ReadClock() const
{
    switch (m_clock_mode) {
    case W8_TIMER_CLOCK_GAME:
        // Retail wraps the game-day clock modulo 2^32 after a few game days.
        return (static_cast<std::uint32_t>(CurrentGame().status.game_time_days) * 86400000u +
                CurrentGame().status.game_time_ms) *
               10u;
    default:
        break;
    }
    if (!m_flags.raw_time) {
        if (g_shared_timer_paused) {
            return g_shared_timer_pause_time;
        }
        return GameClockTicks() - g_shared_timer_pause_base;
    }
    return GameClockTicks();
}

#include "wiz8/platform/game_timer.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>

#include "wiz8/chunk.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/platform/timer.h"

std::uint64_t WorldClockMilliseconds()
{
    return GameInProgress() ? static_cast<std::uint64_t>(CurrentGame().visual_time.count()) : 0;
}

std::uint32_t GameClockMilliseconds()
{
    return GameInProgress() ? static_cast<std::uint32_t>(CurrentGame().time.count()) : 0;
}

std::optional<int> DurationTicks(float seconds, float scale)
{
    // Keep the timer's float arithmetic, but validate before narrowing to ticks.
    const float ticks = seconds * scale * W8_SHARED_TIMER_TICKS_PER_SECOND;
    if (!(ticks >= 0.0f && static_cast<double>(ticks) <= std::numeric_limits<int>::max()))
        return std::nullopt;
    return static_cast<int>(ticks);
}

static std::uint32_t ProgressTicks(int duration, float progress)
{
    const float ticks = static_cast<float>(duration) * progress;
    if (!(ticks >= 0.0f && static_cast<double>(ticks) <= std::numeric_limits<std::uint32_t>::max()))
        return 0;
    return static_cast<std::uint32_t>(ticks);
}

bool g_shared_timer_paused;
bool g_level_motion_resume_pending;

void PauseSharedGameTimers(void)
{
    g_shared_timer_paused = true;
}

void ResumeSharedGameTimers(void)
{
    g_shared_timer_paused = false;
}

W8GameTimer::W8GameTimer() : W8GameTimer(1.0f) {}

W8GameTimer::W8GameTimer(float duration, TimerClock clock)
{
    m_clock_mode = clock;
    m_flags = {};
    m_duration_scale = 1.0f;
    m_start = ReadClock();
    m_duration_seconds = duration;
    m_duration = DurationTicks(duration, 1.0f).value_or(0);
}

void W8GameTimer::SetDuration(float duration)
{
    // A non-positive duration keeps the seconds and only reapplies the scale.
    if (duration > 0.0f) {
        m_duration_seconds = duration;
    }
    m_duration = DurationTicks(m_duration_seconds, m_duration_scale).value_or(0);
}

void W8GameTimer::SetMode(TimerClock mode)
{
    m_clock_mode = mode;
    Restart();
}

void W8GameTimer::Restart()
{
    m_flags.paused = false;
    paused_elapsed_ = 0;
    m_start = ReadClock();
}

std::uint32_t W8GameTimer::ElapsedTicks() const
{
    return m_flags.paused ? paused_elapsed_ : ReadClock() - m_start;
}

void W8GameTimer::Pause()
{
    if (m_flags.paused) return;
    paused_elapsed_ = ElapsedTicks();
    m_flags.paused = true;
}

void W8GameTimer::Resume()
{
    if (!m_flags.paused) return;
    m_start = ReadClock() - paused_elapsed_;
    m_flags.paused = false;
}

void W8GameTimer::SetDurationScale(float scale)
{
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    Rescale(scale);
}

void W8GameTimer::ResetDurationScale()
{
    m_flags.slow_scale = false;
    Rescale(1.0f);
}

void W8GameTimer::Rescale(float scale)
{
    const float progress = m_duration > 0 ? ElapsedTicks() / static_cast<float>(m_duration) : 1.0f;
    m_duration_scale = scale;
    SetDuration(-1.0f);
    SetProgress(progress);
}

bool W8GameTimer::Held() const
{
    return m_flags.paused ||
           ((m_clock_mode == TimerClock::World || m_clock_mode == TimerClock::Calendar) &&
            SharedGameTimersHeld());
}

float W8GameTimer::GetProgress()
{
    if (Held()) return 0.0f;
    if (m_duration <= 0) return 1.0f;
    const auto elapsed = ElapsedTicks();
    const float progress = elapsed / static_cast<float>(m_duration);
    const auto completed = elapsed / static_cast<std::uint32_t>(m_duration);
    if (completed > 0) {
        m_start += completed * static_cast<std::uint32_t>(m_duration);
    }
    return progress;
}

void W8GameTimer::SetProgress(float progress)
{
    paused_elapsed_ = ProgressTicks(m_duration, progress);
    m_start = ReadClock() - paused_elapsed_;
}

bool W8GameTimer::Load(W8Chunk& handle)
try {
    const auto progress = handle.ReadValue<float>();
    const auto scale = handle.ReadValue<float>();
    const auto duration = DurationTicks(m_duration_seconds, scale);
    if (!duration || !(scale > 0.0f)) throw std::runtime_error("invalid saved timer scale");
    m_duration_scale = scale;
    m_duration = *duration;
    SetProgress(progress); // Unsigned interval subtraction also works at a new game epoch.
    return true;
} catch (const std::exception&) {
    return false;
}

float W8GameTimer::GetElapsedSeconds()
{
    return ElapsedTicks() / static_cast<float>(W8_SHARED_TIMER_TICKS_PER_SECOND);
}

std::uint32_t W8GameTimer::ReadClock() const
{
    switch (m_clock_mode) {
    case TimerClock::Calendar:
        return (static_cast<std::uint32_t>(CurrentGame().status.game_time_days) * 86400000u +
                CurrentGame().status.game_time_ms) *
               10u;
    case TimerClock::Presentation:
        return static_cast<std::uint32_t>(MonotonicMicroseconds() / 100);
    case TimerClock::Game:
        return GameClockMilliseconds() * 10u;
    case TimerClock::World:
        return static_cast<std::uint32_t>(WorldClockMilliseconds() * 10u);
    }
    return 0;
}

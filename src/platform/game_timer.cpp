#include "wiz8/platform/game_timer.h"

#include <cstdint>
#include <exception>

#include "wiz8/chunk.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/timer.h"

std::uint64_t WorldClockMilliseconds()
{
    return GameInProgress() ? static_cast<std::uint64_t>(CurrentGame().visual_time.count()) : 0;
}

std::uint32_t GameClockMilliseconds()
{
    return GameInProgress() ? static_cast<std::uint32_t>(CurrentGame().time.count()) : 0;
}

bool g_shared_timer_paused;
bool g_shared_timer_flag0;
bool g_level_motion_resume_pending;

void PauseSharedGameTimers(void)
{
    g_shared_timer_paused = true;

    GameTimeAccumulator().m_flags.paused = true;
}

void ResumeSharedGameTimers(void)
{
    g_shared_timer_paused = false;
    g_shared_timer_flag0 = false;

    GameTimeAccumulator().m_flags.paused = false;
}

std::uint32_t W8GameTimer::GetTime()
{
    return ReadClock();
}

W8GameTimer::W8GameTimer() : W8GameTimer(1.0f) {}

W8GameTimer::W8GameTimer(float duration, TimerClock clock)
{
    m_clock_mode = clock;
    m_flags = {};
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
    m_end = m_start + m_duration;
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
    m_end = m_start + m_duration;
    m_flags.paused = false;
}

void W8GameTimer::SetDurationScale(float scale)
{
    m_duration_scale = scale;
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    const float progress = m_duration > 0 ? ElapsedTicks() / static_cast<float>(m_duration) : 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
    SetProgress(progress);
}

void W8GameTimer::ResetDurationScale()
{
    m_flags.slow_scale = false;
    m_duration_scale = 1.0f;
    const float progress = m_duration > 0 ? ElapsedTicks() / static_cast<float>(m_duration) : 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
    SetProgress(progress);
}

float W8GameTimer::GetProgress()
{
    if (m_flags.paused ||
        ((m_clock_mode == TimerClock::World || m_clock_mode == TimerClock::Calendar) &&
         SharedGameTimersHeld()))
        return 0.0f;
    if (m_duration <= 0) return 1.0f;
    const float progress = ElapsedTicks() / static_cast<float>(m_duration);
    const auto completed = static_cast<std::uint32_t>(progress);
    if (completed > 0) {
        m_start += completed * static_cast<std::uint32_t>(m_duration);
        m_end = m_start + m_duration;
    }
    return progress;
}

void W8GameTimer::SetProgress(float progress)
{
    paused_elapsed_ = static_cast<std::uint32_t>(m_duration * progress);
    m_start = ReadClock() - paused_elapsed_;
    m_end = m_start + m_duration;
}

bool W8GameTimer::Load(W8Chunk& handle)
try {
    const auto progress = handle.ReadValue<float>();
    const auto scale = handle.ReadValue<float>();
    m_duration_scale = scale;
    {
        m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
        m_end = m_start + m_duration;
        const std::uint32_t sample = ReadClock();
        unsigned int elapsed =
            static_cast<unsigned int>(static_cast<unsigned int>(m_duration) * progress);
        paused_elapsed_ = elapsed;
        m_start = sample - elapsed; // Unsigned interval subtraction also works at a new game epoch.
        m_end = m_start + m_duration;
    }
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

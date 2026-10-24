#include "wiz8/platform/game_timer.h"

#include <cstdint>
#include <exception>

#include "wiz8/chunk.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/timer.h"

W8GameTimer::W8GameTimer(TimerClockView clocks, float duration, TimerClock clock) : clocks_(clocks)
{
    m_clock_mode = clock;
    m_flags = {};
    m_duration_scale = 1.0f;
    m_start = ReadClock();
    m_duration_seconds = duration;
    m_duration = static_cast<int>(duration * 10000.0f);
}

void W8GameTimer::SetDuration(float duration)
{
    // A non-positive duration keeps the seconds and only reapplies the scale.
    if (duration > 0.0f) {
        m_duration_seconds = duration;
    }
    m_duration = static_cast<int>(m_duration_scale * m_duration_seconds * 10000.0f);
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
    Rescale(scale);
}

void W8GameTimer::ResetDurationScale()
{
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
            clocks_.held());
}

float W8GameTimer::GetProgress()
{
    if (Held()) return 0.0f;
    if (m_duration <= 0) return 1.0f;
    const float progress = ElapsedTicks() / static_cast<float>(m_duration);
    const auto completed = static_cast<std::uint32_t>(progress);
    if (completed > 0) {
        m_start += completed * static_cast<std::uint32_t>(m_duration);
    }
    return progress;
}

void W8GameTimer::SetProgress(float progress)
{
    paused_elapsed_ = static_cast<std::uint32_t>(m_duration * progress);
    m_start = ReadClock() - paused_elapsed_;
}

bool W8GameTimer::Load(W8Chunk& handle)
try {
    const auto progress = handle.ReadValue<float>();
    m_duration_scale = handle.ReadValue<float>();
    SetDuration(-1.0f);
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
    return clocks_.read(m_clock_mode);
}

std::uint32_t TimerClockView::read(TimerClock clock) const
{
    switch (clock) {
    case TimerClock::Calendar:
        return days_ ? (static_cast<std::uint32_t>(*days_) * 86400000u + *calendar_ms_) * 10u : 0;
    case TimerClock::Presentation:
        return static_cast<std::uint32_t>(MonotonicMicroseconds() / 100);
    case TimerClock::Game:
        return step_ ? static_cast<std::uint32_t>(step_->count()) * 10u : 0;
    case TimerClock::World:
        return world_ ? static_cast<std::uint32_t>(world_->count()) * 10u : 0;
    }
    return 0;
}

float TimerClockView::motionDelta() const
{
    return motion_ ? motion_->GetFrameDelta() : 0.0f;
}

float TimerClockView::motionElapsed() const
{
    return motion_ ? motion_->GetElapsed() : 0.0f;
}

bool TimerClockView::motionSlowed() const
{
    return motion_ && motion_->slow_scale;
}

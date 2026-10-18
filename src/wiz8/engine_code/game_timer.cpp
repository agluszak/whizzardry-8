#include "wiz8/engine_code/game_timer.h"

#include <cstdint>
#include <exception>

#include "wiz8/chunk.h"
#include "wiz8/compat/native.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"

std::uint32_t GameClockTicks()
{
    return static_cast<std::uint32_t>(w8_clock_us() /
                                      (1'000'000 / W8_SHARED_TIMER_TICKS_PER_SECOND));
}

std::uint32_t GameClockMilliseconds()
{
    return static_cast<std::uint32_t>(w8_get_ticks());
}

int g_shared_timer_pause_base;
int g_shared_timer_pause_time;
bool g_shared_timer_paused;
bool g_shared_timer_flag0;
bool g_level_motion_resume_pending;

void PauseSharedGameTimers(void)
{
    g_shared_timer_paused = true;
    g_shared_timer_pause_time = GameClockTicks() - g_shared_timer_pause_base;

    auto& timer = GameTimeAccumulator();
    if (!timer.m_flags.paused) {
        timer.m_flags.paused = true;
        timer.m_start = timer.ReadClock() - timer.m_start;
    }
}

void ResumeSharedGameTimers(void)
{
    g_shared_timer_paused = false;
    g_shared_timer_flag0 = false;
    g_shared_timer_pause_base = GameClockTicks() - g_shared_timer_pause_time;
    g_shared_timer_pause_time = 0;

    auto& timer = GameTimeAccumulator();
    timer.m_flags.paused = false;
    int sample = timer.ReadClock();
    float duration = timer.m_duration_scale * timer.m_duration_seconds * 1e+04f;
    int start = sample - timer.m_start;
    timer.m_start = start;
    timer.m_duration = static_cast<int>(duration);
    timer.m_end = start + timer.m_duration;
}

int W8GameTimer::GetTime()
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
    int sample = ReadClock();
    int start = m_start;
    int end = m_end;
    float progress = static_cast<unsigned int>(sample - start) /
                     static_cast<float>(static_cast<unsigned int>(end - start));
    int completed = static_cast<int>(progress);

    if (completed != 0 && completed > 0) {
        m_start = (completed - 1) * m_duration + end;
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
    int sample = ReadClock();
    m_start = sample - static_cast<int>(static_cast<unsigned int>(m_duration) * progress);
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
        unsigned int sample = ReadClock();
        unsigned int elapsed =
            static_cast<unsigned int>(static_cast<unsigned int>(m_duration) * progress);
        m_start = sample < elapsed ? 0 : sample - elapsed;
        m_end = m_start + m_duration;
    }
    return true;
} catch (const std::exception&) {
    return false;
}

float W8GameTimer::GetElapsedSeconds()
{
    int sample = ReadClock();
    return (static_cast<unsigned int>(sample - m_start) /
            static_cast<float>(static_cast<unsigned int>(m_end - m_start))) *
           m_duration_seconds;
}

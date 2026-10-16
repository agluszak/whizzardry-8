#include "wiz8/engine_code/game_timer.h"

#include <cstddef>
#include <exception>

#include "wiz8/compat/native.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"

std::uint32_t GameClockTicks()
{
    return static_cast<std::uint32_t>(w8_clock_us() /
                                      (1'000'000 / W8_SHARED_TIMER_TICKS_PER_SECOND));
}

std::uint32_t GameClockMilliseconds()
{
    return static_cast<std::uint32_t>(w8_get_ticks());
}

// GLOBAL: WIZ8 0x006598C4
int g_shared_timer_pause_base;
// GLOBAL: WIZ8 0x006598C8
int g_shared_timer_pause_time;
// GLOBAL: WIZ8 0x006598D0
bool g_shared_timer_paused;
// GLOBAL: WIZ8 0x006598D1
bool g_shared_timer_flag0;
// GLOBAL: WIZ8 0x006598D2
bool g_level_motion_resume_pending;

// GLOBAL: WIZ8 0x005ec0a8
const float g_float_ten_thousand = 10000.0f;

// FUNCTION: WIZ8 0x00439bc0
void PauseSharedGameTimers(void)
{
    g_shared_timer_paused = true;
    g_shared_timer_pause_time = GameClockTicks() - g_shared_timer_pause_base;

    if (g_game_time_accumulator != 0 && !g_game_time_accumulator->m_flags.paused) {
        g_game_time_accumulator->m_flags.paused = true;
        g_game_time_accumulator->m_start =
            g_game_time_accumulator->ReadClock() - g_game_time_accumulator->m_start;
    }
}

// FUNCTION: WIZ8 0x00439ca0
void ResumeSharedGameTimers(void)
{
    g_shared_timer_paused = false;
    g_shared_timer_flag0 = false;
    g_shared_timer_pause_base = GameClockTicks() - g_shared_timer_pause_time;
    g_shared_timer_pause_time = 0;

    W8GameTimeAccumulator* timer = g_game_time_accumulator;
    if (timer != 0) {
        timer->m_flags.paused = false;
        int sample = timer->ReadClock();
        float duration = timer->m_duration_scale * timer->m_duration_seconds * g_float_ten_thousand;
        int start = sample - timer->m_start;
        timer->m_start = start;
        timer->m_duration = static_cast<int>(duration);
        timer->m_end = start + timer->m_duration;
    }
}

// FUNCTION: WIZ8 0x00439a60
int W8GameTimer::GetTime()
{
    return ReadClock();
}

// FUNCTION: WIZ8 0x00439550
W8GameTimer::W8GameTimer() : W8GameTimer(1.0f, false) {}

/* The startup status object uses the duration-bearing constructor at
   0x004397F0.  It differs from the default constructor only in the raw-time
   flag and in converting seconds to the timer's 1/10000-second units. */
// FUNCTION: WIZ8 0x004397f0
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

// FUNCTION: WIZ8 0x00439b80
void W8GameTimer::SetDuration(float duration)
{
    if (duration > 0.0f) {
        m_duration_seconds = duration;
    }
    m_duration = static_cast<int>(m_duration_scale * m_duration_seconds * 10000.0f);
    m_end = m_start + m_duration;
}

// FUNCTION: WIZ8 0x00439ad0
void W8GameTimer::SetMode(W8TimerClock mode)
{
    m_clock_mode = mode;
    Restart();
}

// FUNCTION: WIZ8 0x00439d80
void W8GameTimer::Restart()
{
    m_start = ReadClock();
    m_end = m_start + m_duration;
}

// FUNCTION: WIZ8 0x00439e20
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

// FUNCTION: WIZ8 0x00439fe0
void W8GameTimer::ResetDurationScale()
{
    m_flags.slow_scale = false;
    m_duration_scale = 1.0f;
    float progress = GetProgress();
    m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
    SetProgress(progress);
}

// FUNCTION: WIZ8 0x0043a190
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

// FUNCTION: WIZ8 0x0043a290
void W8GameTimer::SetProgress(float progress)
{
    int sample = ReadClock();
    m_start = sample - static_cast<int>(static_cast<unsigned int>(m_duration) * progress);
    m_end = m_start + m_duration;
}

// FUNCTION: WIZ8 0x0043a330
bool W8GameTimer::Load(wiz8::File& handle)
try {
    float progress;
    // Retail performs both reads and combines their results with bitwise OR.
    bool loaded = (handle.read(&progress, sizeof(progress)).bytes ==
                   static_cast<std::size_t>(sizeof(progress)));
    loaded |= (handle.read(&m_duration_scale, sizeof(m_duration_scale)).bytes ==
               static_cast<std::size_t>(sizeof(m_duration_scale)));
    if (loaded != 0) {
        m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
        m_end = m_start + m_duration;
        unsigned int sample = ReadClock();
        unsigned int elapsed =
            static_cast<unsigned int>(static_cast<unsigned int>(m_duration) * progress);
        m_start = sample < elapsed ? 0 : sample - elapsed;
        m_end = m_start + m_duration;
    }
    return loaded;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x0043a440
float W8GameTimer::GetElapsedSeconds()
{
    int sample = ReadClock();
    return (static_cast<unsigned int>(sample - m_start) /
            static_cast<float>(static_cast<unsigned int>(m_end - m_start))) *
           m_duration_seconds;
}

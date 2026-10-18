#include "wiz8/engine_code/GameTimeAccumulator.h"

#include <optional>

#include "game_timer.h"

// Scales real elapsed time into accumulated game time, with a per-frame clamp.

// GLOBAL: WIZ8 0x006068EC
float g_rate = 0.1f;

// GLOBAL: WIZ8 0x006598bc
static std::optional<W8GameTimeAccumulator> g_game_time_accumulator;

W8GameTimeAccumulator& GameTimeAccumulator()
{
    if (!g_game_time_accumulator) g_game_time_accumulator.emplace();
    return *g_game_time_accumulator;
}

void ResetGameTimeAccumulator()
{
    g_game_time_accumulator.reset();
}

// FUNCTION: WIZ8 0x0043a910
W8GameTimeAccumulator::W8GameTimeAccumulator()
{
    m_duration_seconds = g_rate;
    m_max_frame_delta = 2.0f;
    m_frame_delta = 0;
    m_elapsed_ticks = 0;
    m_elapsed = 0;
    m_duration_scale = 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * 10000.0f);
    m_end = m_duration;
}

// FUNCTION: WIZ8 0x0043a960
void W8GameTimeAccumulator::SetTimeScale(float scale)
{
    m_duration_scale = scale;
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    m_max_frame_delta = 2.0f / scale;
    m_duration = static_cast<int>(scale * m_duration_seconds * 10000.0f);
    Restart();
    m_frame_delta = 0.0f;
}

// FUNCTION: WIZ8 0x0043aa20
void W8GameTimeAccumulator::ResetTimeScale()
{
    m_flags.slow_scale = false;
    m_max_frame_delta = 2.0f;
    m_duration_scale = 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * 10000.0f);
    Restart();
    m_frame_delta = 0.0f;
}

// FUNCTION: WIZ8 0x0043aad0
float W8GameTimeAccumulator::Update()
{
    if (m_flags.paused || (g_shared_timer_paused && !m_flags.raw_time) || g_shared_timer_flag0) {
        m_frame_delta = 0.0f;
    } else {
        int sample = ReadClock();
        m_elapsed_ticks = static_cast<unsigned int>(sample - m_start);
        m_start = sample;
        m_frame_delta = m_elapsed_ticks / static_cast<float>(static_cast<unsigned int>(m_duration));
        if (m_frame_delta > m_max_frame_delta) {
            m_frame_delta = m_max_frame_delta;
            m_elapsed_ticks = static_cast<unsigned int>(static_cast<unsigned int>(m_duration) *
                                                        m_max_frame_delta);
        }
        m_elapsed += m_frame_delta;
    }
    if (g_level_motion_resume_pending && !m_flags.raw_time) {
        return 0.0f;
    }
    return m_frame_delta;
}

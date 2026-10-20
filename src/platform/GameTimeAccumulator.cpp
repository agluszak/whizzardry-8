#include "wiz8/platform/GameTimeAccumulator.h"

#include <algorithm>
#include <chrono>
#include <optional>

#include "wiz8/platform/game_timer.h"

// Scales the step's elapsed time into accumulated game time, with a per-frame clamp.

float g_rate = 0.1f;

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

W8GameTimeAccumulator::W8GameTimeAccumulator()
{
    m_duration_seconds = g_rate;
    m_max_frame_delta = 2.0f;
    m_frame_delta = 0;
    m_elapsed = 0;
    m_duration_scale = 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * 10000.0f);
}

void W8GameTimeAccumulator::SetTimeScale(float scale)
{
    m_duration_scale = scale;
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    m_max_frame_delta = 2.0f / scale;
    m_duration = static_cast<int>(scale * m_duration_seconds * 10000.0f);
    m_frame_delta = 0.0f;
}

void W8GameTimeAccumulator::ResetTimeScale()
{
    m_flags.slow_scale = false;
    m_max_frame_delta = 2.0f;
    m_duration_scale = 1.0f;
    m_duration = static_cast<int>(m_duration_seconds * 10000.0f);
    m_frame_delta = 0.0f;
}

float W8GameTimeAccumulator::Update(std::chrono::milliseconds elapsed)
{
    if (m_flags.paused || (g_shared_timer_paused && !m_flags.raw_time) || g_shared_timer_flag0) {
        m_frame_delta = 0.0f;
    } else {
        const auto ticks =
            static_cast<float>(elapsed.count()) * (W8_SHARED_TIMER_TICKS_PER_SECOND / 1000.0f);
        m_frame_delta = std::min(ticks / static_cast<float>(m_duration), m_max_frame_delta);
        m_elapsed += m_frame_delta;
    }
    if (g_level_motion_resume_pending && !m_flags.raw_time) {
        return 0.0f;
    }
    return m_frame_delta;
}

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

W8GameTimeAccumulator::W8GameTimeAccumulator() : W8GameTimer(g_rate) {}

void W8GameTimeAccumulator::SetTimeScale(float scale)
{
    if (scale < 0.5f) {
        m_flags.slow_scale = true;
    }
    m_duration_scale = scale;
    m_max_frame_delta = 2.0f / scale;
    SetDuration(-1.0f);
    m_frame_delta = 0.0f;
}

void W8GameTimeAccumulator::ResetTimeScale()
{
    SetTimeScale(1.0f);
    m_flags.slow_scale = false;
}

float W8GameTimeAccumulator::Update(std::chrono::milliseconds elapsed)
{
    if (m_flags.paused || SharedGameTimersHeld()) {
        m_frame_delta = 0.0f;
    } else {
        const auto ticks =
            static_cast<float>(elapsed.count()) * (W8_SHARED_TIMER_TICKS_PER_SECOND / 1000.0f);
        m_frame_delta = std::min(ticks / static_cast<float>(m_duration), m_max_frame_delta);
        m_elapsed += m_frame_delta;
    }
    return m_frame_delta;
}

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
    : base_duration_(g_rate), duration_(static_cast<int>(g_rate * W8_SHARED_TIMER_TICKS_PER_SECOND))
{
}

void W8GameTimeAccumulator::SetTimeScale(float scale)
{
    if (scale < 0.5f) {
        slow_scale = true;
    }
    m_max_frame_delta = 2.0f / scale;
    duration_ = static_cast<int>(scale * base_duration_ * W8_SHARED_TIMER_TICKS_PER_SECOND);
    m_frame_delta = 0.0f;
}

void W8GameTimeAccumulator::ResetTimeScale()
{
    SetTimeScale(1.0f);
    slow_scale = false;
}

float W8GameTimeAccumulator::Update(std::chrono::milliseconds elapsed, bool held)
{
    if (held) {
        m_frame_delta = 0.0f;
    } else {
        const auto ticks =
            static_cast<float>(elapsed.count()) * (W8_SHARED_TIMER_TICKS_PER_SECOND / 1000.0f);
        m_frame_delta = std::min(ticks / static_cast<float>(duration_), m_max_frame_delta);
        m_elapsed += m_frame_delta;
    }
    return m_frame_delta;
}

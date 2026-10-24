#include "wiz8/platform/GameTimeAccumulator.h"

#include <algorithm>
#include <chrono>

#include "wiz8/platform/game_timer.h"

void W8GameTimeAccumulator::SetTimeScale(float scale)
{
    if (scale < 0.5f) {
        slow_scale = true;
    }
    scale_ = scale;
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
        const auto duration = static_cast<int>(scale_ * g_rate * W8_SHARED_TIMER_TICKS_PER_SECOND);
        m_frame_delta = std::min(ticks / static_cast<float>(duration), 2.0f / scale_);
        m_elapsed += m_frame_delta;
    }
    return m_frame_delta;
}

#include "wiz8/platform/IntervalGate.h"

#include <algorithm>
#include <exception>

#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"
#include "wiz8/platform/game_timer.h"

W8IntervalGate::W8IntervalGate() : W8GameTimer(1.0f), m_finished(0) {}

W8IntervalGate::W8IntervalGate(float duration, TimerClock clock, bool one_shot)
    : W8GameTimer(duration, clock), m_finished(0)
{
    if (one_shot) {
        m_flags.one_shot = true;
    }
}

void W8IntervalGate::Arm()
{
    m_finished = false;
    m_start = ReadClock();
}

unsigned int W8IntervalGate::PollElapsedIntervals()
{
    if (m_finished) {
        return 1;
    }
    if (Held()) return 0;
    if (m_duration <= 0) {
        m_finished = m_flags.one_shot;
        return 1;
    }
    unsigned int intervals = (ReadClock() - m_start) / static_cast<unsigned int>(m_duration);
    if (static_cast<int>(intervals) > 0) {
        if (m_flags.one_shot) {
            m_finished = true;
            return intervals;
        }
        m_start += intervals * static_cast<unsigned int>(m_duration);
    }
    return intervals;
}

bool W8IntervalGate::LoadState(W8Chunk& handle)
try {
    if (!m_flags.one_shot) {
        return W8GameTimer::Load(handle);
    }
    m_duration_seconds = handle.ReadValue<float>();
    SetDuration(-1.0f);
    Restart();
    return true;
} catch (const std::exception&) {
    return false;
}

bool W8IntervalGate::SaveState(wiz8::File& handle)
{
    float elapsed;
    if (!m_flags.one_shot) {
        const float progress =
            m_duration > 0 ? ElapsedTicks() / static_cast<float>(m_duration) : 1.0f;
        // The on-disk pair and its OR-combined result mirror W8GameTimer::Load.
        bool saved = (handle.write(&progress, sizeof(progress)), true);
        saved |= (handle.write(&m_duration_scale, sizeof(m_duration_scale)), true);
        return saved;
    }
    if (m_finished || m_duration <= 0) {
        elapsed = 0.0f;
    } else {
        elapsed = std::max(0.0f, (m_duration - static_cast<float>(ElapsedTicks())) /
                                     W8_SHARED_TIMER_TICKS_PER_SECOND);
    }
    return (handle.write(&elapsed, sizeof(elapsed)), true);
}

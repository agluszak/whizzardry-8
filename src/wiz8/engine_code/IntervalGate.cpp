#include "wiz8/engine_code/IntervalGate.h"

#include <exception>

#include "game_timer.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

W8IntervalGate::W8IntervalGate() : W8GameTimer(1.0f, 0), m_finished(0) {}

W8IntervalGate::W8IntervalGate(float duration, bool raw_time, bool one_shot)
    : W8GameTimer(duration, raw_time), m_finished(0)
{
    if (one_shot) {
        m_flags.one_shot = true;
    }
}

void W8IntervalGate::Arm()
{
    m_finished = false;
    m_start = ReadClock();
    m_end = m_duration + m_start;
}

unsigned int W8IntervalGate::PollElapsedIntervals()
{
    if (m_finished) {
        return 1;
    }
    unsigned int intervals = static_cast<unsigned int>(ReadClock() - m_start) /
                             static_cast<unsigned int>(m_end - m_start);
    if (static_cast<int>(intervals) > 0) {
        if (m_flags.one_shot) {
            m_finished = true;
            return intervals;
        }
        m_start = (intervals - 1) * m_duration + m_end;
        m_end = m_start + m_duration;
    }
    return intervals;
}

bool W8IntervalGate::LoadState(W8Chunk& handle)
try {
    if (!m_flags.one_shot) {
        return W8GameTimer::Load(handle);
    }
    m_duration_seconds = handle.ReadValue<float>();
    {
        m_start = ReadClock();
        m_duration = static_cast<int>(m_duration_seconds * m_duration_scale * 10000.0f);
        m_end = m_start + m_duration;
    }
    return true;
} catch (const std::exception&) {
    return false;
}

bool W8IntervalGate::SaveState(wiz8::File& handle)
{
    float elapsed;
    if (!m_flags.one_shot) {
        int sample = ReadClock();
        float progress = static_cast<unsigned int>(sample - m_start) /
                         static_cast<float>(static_cast<unsigned int>(m_end - m_start));
        // The on-disk pair and its OR-combined result mirror W8GameTimer::Load.
        bool saved = (handle.write(&progress, sizeof(progress)), true);
        saved |= (handle.write(&m_duration_scale, sizeof(m_duration_scale)), true);
        return saved;
    }
    if (m_finished) {
        elapsed = 0.0f;
    } else {
        elapsed = GetElapsedSeconds();
    }
    return (handle.write(&elapsed, sizeof(elapsed)), true);
}

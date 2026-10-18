#pragma once

#include "compat/compiler.h"
#include "wiz8/engine_code/game_timer.h"

/* Address-qualified W8GameTimer specialization constructed beside the
   canonical GDCamera owner in GameData.cpp. */
class W8GameTimeAccumulator : public W8GameTimer {
public:
    W8GameTimeAccumulator(); /* 0x0043A910 */
    // Scale game time against real time and restart the frame interval.
    void SetTimeScale(float scale);
    void ResetTimeScale();
    float Update();
    float GetFrameDelta() const { return m_frame_delta; }
    float GetElapsed() const { return m_elapsed; }

private:
    float m_max_frame_delta;
    /* 0x28: elapsed ticks scaled into duration units, clamped to m_max_frame_delta. */
    float m_frame_delta;
    unsigned int m_elapsed_ticks;
    /* 0x30: running total of the scaled deltas. */
    float m_elapsed;
};

/* The session game clock: created on first use, restarted when a level's data
   is released. */
W8GameTimeAccumulator& GameTimeAccumulator();
void ResetGameTimeAccumulator();

extern float g_rate;

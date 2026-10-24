#pragma once
#include <chrono>

/* The level's frame clock: the game's step feeds it the elapsed time, which it scales
   into the frame delta that motion reads. */
class W8GameTimeAccumulator {
public:
    W8GameTimeAccumulator(); /* 0x0043A910 */
    // Scale game time against real time and restart the frame interval.
    void SetTimeScale(float scale);
    void ResetTimeScale();
    // Take one step's elapsed time; the frame delta is zero while the timers are paused.
    float Update(std::chrono::milliseconds elapsed, bool held);
    float GetFrameDelta() const { return m_frame_delta; }
    float GetElapsed() const { return m_elapsed; }
    bool slow_scale = false;

private:
    float base_duration_;
    int duration_;
    float m_max_frame_delta = 2.0f;
    // Elapsed ticks scaled into duration units, clamped to m_max_frame_delta.
    float m_frame_delta = 0.0f;
    // Running total of the scaled deltas.
    float m_elapsed = 0.0f;
};

/* The session game clock: created on first use, restarted when a level's data
   is released. */
W8GameTimeAccumulator& GameTimeAccumulator();
void ResetGameTimeAccumulator();

extern float g_rate;

#pragma once
#include <chrono>

/* The level's frame clock: the game's step feeds it the elapsed time, which it scales
   into the frame delta that motion reads. */
class W8GameTimeAccumulator {
public:
    // Scale game time against real time and restart the frame interval.
    void SetTimeScale(float scale);
    void ResetTimeScale();
    // Take one step's elapsed time; the frame delta is zero while the timers are paused.
    float Update(std::chrono::milliseconds elapsed, bool held);
    float GetFrameDelta() const { return m_frame_delta; }
    float GetElapsed() const { return m_elapsed; }
    bool slow_scale = false;

private:
    float scale_ = 1.0f;
    // The last scaled, clamped delta, in motion units rather than milliseconds.
    float m_frame_delta = 0.0f;
    // Running total of the scaled deltas.
    float m_elapsed = 0.0f;
};

inline constexpr float g_rate = 0.1f;

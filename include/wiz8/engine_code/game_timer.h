struct W8Chunk;
#pragma once

#include <cstdint>

#include "wiz8/layouts/game_status.h"

namespace wiz8 {
class File;
}

extern int g_shared_timer_pause_base;
extern int g_shared_timer_pause_time;
extern bool g_shared_timer_paused;
extern bool g_shared_timer_flag0;
extern bool g_level_motion_resume_pending;

/* The shared game timer counts in 1/10000-second units. */
inline constexpr std::uint32_t W8_SHARED_TIMER_TICKS_PER_SECOND = 10000;

// Both units share w8_clock_us's monotonic epoch, including its virtual test clock.
std::uint32_t GameClockTicks();
std::uint32_t GameClockMilliseconds();

void PauseSharedGameTimers(void);
void ResumeSharedGameTimers(void);

/* Mode 1 samples the saved game-day/millisecond clock. Mode 0 samples the
   shared clock; its separate raw-time flag controls pause adjustment. */
enum W8TimerClock { W8_TIMER_CLOCK_SHARED = 0, W8_TIMER_CLOCK_GAME = 1 };

class W8GameTimer {
public:
    struct Flags {
        bool raw_time : 1;
        bool one_shot : 1;
        unsigned char reserved_low : 1;
        bool paused : 1;
        bool slow_scale : 1;
        unsigned char reserved_high : 3;
        unsigned char reserved_byte;
    };

    W8GameTimer();
    W8GameTimer(float duration, bool raw_time);
    int ReadClock() const
    {
        switch (m_clock_mode) {
        case W8_TIMER_CLOCK_GAME:
            return (g_status.game_time_days * 86400000 + g_status.game_time_ms) * 10;
        default:
            break;
        }
        if (!m_flags.raw_time) {
            if (g_shared_timer_paused) {
                return g_shared_timer_pause_time;
            }
            return GameClockTicks() - g_shared_timer_pause_base;
        }
        return GameClockTicks();
    }
    int GetTime();
    void SetMode(W8TimerClock mode);
    void SetDuration(float duration);
    void Restart();
    float GetProgress();
    void SetProgress(float progress);
    void SetDurationScale(float scale);
    void ResetDurationScale();
    bool Load(W8Chunk& handle);
    float GetElapsedSeconds();

    W8TimerClock m_clock_mode; /* 1 reads the game clock */
    Flags m_flags;             /* runtime clock state; save/load uses progress and scale */
    int m_start;
    int m_end;                 /* start + duration */
    int m_duration;            /* 10000 */
    float m_duration_seconds;
    float m_duration_scale;
};

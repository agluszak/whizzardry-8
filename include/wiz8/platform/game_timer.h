#pragma once
struct W8Chunk;

#include <cstdint>

namespace wiz8 {
class File;
}

extern bool g_shared_timer_paused;
extern bool g_level_motion_resume_pending;

/* The shared game timer counts in 1/10000-second units. */
inline constexpr std::uint32_t W8_SHARED_TIMER_TICKS_PER_SECOND = 10000;

// Held world step time, without the animation stamp's 32-bit wrap.
std::uint64_t WorldClockMilliseconds();
/* The world's animation clock: the game's step time (`Game::time`), modulo 2^32; zero
   while no game is in progress. Cycles, monsters, missiles and particles stamp it. */
std::uint32_t GameClockMilliseconds();

void PauseSharedGameTimers(void);
void ResumeSharedGameTimers(void);
/* The shared timers are held: a timer on them makes no progress. Step-time countdowns
   that stand in for such timers stop with them. */
inline bool SharedGameTimersHeld()
{
    return g_shared_timer_paused || g_level_motion_resume_pending;
}

// Choose the owner at construction: world holds, game steps, UI presentation, or
// the saved game-day calendar. Only Presentation samples the platform clock.
enum class TimerClock { World, Game, Presentation, Calendar };

class W8GameTimer {
public:
    struct Flags {
        bool one_shot = false;
        bool paused = false;
        bool slow_scale = false;
    };

    W8GameTimer();
    explicit W8GameTimer(float duration, TimerClock clock = TimerClock::World);
    /* Clock samples and stamps are modulo 2^32 ticks: intervals are measured
       as unsigned differences, so the clock may wrap. */
    std::uint32_t ReadClock() const;
    void SetMode(TimerClock mode);
    void SetDuration(float duration);
    void Restart();
    void Pause();
    void Resume();
    float GetProgress();
    void SetProgress(float progress);
    void SetDurationScale(float scale);
    void ResetDurationScale();
    bool Load(W8Chunk& handle);
    float GetElapsedSeconds();

    TimerClock m_clock_mode;
    Flags m_flags;             /* runtime clock state; save/load uses progress and scale */
    std::uint32_t m_start;
    int m_duration;            /* 10000 */
    float m_duration_seconds;
    float m_duration_scale;

protected:
    std::uint32_t ElapsedTicks() const;
    // Paused, or on a world or calendar clock while the shared timers are held.
    bool Held() const;

private:
    void Rescale(float scale);
    std::uint32_t paused_elapsed_ = 0;
};

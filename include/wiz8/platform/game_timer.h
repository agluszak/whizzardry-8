#pragma once
struct W8Chunk;
class W8GameTimeAccumulator;

#include <chrono>
#include <cstdint>

namespace wiz8 {
class File;
}

/* The shared game timer counts in 1/10000-second units. */
inline constexpr std::uint32_t W8_SHARED_TIMER_TICKS_PER_SECOND = 10000;

// Held world step time, without the animation stamp's 32-bit wrap.
std::uint64_t WorldClockMilliseconds();
/* The world's animation clock: the game's step time (`Game::time`), modulo 2^32; zero
   while no game is in progress. Cycles, monsters, missiles and particles stamp it. */
std::uint32_t GameClockMilliseconds();

// Choose the owner at construction: world holds, game steps, UI presentation, or
// the saved game-day calendar. Only Presentation samples the platform clock.
enum class TimerClock { World, Game, Presentation, Calendar };

// Borrow the originating game's clock values and holds, never the installed session.
// An empty view is disconnected: simulation/calendar time stays at zero.
class TimerClockView {
public:
    TimerClockView() = default;
    TimerClockView(const std::chrono::milliseconds& step, const std::chrono::milliseconds& world,
                   const int& days, const unsigned& calendar_ms, bool& paused,
                   const W8GameTimeAccumulator& motion, const bool& motion_suspended)
        : step_(&step), world_(&world), days_(&days), calendar_ms_(&calendar_ms), paused_(&paused),
          motion_(&motion), motion_suspended_(&motion_suspended)
    {
    }
    std::uint32_t read(TimerClock clock) const;
    std::uint64_t worldMilliseconds() const
    {
        return world_ ? static_cast<std::uint64_t>(world_->count()) : 0;
    }
    float motionDelta() const;
    float motionElapsed() const;
    bool motionSlowed() const;
    bool motionSuspended() const { return motion_suspended_ && *motion_suspended_; }
    bool held() const { return paused_ && *paused_; }
    void pause() const
    {
        if (paused_) *paused_ = true;
    }
    void resume() const
    {
        if (paused_) *paused_ = false;
    }

private:
    const std::chrono::milliseconds* step_ = nullptr;
    const std::chrono::milliseconds* world_ = nullptr;
    const int* days_ = nullptr;
    const unsigned* calendar_ms_ = nullptr;
    bool* paused_ = nullptr;
    const W8GameTimeAccumulator* motion_ = nullptr;
    const bool* motion_suspended_ = nullptr;
};

class W8GameTimer {
public:
    struct Flags {
        bool one_shot = false;
        bool paused = false;
    };

    explicit W8GameTimer(TimerClockView clocks, float duration = 1.0f,
                         TimerClock clock = TimerClock::World);
    static W8GameTimer presentation(float duration = 1.0f)
    {
        return W8GameTimer({}, duration, TimerClock::Presentation);
    }
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
    TimerClockView clocks_;
    void Rescale(float scale);
    std::uint32_t paused_elapsed_ = 0;
};

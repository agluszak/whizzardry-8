#pragma once

#include <chrono>

struct Activity;
struct GameStatus;
struct Level;

/* The game's calendar: the saved time of day and day count (`GameStatus::game_time_ms`
   and `game_time_days`), and the level's sky and day/night lighting that follow them.
   While it runs, the step time that passes is taken up on the next catch-up, scaled by
   the time scale. */
class Calendar {
public:
    Calendar(GameStatus& status, const Activity& activity, Level& level);

    /* Advance the calendar by `elapsed` milliseconds (0x00482A20). The game clock runs
       with it while the party is in the world, and the sky moves to the new time. */
    void advance(int elapsed);
    // Step time reaches the calendar while it runs and a level is loaded.
    void passTime(std::chrono::milliseconds elapsed);
    // Run or stop the calendar; starting it drops the time not taken up yet.
    void setRunning(bool running);
    bool running() const { return running_; }
    /* The per-frame update: a running lighting transition, otherwise the calendar and,
       in day/night mode, the day phase's light and colour. */
    void update();
    // Catch up and show the day phase's light and colour.
    void showDayPhase();
    // Light a level for the time of day on entering it.
    void enterLevel();
    // Turn on the level's fog and catch its light direction up.
    void enableSky();
    /* The multiplier from step time to calendar time: 12 normally and 2880 while the
       party rests, which sight and encounters also check. */
    float timeScale() const { return time_scale_; }
    void setTimeScale(float scale) { time_scale_ = scale; }

private:
    // Advance by the time scale's share of the time not taken up yet, while running.
    void catchUp();

    GameStatus& status_;
    const Activity& activity_;
    Level& level_;
    bool running_ = false;
    float time_scale_ = 12.0f;
    std::chrono::milliseconds untaken_{};
};

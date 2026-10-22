#include "wiz8/calendar.h"

#include <chrono>
#include <utility>

#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/rules/activity.h"

Calendar::Calendar(GameStatus& status, const Activity& activity, Level& level)
    : status_(status), activity_(activity), level_(level)
{
}

void Calendar::advance(int elapsed)
{
    unsigned int time = status_.game_time_ms + elapsed;
    if (time > 86399999U) {
        ++status_.game_time_days;
    }
    status_.game_time_ms = time % 86400000U;

    /* The game clock (turns, aging, recharges, fatigue, stamina, camping) runs only while
       the party is in the world. Time reaches it from the calendar while nothing holds
       the world and no fight is on, and from the rounds of a fight there. */
    if (activity_.in_world()) {
        UpdateGameClock(elapsed);
    }
    untaken_ = {};
    level_.placeSky(status_.game_time_ms);
}

void Calendar::passTime(std::chrono::milliseconds elapsed)
{
    if (running_ && level_.primary) untaken_ += elapsed;
}

void Calendar::setRunning(bool running)
{
    running_ = running;
    if (running) untaken_ = {};
}

void Calendar::catchUp()
{
    if (!running_) return;
    const auto elapsed = std::exchange(untaken_, {});
    if (elapsed.count() != 0) {
        advance(static_cast<int>(static_cast<double>(elapsed.count()) * time_scale_));
    }
}

void Calendar::update()
{
    if (!level_.primary) return;
    if (level_.fading()) {
        level_.advanceLightingFade();
        return;
    }
    if (!running_) return;
    catchUp();
    level_.showDayNight(status_.game_time_ms);
}

void Calendar::showDayPhase()
{
    if (!level_.primary) return;
    catchUp();
    level_.showDayPhase(status_.game_time_ms);
}

void Calendar::enterLevel()
{
    if (!level_.primary) return;
    // The time that passed in the level the party left does not reach this one.
    untaken_ = {};
    level_.enterEnvironment(status_.game_time_ms);
}

void Calendar::enableSky()
{
    if (!level_.primary) return;
    catchUp();
    level_.enableSky(status_.game_time_ms);
}

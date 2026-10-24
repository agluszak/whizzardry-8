#pragma once
struct W8Chunk;
#include "wiz8/platform/game_timer.h"

namespace wiz8 {
class File;
}

// Counts elapsed intervals, or latches completion until explicitly rearmed.
class W8IntervalGate : public W8GameTimer {
public:
    explicit W8IntervalGate(TimerClockView clocks, float duration = 1.0f,
                            TimerClock clock = TimerClock::World, bool one_shot = false);
    /* 0x0043A500 */
    void Arm();                          /* 0x0043A530 */
    unsigned int PollElapsedIntervals(); /* 0x0043A5D0 */
    bool IsFinished() const { return m_finished; }
    /* Every waiter polls an unfinished gate before testing it again; the
       level scripts and the camera's manual-input hold expand this body. */
    bool PollFinished()
    {
        if (!IsFinished()) {
            PollElapsedIntervals();
        }
        return IsFinished();
    }
    // Gate persistence: one-shot gates store only their duration.
    bool LoadState(W8Chunk& handle);
    bool SaveState(wiz8::File& handle);

private:
    bool m_finished;
};

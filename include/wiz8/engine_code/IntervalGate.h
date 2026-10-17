#pragma once
struct W8Chunk;

#include "compat/compiler.h"
#include "wiz8/engine_code/game_timer.h"

namespace wiz8 {
class File;
}

// Counts elapsed intervals, or latches completion until explicitly rearmed.
class W8IntervalGate : public W8GameTimer {
public:
    W8IntervalGate(); /* 0x0043A4E0 */
    W8IntervalGate(float duration, bool raw_time, bool one_shot);
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
    bool Load(W8Chunk& handle);
    bool Save(wiz8::File& handle);

private:
    bool m_finished;
};

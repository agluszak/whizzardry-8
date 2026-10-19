#pragma once
struct W8Chunk;
/* Persisted lock/trap device state: `completed` latches once the pick/disarm
   interaction finishes; `pins` holds the eight tumbler bytes of a pickable
   lock, re-rolled by LockState::Reset. */
struct TriggerDeviceState {
    unsigned char completed;
    unsigned char pins[8];
};

enum class LockType : unsigned char { none = 0, pickable_lock = 1, trap = 2, key_lock = 3 };

/* The locks & traps device record embedded at the tail of Trigger. `lock_type` is the editor "Type" (none,
   pickable lock, trap, or key lock); `difficulty` is the editor "Difficulty"
   grade — it doubles as the pickable lock's pin budget; `device_id` indexes
   the tumbler/trap tables (-1 = roll on first use); `lock_countdown` ticks the
   pick interaction (pins remaining, difficulty * 3); `last_interaction_clock`
   is the world clock of the last attempt (-1 = never). */
struct LockState {
    /* Re-rolls the eight pin bytes of a pickable lock and
       resets its difficulty-derived countdown and interaction state. */
    void Reset(); /* 0x00445730 */
    void ReadRuntimeRecord(W8Chunk& handle, int version, int restoring);
    /* Spends one point of the lock countdown and reports whether one
       remained to spend. */
    bool ConsumeCountdown(); /* 0x004457A0 */

    LockType lock_type;
    int difficulty;
    TriggerDeviceState device_state;
    int device_id;
    int key_id;
    int lock_countdown;
    int last_interaction_clock;
};

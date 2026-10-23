#include "wiz8/entities/locks.h"

#include "wiz8/entities/saved_world_sections.h"
#include "wiz8/rules/random.h"

void LockState::Reset(Rng& rng)
{
    int pins;

    if (lock_type == LockType::pickable_lock) {
        for (int pin = 0; pin < 8; ++pin) {
            device_state.pins[pin] = static_cast<unsigned char>(rng.below(4));
        }
        pins = difficulty < 8 ? (difficulty < 2 ? 2 : difficulty) : 8;
        lock_countdown = pins * 3;
    }
    last_interaction_clock = -1;
    device_state.completed = 0;
}

bool LockState::ConsumeCountdown()
{
    if (lock_countdown > 0) {
        --lock_countdown;
        return true;
    }
    return false;
}

void LockState::Restore(const SavedLock& saved)
{
    if (saved.full) {
        lock_type = saved.lock_type;
        difficulty = saved.difficulty;
        device_id = saved.device_id;
        key_id = saved.key_id;
    }
    device_state = saved.device_state;
    if (saved.lock_countdown) lock_countdown = *saved.lock_countdown;
    if (saved.last_interaction_clock) last_interaction_clock = *saved.last_interaction_clock;
}

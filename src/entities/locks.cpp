#include "wiz8/entities/locks.h"

#include "wiz8/chunk.h"
#include "wiz8/platform/random.h"

void LockState::Reset()
{
    int pins;

    if (lock_type == LockType::pickable_lock) {
        for (int pin = 0; pin < 8; ++pin) {
            device_state.pins[pin] = static_cast<char>(Random(4));
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

void LockState::ReadRuntimeRecord(W8Chunk& handle, int version, int restoring)
{
    int record_version;

    if (restoring == 0 && version > 1) {
        handle.ReadBytes(&record_version, sizeof(record_version));
        handle.ReadBytes(&device_state.completed, sizeof(device_state.completed));
        handle.ReadBytes(device_state.pins, sizeof(device_state.pins));
        handle.ReadBytes(&lock_countdown, sizeof(lock_countdown));
        if (record_version > 1) {
            handle.ReadBytes(&last_interaction_clock, sizeof(last_interaction_clock));
        }
    } else {
        int serialized_lock_type;
        handle.ReadBytes(&record_version, sizeof(record_version));
        handle.ReadBytes(&serialized_lock_type, sizeof(serialized_lock_type));
        lock_type = static_cast<LockType>(serialized_lock_type);
        handle.ReadBytes(&difficulty, sizeof(difficulty));
        handle.ReadBytes(&device_state.completed, sizeof(device_state.completed));
        handle.ReadBytes(device_state.pins, sizeof(device_state.pins));
        handle.ReadBytes(&device_id, sizeof(device_id));
        handle.ReadBytes(&key_id, sizeof(key_id));
        if (record_version > 1) {
            handle.ReadBytes(&lock_countdown, sizeof(lock_countdown));
        }
    }
}

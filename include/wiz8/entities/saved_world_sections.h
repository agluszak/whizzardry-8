#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "wiz8/entities/locks.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/save_game.h"

struct W8Chunk;
struct World;

/* The sections of a level group that name a level's own triggers and props (TRIG, APST,
   LCKS and LOCK, TRES), decoded as values. A load decodes the whole group before any of it
   reaches the replacement world, then applies the sections in the order they were read:
   lock states update the doors the trigger states restored. */

// A door's saved state: its flags, and in the packed form the reopening delay and its key.
struct SavedDoorState {
    bool open = false, lockable = false, locked = false, auto_locking = false, one_way = false,
         secret = false, found = false, jammable = false, jammed = false;
    // The packed form, which also stores the timed reopening's progress (ms) and the key.
    bool packed = false;
    std::uint16_t progress_delay = 0;
    std::uint16_t item = 0;
};

struct SavedTriggerLoot {
    std::optional<SavedWorldItem> item_group;
    unsigned char items_generated = 0;
    unsigned int item_group_seed = 0;
    int gold = 0;
    int uses_remaining = 0;
};

struct SavedTrigger {
    // The record's version tag; version 5 names its trigger, earlier ones follow the order.
    std::uint8_t version = 0;
    std::optional<std::string> name;
    unsigned int flags = 0;
    unsigned char state_index = 0;
    W8TriggerStateDirection state_direction{};
    unsigned short action = 0;
    W8TriggerActionState action_state = W8TriggerActionState::inactive;
    int required_item_id = 0;
    // The kind of action payload saved with the trigger, and the door's state when it is one.
    std::optional<std::uint8_t> payload_kind;
    std::optional<SavedDoorState> door;
    // Versions after the first also keep the trigger's items, seed, gold and uses.
    std::optional<SavedTriggerLoot> loot;
};

// A lock's saved state; the full form also carries what the level authored.
struct SavedLock {
    std::string trigger;
    int record_version = 0;
    bool full = false;
    LockType lock_type = LockType::none;
    int difficulty = 0;
    TriggerDeviceState device_state{};
    int device_id = 0;
    int key_id = 0;
    std::optional<int> lock_countdown;
    std::optional<int> last_interaction_clock;
};
struct SavedTriggerLocks {
    // Sections before version 2 store every lock in the full form.
    int version = 2;
    // A restoring section re-rolls each lock after reading it.
    bool restoring = false;
    std::vector<SavedLock> locks;
};

struct SavedTriggerAction {
    std::string trigger;
    std::array<char, 0x100> action{};
};

// A prop's animation, keyed by its name or, in the older format, its legacy id.
struct SavedProp {
    std::variant<std::string, int> key;
    Prop::SavedAnimationState state{};
};

using SavedWorldSection = std::variant<std::vector<SavedTrigger>, std::vector<SavedProp>,
                                       SavedTriggerLocks, std::vector<SavedTriggerAction>>;

/* Decoders read one section's payload and throw on malformed input. A trigger section
   holds one record per trigger of the level that wrote it; applying it restores the
   records that name, or sit at the position of, one of this level's triggers. */
std::vector<SavedTrigger> DecodeSavedTriggers(W8Chunk& chunk, bool from_assets);
std::vector<SavedProp> DecodeSavedProps(W8Chunk& chunk);
SavedTriggerLocks DecodeSavedTriggerLocks(W8Chunk& chunk);
std::vector<SavedTriggerAction> DecodeSavedTriggerActions(W8Chunk& chunk);

/* What a save keeps of the world's triggers (TRIG), props (APST) and locks (LCKS), and the
   writers of each section, which reproduce the records they were decoded from. */
std::vector<SavedTrigger> CaptureSavedTriggers(const World& world);
std::vector<SavedProp> CaptureSavedProps(const World& world);
SavedTriggerLocks CaptureSavedTriggerLocks(const World& world);
void WriteSavedTriggers(wiz8::File& file, std::span<const SavedTrigger> section);
void WriteSavedProps(wiz8::File& file, std::span<const SavedProp> section);
void WriteSavedTriggerLocks(wiz8::File& file, const SavedTriggerLocks& section);
void WriteSavedTriggerActions(wiz8::File& file, std::span<const SavedTriggerAction> section);

// Apply a decoded section to the world; entries naming nothing in it are ignored.
void ApplySavedSection(World& world, SavedWorldSection& section);
void ApplySavedProps(World& world, const std::vector<SavedProp>& section);

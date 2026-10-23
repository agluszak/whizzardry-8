#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/entities/locks.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/saved_world_sections.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/random.h"
#include "wiz8/save_game.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

// Sets or clears one flag bit from a retail integer or float condition.
static void assignFlag(unsigned int& flags, unsigned int flag, bool on)
{
    if (on)
        flags |= flag;
    else
        flags &= ~flag;
}
template<std::size_t N> static void ReadTriggerText(wiz8::File& file, char (&text)[N])
{
    file.read_exact(text, N);
    if (std::find(std::begin(text), std::end(text), '\0') == std::end(text))
        throw std::runtime_error("unterminated trigger text field");
}

template<std::size_t N> static void ReadTriggerText(W8Chunk& file, char (&text)[N])
{
    file.ReadBytes(text, N);
    if (std::find(std::begin(text), std::end(text), '\0') == std::end(text))
        throw std::runtime_error("unterminated trigger text field");
}

const float g_trigger_progress_limit = 64.0f;

void SaveTriggerRuntimeStates(wiz8::File& handle)
{
    const auto& triggers = GetWorld()->triggers;
    const int version = 2;
    const int saved_count =
        static_cast<int>(std::ranges::count_if(triggers, [](const auto& trigger) {
            return trigger->lock_state.lock_type != LockType::none;
        }));
    const int restoring = 0;
    handle.write(&version, sizeof(version));
    handle.write(&saved_count, sizeof(saved_count));
    handle.write(&restoring, sizeof(restoring));

    for (const auto& trigger : triggers) {
        auto& lock = trigger->lock_state;
        if (lock.lock_type == LockType::none) continue;
        handle.write(trigger->name, 0x80);
        handle.write(&version, sizeof(version));
        handle.write(&lock.device_state.completed, sizeof(lock.device_state.completed));
        handle.write(lock.device_state.pins, sizeof(lock.device_state.pins));
        handle.write(&lock.lock_countdown, sizeof(lock.lock_countdown));
        handle.write(&lock.last_interaction_clock, sizeof(lock.last_interaction_clock));
    }
}

/* The runtime-state records written by SaveTriggerRuntimeStates. A record's form
   depends on the section: a restoring section, or one before version two, stores the
   lock as the level authored it. */
SavedTriggerLocks DecodeSavedTriggerLocks(W8Chunk& handle)
{
    const auto version = handle.ReadValue<int>();
    const auto saved_count = handle.ReadValue<int>();
    const auto restoring = handle.ReadValue<int>();
    SavedTriggerLocks section{.restoring = restoring != 0};
    for (int index = 0; index < saved_count; ++index) {
        auto& lock = section.locks.emplace_back();
        char name[0x80];
        ReadTriggerText(handle, name);
        lock.trigger = name;
        handle.ReadValue(lock.record_version);
        lock.full = restoring != 0 || version <= 1;
        if (!lock.full) {
            handle.ReadValue(lock.device_state.completed);
            handle.ReadBytes(lock.device_state.pins, sizeof(lock.device_state.pins));
            lock.lock_countdown = handle.ReadValue<int>();
            if (lock.record_version > 1) lock.last_interaction_clock = handle.ReadValue<int>();
        } else {
            lock.lock_type = static_cast<LockType>(handle.ReadValue<int>());
            handle.ReadValue(lock.difficulty);
            handle.ReadValue(lock.device_state.completed);
            handle.ReadBytes(lock.device_state.pins, sizeof(lock.device_state.pins));
            handle.ReadValue(lock.device_id);
            handle.ReadValue(lock.key_id);
            if (lock.record_version > 1) lock.lock_countdown = handle.ReadValue<int>();
        }
    }
    return section;
}

// A restored lock re-rolls when the section restores, and keeps its door's lock in step.
static void ApplySavedLocks(World& world, const SavedTriggerLocks& section)
{
    for (const auto& saved : section.locks) {
        auto* trigger = FindTriggerInWorld(world, saved.trigger);
        if (!trigger) continue;
        trigger->lock_state.Restore(saved);
        if (section.restoring) trigger->lock_state.Reset(world.rng());
        if (auto* door = trigger->doorData(); door && (!door->locked || door->item == -1)) {
            door->locked = trigger->lock_state.device_state.completed == 0;
            door->item = static_cast<short>(trigger->lock_state.key_id);
        }
    }
}

/* Serialize one trigger for the save file. The header carries a version byte
   and the action-state block; the action payload follows only when one is
   attached, with its flag bits packed and the timed-event delay resolved from
   the live event queue. I/O failures propagate to the staged save. */
// FUNCTION: WIZ8 0x0043BE60
void Trigger::Save(wiz8::File& hFile)
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    unsigned char version = 5;
    int trigger_count = static_cast<int>(GetWorld()->triggers.size());
    unsigned char has_action_data;
    unsigned char action_type;
    unsigned short action_flags;
    unsigned char action_kind;
    unsigned int progress_delay;
    unsigned char has_world_item;

    hFile.write(&version, sizeof(version));
    hFile.write(&trigger_count, sizeof(trigger_count));
    hFile.write(name, 0x80);
    hFile.write(&flags, sizeof(flags));
    hFile.write(&state_index, sizeof(state_index));
    hFile.write(&state_direction, sizeof(state_direction));
    hFile.write(&action, sizeof(action));
    hFile.write(&action_state, sizeof(action_state));
    hFile.write(&required_item_id, sizeof(required_item_id));
    has_action_data = hasActionPayload();
    hFile.write(&has_action_data, sizeof(has_action_data));
    if (has_action_data != 0) {
        action_type = static_cast<unsigned char>(actionPayloadKind());
        hFile.write(&action_type, sizeof(action_type));
        if (const auto* door = doorData()) {
            action_flags = 0;
            progress_delay = 0;
            action_kind = 2;
            hFile.write(&action_kind, sizeof(action_kind));
            action_flags = static_cast<decltype(action_flags)>(
                (door->open ? 1u : 0u) | (door->lockable ? 2u : 0u) | (door->locked ? 4u : 0u) |
                (door->auto_locking ? 8u : 0u) | (door->one_way ? 0x10u : 0u) |
                (door->secret ? 0x20u : 0u) | (door->found ? 0x40u : 0u) |
                (door->jammable ? 0x80u : 0u) | (door->jammed ? 0x100u : 0u));
            if (door->item != 0) {
                action_flags |= 0x200;
            }
            hFile.write(&action_flags, sizeof(action_flags));
            if (m_lData1 != 0 && timed_action != nullptr) {
                float progress = timed_action->timer.GetProgress();
                if (progress <= g_trigger_progress_limit) {
                    progress_delay = static_cast<unsigned int>(
                        timed_action->timer.GetProgress() * 1e+03f + 1.0f);
                } else {
                    progress_delay = 64000;
                }
            }
            hFile.write(&progress_delay, 2);
            {
                unsigned int item = static_cast<unsigned short>(door->item);
                hFile.write(&item, 2);
            }
        }
    }
    has_world_item = world_item_group != 0;
    hFile.write(&has_world_item, sizeof(has_world_item));
    if (has_world_item != 0) {
        SaveItemFile(hFile, *world_item_group);
    }
    hFile.write(&items_generated, sizeof(items_generated));
    hFile.write(&item_group_seed, sizeof(item_group_seed));
    hFile.write(&gold, sizeof(gold));
    hFile.write(&uses_remaining, sizeof(uses_remaining));
}

/* One door flag is stored as a byte whose low bit is the flag. */
static bool readDoorFlag(wiz8::File& file)
{
    unsigned char flag;
    file.read_exact(&flag, 1);
    return (flag & 1) != 0;
}

static bool readDoorFlag(W8Chunk& file)
{
    unsigned char flag = 0;
    file.ReadBytes(&flag, 1);
    return (flag & 1) != 0;
}

// Decode one trigger record of version `version`, which the record's tag gave.
static SavedTrigger DecodeSavedTrigger(W8Chunk& file, std::uint8_t version, bool from_assets)
{
    SavedTrigger saved{.version = version};
    file.ReadValue(saved.flags);
    file.ReadValue(saved.state_index);
    file.ReadValue(saved.state_direction);
    file.ReadValue(saved.action);
    file.ReadValue(saved.action_state);
    file.ReadValue(saved.required_item_id);
    if (file.ReadValue<unsigned char>() != 0) {
        saved.payload_kind = file.ReadValue<unsigned char>();
        if (*saved.payload_kind == W8_TRIGGER_PAYLOAD_DOOR) {
            auto& door = saved.door.emplace();
            if (file.ReadValue<unsigned char>() == 1) {
                door.open = readDoorFlag(file);
                door.lockable = readDoorFlag(file);
                door.locked = readDoorFlag(file);
                door.auto_locking = readDoorFlag(file);
                door.one_way = readDoorFlag(file);
                door.secret = readDoorFlag(file);
                door.found = readDoorFlag(file);
                door.jammable = readDoorFlag(file);
                door.jammed = readDoorFlag(file);
            } else {
                const auto flags = file.ReadValue<std::uint16_t>();
                door.packed = true;
                door.open = (flags & 1) != 0;
                door.lockable = (flags & 2) != 0;
                door.locked = (flags & 4) != 0;
                door.auto_locking = (flags & 8) != 0;
                door.one_way = (flags & 0x10) != 0;
                door.secret = (flags & 0x20) != 0;
                door.found = (flags & 0x40) != 0;
                door.jammable = (flags & 0x80) != 0;
                door.jammed = (flags & 0x100) != 0;
                file.ReadValue(door.progress_delay);
                file.ReadValue(door.item);
            }
        }
    }
    if (version > 1) {
        auto& loot = saved.loot.emplace();
        if (file.ReadValue<unsigned char>() != 0) loot.item_group = LoadItem(file, from_assets);
        file.ReadValue(loot.items_generated);
        file.ReadValue(loot.item_group_seed);
        file.ReadValue(loot.gold);
        file.ReadValue(loot.uses_remaining);
    }
    if (version == 3) {
        std::byte unused[0x1d];
        file.ReadBytes(unused, sizeof(unused));
    }
    return saved;
}

/* Restore the trigger from its saved record. A saved door's reopening delay re-queues
   its timed event with the saved progress. */
void Trigger::Restore(SavedTrigger saved)
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    flags = saved.flags;
    state_index = saved.state_index;
    state_direction = saved.state_direction;
    action = saved.action;
    action_state = saved.action_state;
    required_item_id = saved.required_item_id;
    if (saved.door) {
        const auto& door = *saved.door;
        auto& data = action_payload.emplace<W8DoorTriggerActionData>();
        data.open = door.open;
        data.lockable = door.lockable;
        data.locked = door.locked;
        data.auto_locking = door.auto_locking;
        data.one_way = door.one_way;
        data.secret = door.secret;
        data.found = door.found;
        data.jammable = door.jammable;
        data.jammed = door.jammed;
        if (door.packed) {
            if (m_lData1 != 0 && door.progress_delay != 0) {
                if (timed_action == 0) {
                    timed_action = &m_pWorld->addTimedAction(this);
                    timed_action->action = 2;
                    timed_action->timer.SetDuration(m_lData1 < 0 ? 10.0f
                                                                 : static_cast<float>(m_lData1));
                    timed_action->timer.Restart();
                    timed_action->repeat = true;
                }
                timed_action->timer.SetProgress(door.progress_delay * 0.001f);
            }
            data.item = static_cast<short>(door.item);
        }
    }
    if (saved.loot) {
        world_item_group = std::move(saved.loot->item_group);
        items_generated = saved.loot->items_generated;
        item_group_seed = saved.loot->item_group_seed;
        gold = saved.loot->gold;
        uses_remaining = saved.loot->uses_remaining;
    }
}

/* The trigger chunk's records, one per trigger the writer's level had. Tags 1-4 restore
   the trigger in the same position, tag 5 names its trigger, and anything else ends the
   walk. */
std::vector<SavedTrigger> DecodeSavedTriggers(W8Chunk& file, bool from_assets)
{
    std::vector<SavedTrigger> saved;
    while (file.RemainingBytes() > 0) {
        const auto tag = file.ReadValue<std::uint8_t>();
        // The dispatcher skips the remainder of this completed section.
        if (tag < 1 || tag > 5) break;
        std::optional<std::string> name;
        if (tag == 5) {
            file.ReadValue<int>(); // the writer's trigger count
            char text[128];
            ReadTriggerText(file, text);
            name = text;
        }
        saved.push_back(DecodeSavedTrigger(file, tag, from_assets));
        saved.back().name = std::move(name);
    }
    return saved;
}

static void ApplySavedTriggers(World& world, std::vector<SavedTrigger>& section)
{
    for (std::size_t index = 0; index < section.size(); ++index) {
        auto& saved = section[index];
        // A record past the level's triggers has nothing to restore.
        auto* trigger = saved.name                      ? FindTriggerInWorld(world, *saved.name)
                        : index < world.triggers.size() ? world.triggers[index].get()
                                                        : nullptr;
        if (trigger) trigger->Restore(std::move(saved));
    }
}

/* Write each trigger; failures propagate to the staged save. */
// FUNCTION: WIZ8 0x0043C810
void SaveWorldTriggers(wiz8::File& hFile)
{
    for (const auto& trigger : GetWorld()->triggers) trigger->Save(hFile);
}

void SaveTriggerActionData(wiz8::File& handle)
{
    int trigger_count = static_cast<int>(GetWorld()->triggers.size());
    int saved_count = 0;
    int index;
    int version = 1;

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = GetWorld()->triggers[index].get();
        if (trigger->inline_action_data[0] != '\0') {
            ++saved_count;
        }
    }

    handle.write(&version, sizeof(version));
    handle.write(&saved_count, sizeof(saved_count));

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = GetWorld()->triggers[index].get();
        if (trigger->inline_action_data[0] != '\0') {
            handle.write(trigger->name, 0x80);
            handle.write(trigger->inline_action_data, sizeof(trigger->inline_action_data));
        }
    }
}

/* The trigger action-data chunk written by SaveTriggerActionData: a version/count
   header, then per record the trigger name and its 0x100-byte payload. */
std::vector<SavedTriggerAction> DecodeSavedTriggerActions(W8Chunk& handle)
{
    handle.ReadValue<std::int32_t>(); // version
    const auto count = handle.ReadValue<std::int32_t>();
    if (count < 0 || count > handle.RemainingBytes() / 384)
        throw std::runtime_error("invalid trigger action count");
    std::vector<SavedTriggerAction> saved(count);
    for (auto& record : saved) {
        char name[128];
        ReadTriggerText(handle, name);
        record.trigger = name;
        handle.ReadBytes(record.action.data(), record.action.size());
        if (std::ranges::find(record.action, '\0') == record.action.end())
            throw std::runtime_error("unterminated trigger action");
    }
    return saved;
}

static void ApplySavedTriggerActions(World& world, const std::vector<SavedTriggerAction>& section)
{
    for (const auto& saved : section)
        if (auto* trigger = FindTriggerInWorld(world, saved.trigger))
            std::ranges::copy(saved.action, trigger->inline_action_data);
}

void ApplySavedSection(World& world, SavedWorldSection& section)
{
    std::visit(
        [&](auto& value) {
            using Section = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Section, std::vector<SavedTrigger>>)
                ApplySavedTriggers(world, value);
            else if constexpr (std::is_same_v<Section, std::vector<SavedProp>>)
                ApplySavedProps(world, value);
            else if constexpr (std::is_same_v<Section, SavedTriggerLocks>)
                ApplySavedLocks(world, value);
            else
                ApplySavedTriggerActions(world, value);
        },
        section);
}

W8DoorTriggerActionData ReadDoorTriggerActionData(wiz8::File* handle)
{
    W8DoorTriggerActionData data{};

    unsigned char version;
    unsigned short item;
    unsigned char has_position;
    glm::vec3 position;
    char linked_trigger[0x80];
    handle->read_exact(&version, 1);
    data.open = readDoorFlag(*handle);
    data.lockable = readDoorFlag(*handle);
    data.locked = readDoorFlag(*handle);
    data.auto_locking = readDoorFlag(*handle);
    data.one_way = readDoorFlag(*handle);
    data.secret = readDoorFlag(*handle);
    data.found = readDoorFlag(*handle);
    data.jammable = readDoorFlag(*handle);
    data.jammed = readDoorFlag(*handle);
    handle->read_exact(&item, 2);
    handle->read_exact(&has_position, 1);
    wiz8::retail::read_exact(*handle, position);
    position *= 500.0f;
    ReadTriggerText(*handle, linked_trigger);

    data.item = item;
    strcpy(data.linked_trigger, linked_trigger);
    if (has_position != 0) {
        data.position = position;
        data.has_position = true;
    }
    return data;
}

Trigger* Trigger::CreateAndLoadLevelTrigger(wiz8::File* handle, World* world)
{
    std::string recipient_text;
    /* Retail read these uninitialised when the FileRead chain short-circuited;
       natively they start at zero. */
    std::unique_ptr<Trigger> owner;
    Trigger* trigger = nullptr;
    unsigned char record_version = 0;
    unsigned char record_type = 0;
    assert(handle != 0);
    handle->read_exact(&record_version, 1);
    handle->read_exact(&record_type, 1);
    if (record_type < 1 || record_type > 4) throw std::runtime_error("invalid level trigger type");
    if (record_type != 3) {
        owner = std::make_unique<Trigger>(world);
        trigger = owner.get();
        trigger->trigger_id = world->next_trigger_id++;
        trigger->m_pWorld = world;
        trigger->flags = (trigger->flags & ~0x20U) | W8_TRIGGER_ON;
    }

    switch (record_type) {
    case 1: {
        unsigned char version;
        int byte0;
        int byte1;
        float animate_states;
        int range;
        int action;
        int value_ac;
        int animate_action;
        unsigned char packed_flags;
        unsigned char enabled;
        float minimum_range = 0.0f;
        int action_value = 0;
        char recipients[0x100];
        char sound[0x80];

        handle->read_exact(&version, 1);
        handle->read_exact(&byte0, 4);
        handle->read_exact(&byte1, 4);
        handle->read_exact(&animate_states, 4);
        handle->read_exact(&range, 4);
        handle->read_exact(&action, 4);
        handle->read_exact(&value_ac, 4);
        handle->read_exact(&animate_action, 4);
        handle->read_exact(&packed_flags, 1);
        handle->read_exact(&enabled, 1);
        ReadTriggerText(*handle, trigger->name);
        ReadTriggerText(*handle, recipients);
        ReadTriggerText(*handle, sound);
        if (SDL_snprintf(trigger->action_data, sizeof(trigger->action_data), "data\\sound\\%s",
                         sound) >= static_cast<int>(sizeof(trigger->action_data)))
            throw std::runtime_error("trigger sound path is too long");
        SDL_strupr(trigger->name);
        SDL_strupr(recipients);

        if (version > 1) {
            char surface_id[0x40];
            int id = -1;

            handle->read_exact(&minimum_range, 4);
            handle->read_exact(surface_id, sizeof(surface_id));
            /* The id is the four characters after a leading NUL: retail stores
               a terminator at surface_id[5] before atoi. Retail 0x00441c99
               only requires the world's game data, not its build-time
               geometry index (null once a level runs from its octree). */
            if (surface_id[0] == 0) {
                if (world->collisionIndex() != 0) {
                    surface_id[5] = 0;
                    id = atoi(surface_id + 1);
                }
            } else {
                id = -1;
            }
            trigger->surface_id = id;
        }
        if (version > 2) {
            unsigned char has_action_data;
            handle->read_exact(&has_action_data, 1);
            if (has_action_data != 0) {
                unsigned char action_data_kind;
                handle->read_exact(&action_data_kind, 1);
                if (action_data_kind == 1) {
                    const auto door = ReadDoorTriggerActionData(handle);
                    trigger->state_index = door.open;
                    trigger->action_payload = door;
                }
            }
        }
        if (version > 3) {
            handle->read_exact(&action_value, 4);
        }

        trigger->trigger_kind = W8TriggerKind::item;
        trigger->range_maximum = range * 500.0f;
        trigger->range_minimum = minimum_range * 500.0f;
        recipient_text = recipients;
        trigger->state_count = static_cast<unsigned char>(byte1);
        trigger->state_index = 0;
        trigger->state_direction = W8TriggerStateDirection::forward;
        trigger->cycle_bounce = byte0;
        trigger->action_value = action_value;
        trigger->initial_action = action;
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_STATES, animate_states != 0.0f);
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_ACTION, animate_action != 0);
        assignFlag(trigger->flags, W8_TRIGGER_ENABLED, enabled != 0);
        assignFlag(trigger->flags, W8_TRIGGER_LINK_ON_DEACTIVATE, (packed_flags & 2) != 0);
        assignFlag(trigger->flags, W8_TRIGGER_FIRE_LINKED, (packed_flags & 1) != 0);
        break;
    }

    case 2: {
        unsigned char version;
        float range;
        float x;
        float y;
        float z;
        int action;
        int value_c8;
        unsigned char fire_linked;
        unsigned char enabled;
        char recipients[0x100];
        unsigned char plane = 0;
        unsigned char keep_on_finish = 0;
        int value_ac = 0;

        handle->read_exact(&version, 1);
        handle->read_exact(&range, 4);
        handle->read_exact(&x, 4);
        handle->read_exact(&y, 4);
        handle->read_exact(&z, 4);
        handle->read_exact(&action, 4);
        handle->read_exact(&value_c8, 4);
        handle->read_exact(&fire_linked, 1);
        handle->read_exact(&enabled, 1);
        ReadTriggerText(*handle, trigger->name);
        ReadTriggerText(*handle, recipients);
        SDL_strupr(trigger->name);
        SDL_strupr(recipients);
        if (version > 1) {
            handle->read_exact(&plane, 1);
            for (auto& point : trigger->representation_vectors)
                wiz8::retail::read_exact(*handle, point);
            for (int vector = 0; vector < 4; ++vector) {
                trigger->representation_vectors[vector] *= 500.0f;
            }
        }
        if (version > 2) {
            unsigned char unused;
            char action_string[0x80];
            handle->read_exact(&trigger->angle, 4);
            handle->read_exact(&trigger->direction.x, 4);
            handle->read_exact(&trigger->direction.y, 4);
            handle->read_exact(&trigger->direction.z, 4);
            handle->read_exact(&unused, 1);
            ReadTriggerText(*handle, action_string);
            if (action == 17) {
                trigger->action_payload = W8StringTriggerActionData{action_string};
            }
        }
        if (version > 3) {
            handle->read_exact(&keep_on_finish, 1);
            handle->read_exact(&value_ac, 4);
        }
        if (version > 4) {
            unsigned char has_legacy_geometry;
            handle->read_exact(&has_legacy_geometry, 1);
            if (has_legacy_geometry != 0) {
                unsigned char geometry_kind;
                handle->read_exact(&geometry_kind, 1);
                if (geometry_kind == 2) {
                    unsigned char count;
                    wiz8::retail::Vec3f legacy_vertices[36];
                    unsigned char legacy_flags[2];
                    handle->read_exact(&count, 1);
                    for (int index = 0; index < 36; ++index) {
                        handle->read_exact(&legacy_vertices[index], sizeof(legacy_vertices[index]));
                    }
                    handle->read_exact(&legacy_flags[0], 1);
                    handle->read_exact(&legacy_flags[1], 1);
                }
            }
        }

        trigger->trigger_kind = W8TriggerKind::prop;
        trigger->position = glm::vec3(x * 500.0f, y * 500.0f, z * 500.0f);
        trigger->range_maximum = range * 500.0f;
        trigger->m_bRepType = W8_TRIGGER_REP_POSITION;
        trigger->action_value = value_ac;
        trigger->initial_action = static_cast<unsigned short>(action);
        trigger->searchable = value_c8;
        trigger->flags |= W8_TRIGGER_POSITIONED;
        assignFlag(trigger->flags, W8_TRIGGER_FIRE_LINKED, fire_linked != 0);
        assignFlag(trigger->flags, W8_TRIGGER_ENABLED, enabled != 0);
        assignFlag(trigger->flags, W8_TRIGGER_KEEP_ON_FINISH, keep_on_finish != 0);
        if (plane == 1) trigger->flags |= W8_TRIGGER_PLANE;
        recipient_text = recipients;
        /* Retail 0x00442483 hands plane triggers to the game data whenever it
           exists; AddTriggerPlane itself picks the octree or build path. */
        if ((trigger->flags & W8_TRIGGER_PLANE) != 0 && world->collisionIndex() != 0) {
            world->collisionIndex()->AddTriggerPlane(trigger->representation_vectors, trigger);
        }
        if (trigger->initial_action == 0x34 && recipient_text.empty()) {
            trigger->range_minimum = 0.0f;
            trigger->range_maximum = 0.0f;
        }
        break;
    }

    case 3: {
        unsigned char version;
        int volume_min, volume_max, speed_min, speed_max, time_min, time_max;
        int unbounded;
        float radius;
        glm::vec3 position, region_u, region_v;
        glm::vec3 region_center;
        float region_angle = 0.0f;
        glm::vec3 region_min;
        glm::vec3 region_max;
        char wave[0x80];
        char name[0x80];
        const char* optional_name = 0;
        unsigned char looping = 0;
        unsigned char shared = 0;

        region_center.x = region_center.y = region_center.z = 0.0f;
        region_min.x = region_min.y = region_min.z = 0.0f;
        region_max.x = region_max.y = region_max.z = 0.0f;

        handle->read_exact(&version, 1);
        handle->read_exact(&volume_min, 4);
        handle->read_exact(&volume_max, 4);
        handle->read_exact(&speed_min, 4);
        handle->read_exact(&speed_max, 4);
        handle->read_exact(&time_min, 4);
        handle->read_exact(&time_max, 4);
        handle->read_exact(&unbounded, 4);
        handle->read_exact(&radius, 4);
        wiz8::retail::read_exact(*handle, position);
        handle->read_exact(&region_u, sizeof(region_u));
        handle->read_exact(&region_v, sizeof(region_v));
        ReadTriggerText(*handle, wave);
        W8AmbientSoundConfig config;
        if (SDL_snprintf(config.wave_name, sizeof(config.wave_name), "data\\sound\\%s", wave) >=
            static_cast<int>(sizeof(config.wave_name)))
            throw std::runtime_error("ambient sound path is too long");
        if (version > 1) {
            unsigned char has_position;
            handle->read_exact(&has_position, 1);
            handle->read_exact(&looping, 1);
        }
        if (version > 2) {
            handle->read_exact(&region_center, sizeof(region_center));
            handle->read_exact(&region_angle, 4);
            handle->read_exact(&region_min, sizeof(region_min));
            handle->read_exact(&region_max, sizeof(region_max));
            region_center *= 500.0f;
        }
        if (version > 3) {
            ReadTriggerText(*handle, name);
            optional_name = name;
        }
        if (version > 4) {
            handle->read_exact(&shared, 1);
        }
        position *= 500.0f;
        region_u *= 500.0f;
        region_v *= 500.0f;
        radius *= 500.0f;
        AddAmbientSound(world, optional_name, &config, &position, &region_u, &region_v, volume_min,
                        volume_max, speed_min, speed_max, time_min, time_max, radius,
                        unbounded == 0, looping, &region_center, region_angle, &region_min,
                        &region_max, shared);
        return 0;
    }

    case 4: {
        unsigned char version;
        unsigned char packed_flags;
        unsigned char enabled;
        unsigned char fire_linked;
        unsigned char link_on_deactivate;
        unsigned char keep_on_finish;
        unsigned char flag;
        unsigned char alternate_toggles;
        int initial_action;
        int alternate_action;
        int fallback_action;
        char recipients[0x100];
        unsigned char searchable;
        char location_variable[0x100];
        unsigned char consume_item;
        int action_value;
        unsigned char animate_action;
        char sound[0x80];
        float representation_scale = 1.0f;
        unsigned char initial_location_value = 0;
        unsigned char flag0 = 0;
        unsigned char representation_kind = 0;

        handle->read_exact(&version, 1);
        ReadTriggerText(*handle, trigger->name);
        handle->read_exact(&packed_flags, 1);
        handle->read_exact(&enabled, 1);
        handle->read_exact(&fire_linked, 1);
        handle->read_exact(&link_on_deactivate, 1);
        handle->read_exact(&keep_on_finish, 1);
        handle->read_exact(&flag, 1);
        handle->read_exact(&alternate_toggles, 1);
        handle->read_exact(&initial_action, 4);
        handle->read_exact(&alternate_action, 4);
        handle->read_exact(&fallback_action, 4);
        ReadTriggerText(*handle, recipients);
        handle->read_exact(&searchable, 1);
        ReadTriggerText(*handle, location_variable);
        handle->read_exact(&consume_item, 1);
        handle->read_exact(&action_value, 4);
        handle->read_exact(&animate_action, 1);
        ReadTriggerText(*handle, sound);
        if (SDL_snprintf(trigger->action_data, sizeof(trigger->action_data), "data\\sound\\%s",
                         sound) >= static_cast<int>(sizeof(trigger->action_data)))
            throw std::runtime_error("trigger sound path is too long");
        SDL_strupr(trigger->name);
        SDL_strupr(recipients);
        SDL_strupr(location_variable);

        if (version > 1) {
            handle->read_exact(&trigger->m_lData1, 4);
            handle->read_exact(&trigger->m_lData2, 4);
            handle->read_exact(&trigger->m_lData3, 4);
            handle->read_exact(&representation_scale, 4);
            handle->read_exact(&initial_location_value, 1);
            handle->read_exact(&flag0, 1);
            signed char action_data_mode;
            handle->read_exact(&action_data_mode, 1);
            trigger->action_data_mode = static_cast<W8TriggerActionDataMode>(action_data_mode);
            handle->read_exact(&trigger->sound_volume, 1);
            int unused;
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
        }

        trigger->trigger_kind = ((packed_flags & 1) != 0 || searchable == 1) ? W8TriggerKind::item
                                                                             : W8TriggerKind::prop;
        if ((packed_flags & 2) != 0) trigger->flags |= W8_TRIGGER_CAN_RUN_LINKED;
        if ((packed_flags & 4) != 0) trigger->flags |= W8_TRIGGER_ONCE;
        if ((packed_flags & 8) != 0) trigger->flags |= W8_TRIGGER_EXCLUSIVE;
        if ((packed_flags & 0x10) != 0) trigger->flags |= W8_TRIGGER_REACTIVATE_LINKED;
        if ((packed_flags & 0x20) != 0) trigger->flags |= 0x400000;

        if (recipients[0] != 0) {
            recipient_text = recipients;
        }
        trigger->required_item_id = location_variable[0] == 0 ? -1 : atoi(location_variable);
        trigger->alternate_action = static_cast<unsigned short>(alternate_action);
        trigger->initial_action = static_cast<unsigned short>(initial_action);
        trigger->fallback_action = static_cast<unsigned short>(fallback_action);
        trigger->searchable = searchable;
        assignFlag(trigger->flags, W8_TRIGGER_ENABLED, enabled != 0);
        assignFlag(trigger->flags, W8_TRIGGER_FIRE_LINKED, fire_linked != 0);
        assignFlag(trigger->flags, W8_TRIGGER_LINK_ON_DEACTIVATE, link_on_deactivate != 0);
        assignFlag(trigger->flags, W8_TRIGGER_KEEP_ON_FINISH, keep_on_finish != 0);
        assignFlag(trigger->flags, 0x1000, flag != 0);
        assignFlag(trigger->flags, W8_TRIGGER_CONSUME_ITEM, consume_item != 0);
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_ACTION, animate_action != 0);
        assignFlag(trigger->flags, W8_TRIGGER_ALTERNATE_TOGGLES, alternate_toggles != 0);
        if (trigger->alternate_action != 0) trigger->flags |= W8_TRIGGER_HAS_ALTERNATE;

        unsigned char value_b0;
        unsigned char animate_states;
        unsigned char value_b3;
        char required_states[0x100];
        char state_to_modify[0x100];
        unsigned char value_b4;
        handle->read_exact(&value_b0, 1);
        handle->read_exact(&animate_states, 1);
        handle->read_exact(&value_b3, 1);
        ReadTriggerText(*handle, required_states);
        ReadTriggerText(*handle, state_to_modify);
        handle->read_exact(&value_b4, 1);
        SDL_strupr(required_states);
        SDL_strupr(state_to_modify);
        if (required_states[0] != 0) {
            trigger->required_states = ParseTriggerNames(required_states);
        }
        if (state_to_modify[0] != 0) {
            trigger->state_to_modify = state_to_modify;
        }
        trigger->state_count = value_b0;
        trigger->cycle_bounce = value_b3;
        trigger->state_mod_mode = static_cast<W8TriggerStateModificationMode>(value_b4);
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_STATES, animate_states != 0);

        if (!trigger->state_to_modify.empty() &&
            (initial_location_value == 0 || initial_location_value == 1)) {
            trigger->initial_location_value = initial_location_value;
        }

        int unused_value;
        handle->read_exact(&trigger->range_minimum, 4);
        handle->read_exact(&trigger->range_maximum, 4);
        ReadTriggerText(*handle, trigger->inline_action_data);
        handle->read_exact(&unused_value, 4);
        SDL_strupr(trigger->inline_action_data);
        trigger->range_minimum *= 500.0f;
        trigger->range_maximum *= 500.0f;
        if (version > 2) {
            ReadTriggerText(*handle, sound);
            sprintf(trigger->alternate_action_data, "data\\sound\\%s", sound);
            assignFlag(trigger->flags, W8_TRIGGER_ALTERNATE_ACTION, flag0 != 0);
        }

        if ((packed_flags & 1) == 0) {
            handle->read_exact(&representation_kind, 1);
            if (representation_kind == 1) {
                wiz8::retail::read_exact(*handle, trigger->position);
                handle->read_exact(&trigger->angle, 4);
                wiz8::retail::read_exact(*handle, trigger->direction);
                trigger->position *= 500.0f;
                trigger->flags |= W8_TRIGGER_POSITIONED;
            } else if (representation_kind == 2) {
                for (auto& point : trigger->representation_vectors)
                    wiz8::retail::read_exact(*handle, point);
                for (int vector = 0; vector < 4; ++vector) {
                    trigger->representation_vectors[vector] *= 500.0f;
                }
            }
            unsigned char has_legacy_action;
            handle->read_exact(&has_legacy_action, 1);
            if (has_legacy_action != 0) {
                unsigned char legacy_kind;
                float legacy_value;
                handle->read_exact(&legacy_kind, 1);
                handle->read_exact(&legacy_value, 4);
                ReadTriggerText(*handle, sound);
            }
        }

        unsigned char has_action_data;
        handle->read_exact(&has_action_data, 1);
        if (has_action_data != 0) {
            unsigned char action_data_kind;
            handle->read_exact(&action_data_kind, 1);
            if (action_data_kind == 1) {
                const auto door = ReadDoorTriggerActionData(handle);
                trigger->state_index = door.open;
                trigger->action_payload = door;
            } else if (action_data_kind == 2) {
                unsigned char count;
                wiz8::retail::Vec3f legacy_vertices[36];
                unsigned char legacy_flags[2];
                handle->read_exact(&count, 1);
                for (int index = 0; index < 36; ++index) {
                    handle->read_exact(&legacy_vertices[index], sizeof(legacy_vertices[index]));
                }
                handle->read_exact(&legacy_flags[0], 1);
                handle->read_exact(&legacy_flags[1], 1);
            }
        }

        /* Retail 0x0044342d: the legacy representation path has the same
           game-data-only guard. */
        if (representation_kind == 2 && world->collisionIndex() != 0) {
            world->collisionIndex()->AddTriggerPlane(trigger->representation_vectors, trigger);
        }
        if (trigger->initial_action == 0x34 && recipient_text.empty()) {
            trigger->range_minimum = 0.0f;
            trigger->range_maximum = 0.0f;
        }
        if (trigger->initial_action == 0x0c) {
            if (trigger->m_lData1 < 0 ||
                trigger->m_lData1 >= static_cast<int>(Data().missiles.size()))
                throw std::runtime_error(fmt::format("trigger {}: invalid missile {}",
                                                     trigger->name, trigger->m_lData1));
            if (trigger->m_lData2 == -1)
                throw std::runtime_error(
                    fmt::format("trigger {}: missile trigger needs a time", trigger->name));
            if (trigger->m_lData2 < 0) {
                auto& timed_action = world->addTimedAction(trigger);
                timed_action.action = static_cast<short>(trigger->initial_action);
                timed_action.timer.SetDuration(abs(trigger->m_lData2) * 0.001f);
                timed_action.timer.Restart();

                trigger->flags |= W8_TRIGGER_RUNNING;
            }
        } else if (trigger->initial_action > 0x24 && trigger->initial_action < 0x2c &&
                   trigger->m_lData1 >= 0) {
            trigger->uses_remaining = trigger->m_lData1;
        }
        break;
    }
    }
    trigger->recipients = ParseTriggerNames(recipient_text);
    trigger->recipient_group = atoi(recipient_text.c_str());
    for (const auto action :
         {trigger->initial_action, trigger->alternate_action, trigger->fallback_action}) {
        if (action == 9 || action == 0x0e || action == 0x34 || action == 0x3f) {
            trigger->recipient_name = recipient_text;
            break;
        }
    }
    world->triggers.push_back(std::move(owner));
    return trigger;
}

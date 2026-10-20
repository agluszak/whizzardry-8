#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/entities/locks.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
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

/* Read the runtime-state records written by SaveTriggerRuntimeStates.
   Each record names its trigger; a record whose trigger no longer exists is
   still consumed through a scratch trigger so the stream stays aligned. When
   restoring, a state byte block is re-randomized and the type-10 action data is
   re-linked to the stored item. */
bool LoadTriggerRuntimeStates(World& world, W8Chunk& handle)
try {
    int version;
    int saved_count;
    int restoring;
    int index = 0;

    handle.ReadBytes(&version, sizeof(version));
    handle.ReadBytes(&saved_count, sizeof(saved_count));
    handle.ReadBytes(&restoring, sizeof(restoring));
    if (saved_count < 1) {
        return true;
    }
    for (;;) {
        char name[0x80];
        Trigger* trigger;

        ReadTriggerText(handle, name);
        trigger = FindTriggerInWorld(world, name);
        if (trigger == 0) {
            Trigger scratch(&world);
            scratch.lock_state.ReadRuntimeRecord(handle, version, restoring);
        } else {
            trigger->lock_state.ReadRuntimeRecord(handle, version, restoring);
            if (restoring != 0) {
                trigger->lock_state.Reset();
            }
            if (auto* door = trigger->doorData(); door && (!door->locked || door->item == -1)) {
                door->locked = trigger->lock_state.device_state.completed == 0;
                door->item = static_cast<short>(trigger->lock_state.key_id);
            }
        }
        ++index;
        if (saved_count <= index) {
            return true;
        }
    }
} catch (const std::exception&) {
    return false;
}

/* Serialize one trigger for the save file. The header carries a version byte
   and the action-state block; the action payload follows only when one is
   attached, with its flag bits packed and the timed-event delay resolved from
   the live event queue. I/O failures propagate to the staged save. */
// FUNCTION: WIZ8 0x0043BE60
void Trigger::Save(wiz8::File& hFile)
{
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
            if (m_lData1 != 0 && m_pEvent != 0 &&
                std::ranges::contains(m_pWorld->timed_events, m_pEvent.get())) {
                float progress = m_pEvent->timer.GetProgress();
                if (progress <= g_trigger_progress_limit) {
                    progress_delay =
                        static_cast<unsigned int>(m_pEvent->timer.GetProgress() * 1e+03f + 1.0f);
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

/* Read one trigger back from the save file. The version byte selects how much
   of the trailing block is present; a type-10 action payload rebuilds its
   action data and re-queues the delayed timed event from the saved progress. */
bool Trigger::Load(W8Chunk& hFile, char version, bool from_assets)
try {
    unsigned char has_action_data;
    unsigned char action_type;
    unsigned char flag_mode;

    hFile.ReadValue(flags);
    hFile.ReadValue(state_index);
    hFile.ReadValue(state_direction);
    hFile.ReadValue(action);
    hFile.ReadValue(action_state);
    hFile.ReadValue(required_item_id);
    hFile.ReadBytes(&has_action_data, sizeof(has_action_data));
    if (has_action_data != 0) {
        hFile.ReadBytes(&action_type, sizeof(action_type));
        if (action_type == W8_TRIGGER_PAYLOAD_DOOR) {
            auto* pDoor = &action_payload.emplace<W8DoorTriggerActionData>();
            unsigned short action_flags;
            unsigned short progress_delay;
            unsigned short item;

            hFile.ReadBytes(&flag_mode, 1);
            if (flag_mode == 1) {
                pDoor->open = readDoorFlag(hFile);
                pDoor->lockable = readDoorFlag(hFile);
                pDoor->locked = readDoorFlag(hFile);
                pDoor->auto_locking = readDoorFlag(hFile);
                pDoor->one_way = readDoorFlag(hFile);
                pDoor->secret = readDoorFlag(hFile);
                pDoor->found = readDoorFlag(hFile);
                pDoor->jammable = readDoorFlag(hFile);
                pDoor->jammed = readDoorFlag(hFile);
            } else {
                action_flags = 0;
                progress_delay = 0;
                hFile.ReadBytes(&action_flags, 2);
                pDoor->open = (action_flags & 1) != 0;
                pDoor->lockable = (action_flags & 2) != 0;
                pDoor->locked = (action_flags & 4) != 0;
                pDoor->auto_locking = (action_flags & 8) != 0;
                pDoor->one_way = (action_flags & 0x10) != 0;
                pDoor->secret = (action_flags & 0x20) != 0;
                pDoor->found = (action_flags & 0x40) != 0;
                pDoor->jammable = (action_flags & 0x80) != 0;
                pDoor->jammed = (action_flags & 0x100) != 0;
                hFile.ReadBytes(&progress_delay, 2);
                if (m_lData1 != 0 && progress_delay != 0) {
                    if (m_pEvent == 0) {
                        float duration;

                        m_pEvent = std::make_unique<TriggerEvent>();
                        m_pEvent->trigger = this;
                        m_pEvent->action = 2;
                        if (m_lData1 < 0) {
                            duration = 10.0f;
                        } else {
                            duration = static_cast<float>(m_lData1);
                        }
                        m_pEvent->timer.SetDuration(duration);
                        m_pEvent->timer.Restart();
                        m_pEvent->repeat = true;
                    }
                    m_pEvent->timer.SetProgress(progress_delay * 0.001f);
                    if (!std::ranges::contains(m_pWorld->timed_events, m_pEvent.get())) {
                        m_pWorld->timed_events.push_back(m_pEvent.get());
                    }
                }
                hFile.ReadBytes(&item, 2);
                pDoor->item = static_cast<short>(item);
            }
        }
    }

    if (version > 1) {
        unsigned char has_world_item;

        hFile.ReadBytes(&has_world_item, sizeof(has_world_item));
        if (has_world_item != 0) {
            world_item_group = LoadItem(hFile, from_assets);
        }
        hFile.ReadBytes(&items_generated, sizeof(items_generated));
        hFile.ReadBytes(&item_group_seed, sizeof(item_group_seed));
        hFile.ReadBytes(&gold, sizeof(gold));
        hFile.ReadBytes(&uses_remaining, sizeof(uses_remaining));
    }
    if (version == 3) {
        std::byte unused[0x1d];
        hFile.ReadBytes(unused, sizeof(unused));
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Read every trigger record of the save file's trigger chunk. Tags 1-4 update
   the matching in-place trigger, tag 5 names its trigger and is consumed
   through a scratch trigger when the name is gone, and anything else pushes
   the tag byte back and ends the walk. */
bool LoadWorldTriggers(World* world, W8Chunk& file, bool from_assets)
try {
    for (std::size_t index = 0; index < world->triggers.size(); ++index) {
        char tag;
        file.ReadBytes(&tag, 1);
        if (tag < 1 || tag > 5) {
            // The dispatcher skips the remainder of this completed section.
            return true;
        }
        Trigger* trigger = world->triggers[index].get();
        if (tag == 5) {
            int trigger_id;
            char name[128];
            file.ReadBytes(&trigger_id, sizeof(trigger_id));
            ReadTriggerText(file, name);
            trigger = FindTriggerInWorld(*world, name);
        }
        if (trigger) {
            if (!trigger->Load(file, tag, from_assets)) return false;
        } else {
            Trigger scratch(world);
            if (!scratch.Load(file, tag, from_assets)) return false;
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
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

/* Read the trigger action-data chunk written by SaveTriggerActionData:
   a version/count header, then per record the trigger name and its 0x100-byte
   payload. Records for missing triggers are skipped with a seek. */
bool LoadTriggerActionData(World& world, W8Chunk& handle)
try {
    std::int32_t version, count;
    handle.ReadBytes(&version, sizeof(version));
    handle.ReadBytes(&count, sizeof(count));
    if (count < 0 || count > (handle.RemainingBytes()) / 384) return false;
    std::vector<std::pair<Trigger*, std::array<char, 256>>> pending;
    pending.reserve(count);
    for (std::int32_t index = 0; index < count; ++index) {
        char name[128];
        std::array<char, 256> action;
        ReadTriggerText(handle, name);
        handle.ReadBytes(action.data(), action.size());
        if (std::find(action.begin(), action.end(), '\0') == action.end()) return false;
        pending.emplace_back(FindTriggerInWorld(world, name), action);
    }
    for (const auto& [trigger, action] : pending)
        if (trigger) std::copy(action.begin(), action.end(), trigger->inline_action_data);
    return true;
} catch (const std::exception&) {
    return false;
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
    /* Retail read these uninitialised when the FileRead chain short-circuited;
       natively they start at zero. */
    std::unique_ptr<Trigger> owner;
    Trigger* trigger = nullptr;
    unsigned char record_version = 0;
    unsigned char record_type = 0;
    if (handle == 0) {
        Fail("hFile");
    }
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
        trigger->recipients = recipients;
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
        trigger->recipients = recipients;
        /* Retail 0x00442483 hands plane triggers to the game data whenever it
           exists; AddTriggerPlane itself picks the octree or build path. */
        if ((trigger->flags & W8_TRIGGER_PLANE) != 0 && world->collisionIndex() != 0) {
            world->collisionIndex()->AddTriggerPlane(trigger->representation_vectors, trigger);
        }
        if (trigger->initial_action == 0x34 && trigger->recipients.empty()) {
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
            trigger->recipients = recipients;
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
            trigger->required_states = required_states;
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
            CreateLocationVar(trigger->state_to_modify.c_str(), initial_location_value);
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
        if (trigger->initial_action == 0x34 && trigger->recipients.empty()) {
            trigger->range_minimum = 0.0f;
            trigger->range_maximum = 0.0f;
        }
        if (trigger->initial_action == 0x0c) {
            if (trigger->m_lData1 < 0 ||
                trigger->m_lData1 >= static_cast<int>(Data().missiles.size())) {
                Fail("((pTrigger->m_lData1 >= 0) && (pTrigger->m_lData1 < "
                     "Missile::GetNumTypes()))",
                     fmt::format("Trigger {}: You must enter a valid missile number",
                                 trigger->name));
            }
            if (trigger->m_lData2 == -1) {
                Fail(
                    "(pTrigger->m_lData2!=(-1))",
                    fmt::format("Trigger {}: You must enter a time value in Data2", trigger->name));
            }
            if (trigger->m_lData2 < 0) {
                trigger->m_pEvent = std::make_unique<TriggerEvent>();
                trigger->m_pEvent->action = static_cast<short>(trigger->initial_action);
                trigger->m_pEvent->timer.SetDuration(abs(trigger->m_lData2) * 0.001f);
                trigger->m_pEvent->timer.Restart();
                trigger->m_pEvent->trigger = trigger;
                trigger->flags |= W8_TRIGGER_RUNNING;
                world->timed_events.push_back(trigger->m_pEvent.get());
            }
        } else if (trigger->initial_action > 0x24 && trigger->initial_action < 0x2c &&
                   trigger->m_lData1 >= 0) {
            trigger->uses_remaining = trigger->m_lData1;
        }
        break;
    }
    }
    world->triggers.push_back(std::move(owner));
    return trigger;
}

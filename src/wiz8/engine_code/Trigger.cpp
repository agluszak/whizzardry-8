#include "wiz8/engine_code/Trigger.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Item.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "dice.h"
#include "game_timer.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "layouts/item_instance.h"
#include "layouts/item_tables.h"
#include "random.h"
#include "soundman.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "surrender/srMath.h"
#include "wiz8/chunk.h"
#include "wiz8/containers.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/save_game.h"
#include "wiz8/spell_ids.h"
#include "wiz8/startup_world.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

// Sets or clears one flag bit from a retail integer or float condition.
static void assignFlag(unsigned int& flags, unsigned int flag, bool on)
{
    if (on)
        flags |= flag;
    else
        flags &= ~flag;
}
#include <SDL3/SDL_stdinc.h>

#include "layouts/world.h"
#include "surrender/srNode.h"
#include "wiz8/gameplay_constants.h"

class W8DialogBase;

static std::string NextTriggerRecipient(const char*& cursor)
{
    if (!cursor) return {};
    const char* comma = std::strchr(cursor, ',');
    std::string name(cursor,
                     comma ? static_cast<std::size_t>(comma - cursor) : std::strlen(cursor));
    cursor = comma ? comma + 1 : nullptr;
    return name;
}

template<std::size_t N> static void ReadTriggerText(wiz8::File& file, char (&text)[N])
{
    file.read_exact(text, N);
    if (std::find(std::begin(text), std::end(text), '\0') == std::end(text))
        throw std::runtime_error("unterminated trigger text field");
}

// GLOBAL: WIZ8 0x006599B8
static std::vector<W8TriggerEvent*> g_timed_events;
static std::vector<std::unique_ptr<W8TriggerEvent>> standalone_events;

// GLOBAL: WIZ8 0x006599C8
bool g_trigger_action_active;
// GLOBAL: WIZ8 0x006599AC
glm::vec3 g_trigger_action_scene_offset;
// GLOBAL: WIZ8 0x00659908

namespace {
struct LocationVariable {
    std::string name;
    int value;
    int level;
};
std::vector<LocationVariable> location_variables;
}

// GLOBAL: WIZ8 0x00606994
bool g_trigger_feedback = true;

// GLOBAL: WIZ8 0x0068c520
int g_container_event_alt = kFirstRemappedEvent + 13;

// GLOBAL: WIZ8 0x0068c548
int g_container_event = kFirstRemappedEvent + 14;

// GLOBAL: WIZ8 0x005ec124
const float g_trigger_progress_limit = 64.0f;

/* Trigger names are stored uppercase. Like the retail instance registry, the
   newest trigger wins when a name repeats. */
// FUNCTION: WIZ8 0x00443780
Trigger* FindTriggerByName(const char* name)
{
    if (name == 0 || name[0] == 0) {
        return 0;
    }
    for (W8World* world : {g_world.get(), g_secondary_world.get()}) {
        if (world == 0) {
            continue;
        }
        for (const auto& trigger : world->triggers | std::views::reverse) {
            if (SDL_strcasecmp(trigger->name, name) == 0) {
                return trigger.get();
            }
        }
    }
    return 0;
}

Trigger* FindRequiredTriggerByName(const char* name, std::source_location caller)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger == nullptr) {
        srAssertFail("required trigger", caller.file_name(), caller.line(),
                     fmt::format("Missing trigger '{}'! It's not in the LVL file!", name));
    }
    return trigger;
}

/* Resolve the trigger that drives a prop's use action. A kind-one or kind-two
   trigger without type-10 action data is followed along its recipient chain —
   each step picks the first recipient that is not the link we came from — until
   a trigger carrying type-10 action data is found, five hops at most. */
// FUNCTION: WIZ8 0x00443830
Trigger* FindTriggerForProp(W8World* world, W8Prop* prop)
{
    Trigger* trigger = prop->GetTrigger();

    if (trigger != 0) {
        W8TriggerActionData* action_data = trigger->m_pActionData.get();

        if (action_data != 0 && action_data->type == W8_TRIGGER_PAYLOAD_DOOR) {
            return trigger;
        }
        if (trigger->trigger_kind == 1 || trigger->trigger_kind == 2) {
            int trigger_count = static_cast<int>(world->triggers.size());

            for (int index = 0; index < trigger_count; ++index) {
                Trigger* other = world->triggers[index].get();
                Trigger* previous = trigger;
                int hops = 0;

                if (other == trigger) {
                    continue;
                }
                while (hops < 5 && !other->recipients.empty() &&
                       (other->trigger_kind == 1 || other->trigger_kind == 2)) {
                    const char* recipient =
                        (other->recipients.empty() ? nullptr : other->recipients.c_str());
                    Trigger* linked;

                    do {

                        if (recipient == 0) {
                            goto next_trigger;
                        }
                        const auto recipient_name = NextTriggerRecipient(recipient);
                        linked = FindTriggerByName(recipient_name.c_str());
                    } while (linked != previous);
                    action_data = other->m_pActionData.get();
                    if (action_data != 0 && action_data->type == W8_TRIGGER_PAYLOAD_DOOR) {
                        return other;
                    }
                    ++hops;
                    previous = other;
                    if (other == 0) {
                        break;
                    }
                }
            next_trigger:;
            }
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x00445730
void W8LockState::Reset()
{
    int pins;

    if (lock_type == 1) {
        for (int pin = 0; pin < 8; ++pin) {
            device_state.pins[pin] = static_cast<char>(Random(4));
        }
        pins = difficulty < 8 ? (difficulty < 2 ? 2 : difficulty) : 8;
        lock_countdown = pins * 3;
    }
    last_interaction_clock = -1;
    device_state.completed = 0;
}

// FUNCTION: WIZ8 0x004457A0
bool W8LockState::ConsumeCountdown()
{
    if (lock_countdown > 0) {
        --lock_countdown;
        return true;
    }
    return false;
}

/* True while a type-0x34 destination trigger holds (x, y, z) inside its
   activation annulus: under range_maximum and, unless flags bit 6
   waives the minimum, at or beyond range_minimum. */
// FUNCTION: WIZ8 0x00445940
bool InsideDestinationTrigger(float x, float y, float z)
{
    int count = static_cast<int>(g_world->triggers.size());

    for (int index = 0; index < count; ++index) {
        Trigger* trigger = g_world->triggers[index].get();
        if (trigger->initial_action != 0x34) {
            continue;
        }
        float dx = trigger->position.x - x;
        float dy = trigger->position.y - y;
        float dz = trigger->position.z - z;
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (distance < trigger->range_maximum && ((trigger->flags >> 6) & 1) == 0 &&
            distance >= trigger->range_minimum) {
            return true;
        }
    }
    return false;
}

// FUNCTION: WIZ8 0x0043cb30
void SaveTriggerRuntimeStates(wiz8::File& handle, bool restoring)
{
    int trigger_count = static_cast<int>(g_world->triggers.size());
    int saved_count = 0;
    int index;
    int version = 2;
    int restoring_value = restoring;

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = g_world->triggers[index].get();
        if (trigger->lock_state.lock_type != 0) {
            ++saved_count;
        }
    }

    handle.write(&version, sizeof(version));
    handle.write(&saved_count, sizeof(saved_count));
    handle.write(&restoring_value, sizeof(restoring_value));

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = g_world->triggers[index].get();
        if (trigger->lock_state.lock_type != 0) {
            handle.write(trigger->name, 0x80);
            handle.write(&version, sizeof(version));
            if (restoring) {
                handle.write(&trigger->lock_state.lock_type, sizeof(trigger->lock_state.lock_type));
                handle.write(&trigger->lock_state.difficulty,
                             sizeof(trigger->lock_state.difficulty));
                handle.write(&trigger->lock_state.device_state.completed,
                             sizeof(trigger->lock_state.device_state.completed));
                handle.write(trigger->lock_state.device_state.pins,
                             sizeof(trigger->lock_state.device_state.pins));
                handle.write(&trigger->lock_state.device_id, sizeof(trigger->lock_state.device_id));
                handle.write(&trigger->lock_state.key_id, sizeof(trigger->lock_state.key_id));
                handle.write(&trigger->lock_state.lock_countdown,
                             sizeof(trigger->lock_state.lock_countdown));
            } else {
                handle.write(&trigger->lock_state.device_state.completed,
                             sizeof(trigger->lock_state.device_state.completed));
                handle.write(trigger->lock_state.device_state.pins,
                             sizeof(trigger->lock_state.device_state.pins));
                handle.write(&trigger->lock_state.lock_countdown,
                             sizeof(trigger->lock_state.lock_countdown));
                handle.write(&trigger->lock_state.last_interaction_clock,
                             sizeof(trigger->lock_state.last_interaction_clock));
            }
        }
    }
}

void W8LockState::ReadRuntimeRecord(wiz8::File& handle, int version, int restoring)
{
    int record_version;

    if (restoring == 0 && version > 1) {
        handle.read_exact(&record_version, sizeof(record_version));
        handle.read_exact(&device_state.completed, sizeof(device_state.completed));
        handle.read_exact(device_state.pins, sizeof(device_state.pins));
        handle.read_exact(&lock_countdown, sizeof(lock_countdown));
        if (record_version > 1) {
            handle.read_exact(&last_interaction_clock, sizeof(last_interaction_clock));
        }
    } else {
        handle.read_exact(&record_version, sizeof(record_version));
        handle.read_exact(&lock_type, sizeof(lock_type));
        handle.read_exact(&difficulty, sizeof(difficulty));
        handle.read_exact(&device_state.completed, sizeof(device_state.completed));
        handle.read_exact(device_state.pins, sizeof(device_state.pins));
        handle.read_exact(&device_id, sizeof(device_id));
        handle.read_exact(&key_id, sizeof(key_id));
        if (record_version > 1) {
            handle.read_exact(&lock_countdown, sizeof(lock_countdown));
        }
    }
}

/* Read the runtime-state records written by SaveTriggerRuntimeStates.
   Each record names its trigger; a record whose trigger no longer exists is
   still consumed through a scratch trigger so the stream stays aligned. When
   restoring, a state byte block is re-randomized and the type-10 action data is
   re-linked to the stored item. */
// FUNCTION: WIZ8 0x0043ccf0
bool LoadTriggerRuntimeStates(wiz8::File& handle)
try {
    int version;
    int saved_count;
    int restoring;
    int index = 0;

    handle.read_exact(&version, sizeof(version));
    handle.read_exact(&saved_count, sizeof(saved_count));
    handle.read_exact(&restoring, sizeof(restoring));
    if (saved_count < 1) {
        return true;
    }
    for (;;) {
        char name[0x80];
        Trigger* trigger;

        ReadTriggerText(handle, name);
        trigger = FindTriggerByName(name);
        if (trigger == 0) {
            Trigger scratch;
            scratch.lock_state.ReadRuntimeRecord(handle, version, restoring);
        } else {
            W8TriggerActionData* action_data;

            trigger->lock_state.ReadRuntimeRecord(handle, version, restoring);
            if (restoring != 0) {
                trigger->lock_state.Reset();
            }
            action_data = trigger->m_pActionData.get();
            if (action_data != 0 && action_data->type == W8_TRIGGER_PAYLOAD_DOOR &&
                (!static_cast<W8DoorTriggerActionData*>(action_data)->locked ||
                 static_cast<W8DoorTriggerActionData*>(action_data)->item == -1)) {
                static_cast<W8DoorTriggerActionData*>(action_data)->locked =
                    trigger->lock_state.device_state.completed == 0;
                static_cast<W8DoorTriggerActionData*>(action_data)->item =
                    static_cast<short>(trigger->lock_state.key_id);
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
   the live event queue. Returns whether the header went out completely. */
// FUNCTION: WIZ8 0x0043BE60
bool Trigger::Save(wiz8::File& hFile)
{
    unsigned char version = 5;
    int trigger_count = static_cast<int>(g_world->triggers.size());
    bool header_ok;
    W8TriggerActionData* action_data;
    unsigned char has_action_data;
    unsigned char action_type;
    unsigned short action_flags;
    unsigned char action_kind;
    unsigned int progress_delay;
    unsigned char has_world_item;

    header_ok = (hFile.write(&version, sizeof(version)), true) &&
                (hFile.write(&trigger_count, sizeof(trigger_count)), true) &&
                (hFile.write(name, 0x80), true) && (hFile.write(&flags, sizeof(flags)), true) &&
                (hFile.write(&state_index, sizeof(state_index)), true) &&
                (hFile.write(&state_direction, sizeof(state_direction)), true) &&
                (hFile.write(&action, sizeof(action)), true) &&
                (hFile.write(&action_state, sizeof(action_state)), true) &&
                (hFile.write(&required_item_id, sizeof(required_item_id)), true);
    action_data = m_pActionData.get();
    has_action_data = action_data != 0;
    hFile.write(&has_action_data, sizeof(has_action_data));
    if (action_data != 0) {
        action_type = action_data->type;
        hFile.write(&action_type, sizeof(action_type));
        if (action_type == W8_TRIGGER_PAYLOAD_DOOR) {
            action_flags = 0;
            progress_delay = 0;
            action_kind = 2;
            hFile.write(&action_kind, sizeof(action_kind));
            const auto* door = static_cast<W8DoorTriggerActionData*>(action_data);
            action_flags = static_cast<decltype(action_flags)>(
                (door->open ? 1u : 0u) | (door->lockable ? 2u : 0u) | (door->locked ? 4u : 0u) |
                (door->auto_locking ? 8u : 0u) | (door->one_way ? 0x10u : 0u) |
                (door->secret ? 0x20u : 0u) | (door->found ? 0x40u : 0u) |
                (door->jammable ? 0x80u : 0u) | (door->jammed ? 0x100u : 0u));
            if (static_cast<W8DoorTriggerActionData*>(action_data)->item != 0) {
                action_flags |= 0x200;
            }
            hFile.write(&action_flags, sizeof(action_flags));
            if (m_lData1 != 0 && m_pEvent != 0 &&
                std::ranges::contains(g_timed_events, m_pEvent.get())) {
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
                unsigned int item = static_cast<unsigned short>(
                    static_cast<W8DoorTriggerActionData*>(action_data)->item);
                hFile.write(&item, 2);
            }
        }
    }
    has_world_item = world_item_group != 0;
    hFile.write(&has_world_item, sizeof(has_world_item));
    if (has_world_item != 0) {
        SaveItemFile(hFile, world_item_group.get());
    }
    hFile.write(&items_generated, sizeof(items_generated));
    hFile.write(&item_group_seed, sizeof(item_group_seed));
    hFile.write(&gold, sizeof(gold));
    hFile.write(&uses_remaining, sizeof(uses_remaining));
    return header_ok;
}

/* One door flag is stored as a byte whose low bit is the flag. */
template<class File>
static bool readDoorFlag(File& file)
{
    unsigned char flag = 0;
    file.read_exact(&flag, 1);
    return (flag & 1) != 0;
}

/* Read one trigger back from the save file. The version byte selects how much
   of the trailing block is present; a type-10 action payload rebuilds its
   action data and re-queues the delayed timed event from the saved progress. */
// FUNCTION: WIZ8 0x0043c1b0
bool Trigger::Load(wiz8::File& hFile, char version)
try {
    bool header_ok;
    unsigned char has_action_data;
    unsigned char action_type;
    unsigned char flag_mode;

    header_ok =
        (hFile.read(&flags, sizeof(flags)).bytes == static_cast<std::size_t>(sizeof(flags))) &&
        (hFile.read(&state_index, sizeof(state_index)).bytes ==
         static_cast<std::size_t>(sizeof(state_index))) &&
        (hFile.read(&state_direction, sizeof(state_direction)).bytes ==
         static_cast<std::size_t>(sizeof(state_direction))) &&
        (hFile.read(&action, sizeof(action)).bytes == static_cast<std::size_t>(sizeof(action))) &&
        (hFile.read(&action_state, sizeof(action_state)).bytes ==
         static_cast<std::size_t>(sizeof(action_state))) &&
        (hFile.read(&required_item_id, sizeof(required_item_id)).bytes ==
         static_cast<std::size_t>(sizeof(required_item_id)));
    if ((flags & W8_TRIGGER_SEARCHED) != 0) {
        UnregisterSearchableTrigger(this);
    }
    hFile.read_exact(&has_action_data, sizeof(has_action_data));
    if (has_action_data != 0) {
        hFile.read_exact(&action_type, sizeof(action_type));
        if (action_type == W8_TRIGGER_PAYLOAD_DOOR) {
            auto pDoorOwner = std::make_unique<W8DoorTriggerActionData>();
            auto* pDoor = pDoorOwner.get();
            unsigned short action_flags;
            unsigned short progress_delay;
            unsigned short item;

            if (pDoor == 0) {
                srAssertFail("pDoor", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x20b,
                             0);
            }
            m_pActionData = std::move(pDoorOwner);
            pDoor->type = W8_TRIGGER_PAYLOAD_DOOR;
            hFile.read_exact(&flag_mode, 1);
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
                hFile.read_exact(&action_flags, 2);
                pDoor->open = (action_flags & 1) != 0;
                pDoor->lockable = (action_flags & 2) != 0;
                pDoor->locked = (action_flags & 4) != 0;
                pDoor->auto_locking = (action_flags & 8) != 0;
                pDoor->one_way = (action_flags & 0x10) != 0;
                pDoor->secret = (action_flags & 0x20) != 0;
                pDoor->found = (action_flags & 0x40) != 0;
                pDoor->jammable = (action_flags & 0x80) != 0;
                pDoor->jammed = (action_flags & 0x100) != 0;
                hFile.read_exact(&progress_delay, 2);
                if (m_lData1 != 0 && progress_delay != 0) {
                    if (m_pEvent == 0) {
                        float duration;

                        m_pEvent = std::make_unique<W8TriggerEvent>();
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
                    if (!std::ranges::contains(g_timed_events, m_pEvent.get())) {
                        g_timed_events.push_back(m_pEvent.get());
                    }
                }
                hFile.read_exact(&item, 2);
                pDoor->item = static_cast<short>(item);
            }
        }
    }

    if (version > 1) {
        unsigned char has_world_item;

        hFile.read_exact(&has_world_item, sizeof(has_world_item));
        if (has_world_item != 0) {
            world_item_group = LoadItem(hFile);
        }
        hFile.read_exact(&items_generated, sizeof(items_generated));
        hFile.read_exact(&item_group_seed, sizeof(item_group_seed));
        hFile.read_exact(&gold, sizeof(gold));
        hFile.read_exact(&uses_remaining, sizeof(uses_remaining));
    }
    if (version == 3) {
        hFile.seek(0x1d, wiz8::SeekOrigin::current);
    }
    return header_ok;
} catch (const std::exception&) {
    return false;
}

/* Read every trigger record of the save file's trigger chunk. Tags 1-4 update
   the matching in-place trigger, tag 5 names its trigger and is consumed
   through a scratch trigger when the name is gone, and anything else pushes
   the tag byte back and ends the walk. */
// FUNCTION: WIZ8 0x0043c860
bool LoadWorldTriggers(wiz8::File& file)
try {
    for (std::size_t index = 0; index < g_world->triggers.size(); ++index) {
        char tag;
        file.read_exact(&tag, 1);
        if (tag < 1 || tag > 5) {
            file.seek(-1, wiz8::SeekOrigin::current);
            return true;
        }
        Trigger* trigger = g_world->triggers[index].get();
        if (tag == 5) {
            int trigger_id;
            char name[128];
            file.read_exact(&trigger_id, sizeof(trigger_id));
            ReadTriggerText(file, name);
            trigger = FindTriggerByName(name);
        }
        if (trigger) {
            if (!trigger->Load(file, tag)) return false;
        } else {
            Trigger scratch;
            if (!scratch.Load(file, tag)) return false;
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Write every trigger of a world for the save file's trigger chunk. A trigger
   whose own serialization reports failure stops the walk. */
// FUNCTION: WIZ8 0x0043C810
void SaveWorldTriggers(wiz8::File& hFile)
{
    auto* triggers = &g_world->triggers;

    for (int index = 0; index < static_cast<int>(triggers->size()); ++index) {
        if (!((*triggers)[index])->Save(hFile)) {
            return;
        }
    }
}

// FUNCTION: WIZ8 0x0043d120
void SaveTriggerActionData(wiz8::File& handle)
{
    int trigger_count = static_cast<int>(g_world->triggers.size());
    int saved_count = 0;
    int index;
    int version = 1;

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = g_world->triggers[index].get();
        if (trigger->inline_action_data[0] != '\0') {
            ++saved_count;
        }
    }

    handle.write(&version, sizeof(version));
    handle.write(&saved_count, sizeof(saved_count));

    for (index = 0; index < trigger_count; ++index) {
        Trigger* trigger = g_world->triggers[index].get();
        if (trigger->inline_action_data[0] != '\0') {
            handle.write(trigger->name, 0x80);
            handle.write(trigger->inline_action_data, sizeof(trigger->inline_action_data));
        }
    }
}

/* Read the trigger action-data chunk written by SaveTriggerActionData:
   a version/count header, then per record the trigger name and its 0x100-byte
   payload. Records for missing triggers are skipped with a seek. */
// FUNCTION: WIZ8 0x0043d1f0
bool LoadTriggerActionData(wiz8::File& handle)
try {
    std::int32_t version, count;
    handle.read_exact(&version, sizeof(version));
    handle.read_exact(&count, sizeof(count));
    if (count < 0 || count > (handle.size() - handle.tell()) / 384) return false;
    std::vector<std::pair<Trigger*, std::array<char, 256>>> pending;
    pending.reserve(count);
    for (std::int32_t index = 0; index < count; ++index) {
        char name[128];
        std::array<char, 256> action;
        ReadTriggerText(handle, name);
        handle.read_exact(action.data(), action.size());
        if (std::find(action.begin(), action.end(), '\0') == action.end()) return false;
        pending.emplace_back(FindTriggerByName(name), action);
    }
    for (const auto& [trigger, action] : pending)
        if (trigger) std::copy(action.begin(), action.end(), trigger->inline_action_data);
    return true;
} catch (const std::exception&) {
    return false;
}

// VTABLE: WIZ8 0x005ec12c
// class W8TriggerEvent
W8TriggerEvent::W8TriggerEvent() : action(-1), timer(), trigger(0), repeat(0), completed(0) {}

// FUNCTION: WIZ8 0x004409a0
W8TriggerEvent::~W8TriggerEvent() = default;

class W8TriggerShakeEvent : public W8TriggerEvent {
public:
    W8TriggerShakeEvent();
    virtual void Update() override;
    ~W8TriggerShakeEvent() override
    {
        if (effect) {
            CameraShakes().Stop(*effect);
        }
    }

    std::optional<ShakeDefinition> effect;
    int intensity;
    bool reverse;
};

// VTABLE: WIZ8 0x005ec140
// class W8TriggerShakeEvent
/* Retail ICF folds this class's deleting destructor onto W8TriggerEvent's
   retained body at 0x00440980; there is no distinct retail emission to mark. */

W8TriggerShakeEvent::W8TriggerShakeEvent() : intensity(1), reverse(0) {}

// GLOBAL: WIZ8 0x006599a0
glm::vec3 g_trigger_camera;

/* Advance every world trigger: raise the item picker when a prop-bearing
   activation asks for one, then run proximity activations for kind-two
   triggers. The camera position is cached when no trigger fired. */
// FUNCTION: WIZ8 0x00443ae0
void UpdateWorldTriggers(W8World* world)
{
    glm::vec3 camera;
    bool activated = false;
    bool running = false;

    camera = cameraLocation();
    int count = static_cast<int>(world->triggers.size());
    for (int index = 0; index < count; ++index) {
        Trigger* trigger = world->triggers[index].get();
        if ((trigger->flags & W8_TRIGGER_ITEM_PICKER) != 0 && trigger->m_pProp != 0 &&
            (trigger->m_pProp->GetAnimationState() < 2 ||
             trigger->m_pProp->Rep()->animation_playing == 0)) {
            trigger->GenerateItemGroup();
            if (g_modal_owner == 0 && trigger->world_item_group != 0) {
                auto dialog = std::make_unique<W8TriggerItemPickerDialog>();
                dialog->SetItemGroup(trigger->world_item_group.get());
                dialog->on_complete = [trigger](W8WorldItem*) {
                    trigger->flags &= ~W8_TRIGGER_ITEM_PICKER;
                };
                gXStatus.item_pick_pending = false;
                OpenModal(std::move(dialog));
            }
        }
        if (g_environment_load_flag != 0 && trigger->trigger_kind == 2 &&
            (trigger->flags & W8_TRIGGER_ENABLED) != 0 &&
            (trigger->flags & W8_TRIGGER_POSITIONED) != 0 &&
            (trigger->flags & W8_TRIGGER_PLANE) == 0) {
            glm::vec3 trigger_position(trigger->position.x, trigger->position.y,
                                       trigger->position.z);
            float distance = glm::length((trigger_position - camera));
            if (trigger->range_maximum <= distance) {
                if ((trigger->flags & W8_TRIGGER_RUNNING) != 0) {
                    trigger->FinishAction();
                }
            } else if ((trigger->flags & W8_TRIGGER_RUNNING) == 0 &&
                       trigger->range_minimum <= distance) {
                activated = true;
                if ((trigger->flags & W8_TRIGGER_EXCLUSIVE) == 0 || !running) {
                    trigger->Run(-1);
                    running = true;
                }
            }
        }
    }
    if (world == g_world.get() && !activated) {
        g_trigger_camera = camera;
    }
}

// FUNCTION: WIZ8 0x00443D30
void UpdateTimedTriggerEvents(void)
{
    for (int index = 0; index < static_cast<int>(g_timed_events.size()); ++index) {
        W8TriggerEvent* event = g_timed_events[index];
        event->Update();
        if (event->completed) {
            g_timed_events.erase(g_timed_events.begin() + index);
            --index;
            if (event->trigger != 0) {
                event->trigger->m_pEvent.reset();
            } else {
                std::erase_if(standalone_events,
                              [event](const auto& owner) { return owner.get() == event; });
            }
        }
    }
}

// FUNCTION: WIZ8 0x00444ec0
void W8TriggerShakeEvent::Update()
{
    if (!effect) {
        float intensity = this->intensity / 2.5e+02f;

        if (intensity < 1.0f) {
            intensity = 1.0f;
        }
        effect.emplace(m_pCountdown->m_duration_seconds, false, intensity, 0, nullptr);
        if (reverse) {
            effect->fade_out = true;
        }
        CameraShakes().Start(*effect, &*effect);
    }

    if (!CameraShakes().IsActive(*effect)) {
        effect.reset();
        if (trigger != 0) {
            trigger->FinishAction();
        }
        if (repeat) {
            completed = true;
        }
    }
}

// FUNCTION: WIZ8 0x004447F0
void Trigger::CompleteItemInteraction()
{
    W8TriggerActionData* action_data = m_pActionData.get();
    lock_state.device_state.completed = 1;
    if (action_data != 0 && action_data->type == W8_TRIGGER_PAYLOAD_DOOR) {
        static_cast<W8DoorTriggerActionData*>(action_data)->locked = false;
    }
}

/* Run the trigger now when its action data selects the immediate path, or
   restart its timed-event clocks when it already owns a queued event. The
   immediate path sets the running flag around Run so nested activation sees
   it; the timed path only touches clocks for events still queued. */
// FUNCTION: WIZ8 0x00444750
void Trigger::Activate()
{
    W8TriggerActionData* action_data = m_pActionData.get();
    if (action_data != 0 && action_data->type == W8_TRIGGER_PAYLOAD_DOOR &&
        (lock_state.lock_type == 0 || lock_state.device_state.completed != 0) &&
        !static_cast<W8DoorTriggerActionData*>(action_data)->locked) {
        if (!static_cast<W8DoorTriggerActionData*>(action_data)->open) {
            running = true;
            Run(-1);
            running = false;
        } else {
            W8TriggerEvent* event = m_pEvent.get();
            if (event != 0 && std::ranges::contains(g_timed_events, event)) {
                event->timer.Restart();
                if (event->m_pCountdown.has_value()) {
                    event->m_pCountdown->Restart();
                }
            }
        }
    }
}

// FUNCTION: WIZ8 0x00444810
bool Trigger::HasActorWithinRadius(float radius, bool include_party)
{
    glm::vec3 center;

    if ((flags & W8_TRIGGER_POSITIONED) != 0 || m_pProp != 0) {
        if (m_pProp == 0) {
            center = glm::vec3(position.x, position.y, position.z);
        } else {
            const auto prop_center = m_pProp->GetCenterPosition();
            if (!prop_center) return false;
            center = *prop_center;
        }

        glm::vec3 lower;
        glm::vec3 upper;
        glm::vec3 extent;
        std::vector<w8_ulong> locations;
        extent = glm::vec3(radius, radius, radius);
        lower = center - extent;
        upper = center + extent;

        locations = g_octree->queryObjects({lower, upper}, W8_OCTREE_KIND_LOCATION, -1);
        unsigned int count = locations.size();
        for (unsigned int index = 0; index < count; ++index) {
            int location_id = locations[index];
            if (location_id == 0) {
                break;
            }
            W8MonsterInfo* monster_info = FindMonster(location_id);
            if (monster_info != 0 && monster_info->p3D != 0) {
                glm::vec3 monster_position = monster_info->p3D->GetPosition();
                if (glm::length((monster_position - center)) <= radius) {
                    return true;
                }
            }
        }
    }

    if (include_party) {
        glm::vec3 party_position = g_startup_world->GetPosition();
        if (glm::length((party_position - center)) <= radius) {
            return true;
        }
    }
    return false;
}

// FUNCTION: WIZ8 0x004457c0
bool Trigger::PlayActionSound(const char* sound_name, int volume)
{
    glm::vec3 position;

    if (volume == 0) {
        volume = 100;
    }
    if ((flags & W8_TRIGGER_POSITIONED) == 0) {
        if (m_pProp == 0) {
            wiz8::sound::play(sound_name, {.volume = std::uint32_t(
                                               (g_settings.sound_effects_volume * volume) / 0x7f)});
            return false;
        }
        const auto center = m_pProp->GetCenterPosition();
        if (!center) return false;
        position = *center;
    } else {
        position = glm::vec3(this->position.x, this->position.y, this->position.z);
    }

    auto sound = std::make_unique<stSound3D>(sound_name, nullptr);
    sound->volume = volume;
    sound->setLocation(glm::dvec3(position));
    if (!sound->Play(false)) return false;
    Retain3DSound(std::move(sound));
    return true;
}

/* Timed actions pause their private timer with the game clock, then dispatch
   the small set of delayed Trigger effects once the timer completes. */
// FUNCTION: WIZ8 0x00444a00
void W8TriggerEvent::Update()
{
    if (action == 2) {
        W8GameTimer::Flags flags = timer.m_flags;

        if (g_combat_inactive == 0) {
            if (flags.paused || (g_shared_timer_paused && !flags.raw_time) ||
                g_shared_timer_flag0) {
                return;
            }
            timer.m_flags.paused = true;
            timer.m_start = timer.GetTime() - timer.m_start;
            return;
        }
        if (flags.paused || (g_shared_timer_paused && !flags.raw_time) || g_shared_timer_flag0) {
            timer.m_flags.paused = false;
            timer.m_start = timer.GetTime() - timer.m_start;
            timer.SetDuration(-1.0f);
        }
    }

    if (timer.GetProgress() <= 1.0f || trigger->HasActorWithinRadius(5000.0f, true)) {
        return;
    }

    switch (action) {
    case 0x23: {
        W8Dice dice;
        SetDice(&dice, 1, 6, 2);
        ApplyRolledHealthChangeToParty(&dice, 0, 0);
        trigger->UpdateActionAnimation();
        break;
    }

    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
        trigger->uses_remaining = trigger->m_lData1;
        completed = true;
        break;

    case 0x0c: {
        if (trigger != 0) {
            glm::vec3 source;
            glm::vec3 target;
            glm::vec3 transformed;
            glm::vec3 axis;
            glm::mat3 rotation;

            source = trigger->position;
            target = source;
            target.z += 100.0f;

            axis = glm::vec3(0.0, 0.0, 1.0);
            rotation = glm::transpose(
                glm::mat3(trigger->direction, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)));

            if (trigger->angle != 0.0f) {
                rotation *= glm::mat3_cast(
                    glm::angleAxis(float(std::atan2(sin(trigger->angle), cos(trigger->angle))),
                                   normalizedOrZero(glm::vec3(axis))));
            }

            transformed.x = glm::dot(glm::row(rotation, 1), target);
            transformed.y = glm::dot(glm::row(rotation, 2), target);
            transformed.z = glm::dot(axis, target);
            FireMissile(static_cast<unsigned int>(trigger->m_lData1), &source, &transformed, 0, 1,
                        1, 50000.0f);
        }
        break;
    }

    case 0x3f: {
        Trigger* target = FindTriggerByName(trigger->recipients.c_str());
        if (target != 0) {
            target->Run(-1);
        }
        break;
    }

    case 0x47: {
        const char* name = (trigger->recipients.empty() ? nullptr : trigger->recipients.c_str());
        while (name != 0) {
            const auto recipient_name = NextTriggerRecipient(name);
            stParticle* particle = FindParticleByName(recipient_name.c_str());
            if (particle != 0) {
                particle->persisted = true;
                particle->SetActive(0);
            }
        }
        break;
    }

    case 2:
        if (trigger->m_pActionData != 0 &&
            trigger->m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR &&
            static_cast<W8DoorTriggerActionData*>(trigger->m_pActionData.get())->open) {
            trigger->Run(-1);
        }
        break;

    default:
        break;
    }

    if (repeat) {
        completed = true;
    }
}

/* A state-driven Prop has one location variable per animation slot.  Ensure
   the complete set exists for the loaded level and select slot zero as the
   initial active state. */
// FUNCTION: WIZ8 0x00445200
void InitializeStateDrivenPropVariables(Trigger* trigger)
{
    int slot;

    if (trigger->state_to_modify.empty()) {
        return;
    }
    for (slot = 0; slot < static_cast<signed char>(trigger->m_pProp->Rep()->slots.size()); ++slot) {
        char name[132];

        if (trigger->m_bRepType != W8_TRIGGER_REP_PROP) {
            srAssertFail("m_bRepType == TRIGGER_REP_PROP", "..\\Engine Code\\Include\\Trigger.hpp",
                         0x3ed, 0);
        }
        snprintf(name, sizeof(name), "%s%d", trigger->state_to_modify.c_str(), slot);
        CreateLocationVar(name, slot == 0);
    }
}

// FUNCTION: WIZ8 0x00444030
void SetTriggerVariableByName(const char* name, int value)
{
    const int index = GetLocationVarIDByName(name);
    if (index < 0) srAssertFail("Unknown location variable", __FILE__, __LINE__, "%s", name);
    location_variables[index].value = value;
}

// VTABLE: WIZ8 0x005ec138
// class W8TriggerActionData
W8TriggerActionData::W8TriggerActionData() : type(W8_TRIGGER_PAYLOAD_NONE) {}

// FUNCTION: WIZ8 0x00445ee0
W8TriggerActionData::~W8TriggerActionData() = default;

/* Type 5 installs 0x005EC148 after constructing the common base. Its deleting
   destructor folds with the type-10 wrapper at 0x00445EC0: both call the
   common destructor, then scalar operator delete when requested. */
// VTABLE: WIZ8 0x005ec148
// class W8EnvironmentTriggerActionData
// VTABLE: WIZ8 0x005ec134
// class W8DoorTriggerActionData
// VTABLE: WIZ8 0x005ec158
// class W8StringTriggerActionData
// FUNCTION: WIZ8 0x00443750
W8StringTriggerActionData::~W8StringTriggerActionData() = default;

/* Clear the running bit and run every comma-separated recipient trigger once
   when the link-out and state-gate bits are set. */
// FUNCTION: WIZ8 0x00441590
void Trigger::RunLinkedTriggers()
{
    const char* recipient;

    flags &= ~W8_TRIGGER_RUNNING;
    if ((flags & W8_TRIGGER_FIRE_LINKED) != 0 && (flags & W8_TRIGGER_LINK_ON_DEACTIVATE) != 0 &&
        !recipients.empty()) {
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        while (recipient != 0) {
            Trigger* trigger = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (trigger != 0) {
                trigger->Run(-1);
            }
        }
    }
}

// FUNCTION: WIZ8 0x004417c0
std::unique_ptr<W8TriggerActionData> ReadDoorTriggerActionData(wiz8::File* handle)
{
    auto data = std::make_unique<W8DoorTriggerActionData>();

    unsigned char version;
    unsigned short item;
    unsigned char has_position;
    glm::vec3 position;
    char linked_trigger[0x80];
    handle->read_exact(&version, 1);
    data->open = readDoorFlag(*handle);
    data->lockable = readDoorFlag(*handle);
    data->locked = readDoorFlag(*handle);
    data->auto_locking = readDoorFlag(*handle);
    data->one_way = readDoorFlag(*handle);
    data->secret = readDoorFlag(*handle);
    data->found = readDoorFlag(*handle);
    data->jammable = readDoorFlag(*handle);
    data->jammed = readDoorFlag(*handle);
    handle->read_exact(&item, 2);
    handle->read_exact(&has_position, 1);
    wiz8::retail::read_exact(*handle, position);
    position *= 500.0f;
    ReadTriggerText(*handle, linked_trigger);

    data->item = item;
    strcpy(data->linked_trigger, linked_trigger);
    if (has_position != 0) {
        data->position = position;
        data->has_position = true;
    }
    return data;
}

// FUNCTION: WIZ8 0x00441a20
Trigger* Trigger::CreateAndLoadLevelTrigger(wiz8::File* handle, W8World* world)
{
    /* Retail read these uninitialised when the FileRead chain short-circuited;
       natively they start at zero. */
    std::unique_ptr<Trigger> owner;
    Trigger* trigger = nullptr;
    unsigned char record_version = 0;
    unsigned char record_type = 0;
    if (handle == 0) {
        srAssertFail("hFile", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0xca3, 0);
    }
    handle->read_exact(&record_version, 1);
    handle->read_exact(&record_type, 1);
    if (record_type < 1 || record_type > 4) throw std::runtime_error("invalid level trigger type");
    if (record_type != 3) {
        owner = std::make_unique<Trigger>();
        trigger = owner.get();
        trigger->trigger_id = g_status.next_trigger_id++;
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
                if (world->game_data != 0) {
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
                    trigger->m_pActionData = ReadDoorTriggerActionData(handle);
                    trigger->state_index =
                        static_cast<W8DoorTriggerActionData*>(trigger->m_pActionData.get())->open;
                }
            }
        }
        if (version > 3) {
            handle->read_exact(&action_value, 4);
        }

        trigger->trigger_kind = 1;
        trigger->range_maximum = range * 500.0f;
        trigger->range_minimum = minimum_range * 500.0f;
        trigger->recipients = recipients;
        trigger->state_count = static_cast<unsigned char>(byte1);
        trigger->state_index = 0;
        trigger->state_direction = 1;
        trigger->cycle_bounce = byte0;
        trigger->action_value = action_value;
        trigger->initial_action = action;
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_STATES, animate_states != 0.0f);
        assignFlag(trigger->flags, W8_TRIGGER_ANIMATE_ACTION, animate_action != 0);
        assignFlag(trigger->flags, W8_TRIGGER_ENABLED, enabled != 0);
        assignFlag(trigger->flags, W8_TRIGGER_LINK_ON_DEACTIVATE, (packed_flags & 2) != 0);
        assignFlag(trigger->flags, W8_TRIGGER_FIRE_LINKED, (packed_flags & 1) != 0);
        if (trigger->name[0] != 0) {
            trigger->setName(trigger->name);
        }
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
                auto data = std::make_unique<W8StringTriggerActionData>();
                data->type = W8_TRIGGER_PAYLOAD_STRING;
                trigger->m_pActionData.reset();
                data->owned_string = action_string;
                trigger->m_pActionData = std::move(data);
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

        trigger->trigger_kind = 2;
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
        if ((trigger->flags & W8_TRIGGER_PLANE) != 0 && world->game_data != 0) {
            world->game_data->AddTriggerPlane(trigger->representation_vectors, trigger);
        }
        if (trigger->initial_action == 0x34 && trigger->recipients[0] == 0) {
            trigger->range_minimum = 0.0f;
            trigger->range_maximum = 0.0f;
        }
        if (trigger->name[0] != 0) {
            trigger->setName(trigger->name);
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
            handle->read_exact(&trigger->action_data_mode, 1);
            handle->read_exact(&trigger->sound_volume, 1);
            int unused;
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
            handle->read_exact(&unused, 4);
        }

        trigger->trigger_kind = ((packed_flags & 1) != 0 || searchable == 1) ? 1 : 2;
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
        trigger->state_mod_mode = value_b4;
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
                trigger->m_pActionData = ReadDoorTriggerActionData(handle);
                trigger->state_index =
                    static_cast<W8DoorTriggerActionData*>(trigger->m_pActionData.get())->open;
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
        if (representation_kind == 2 && world->game_data != 0) {
            world->game_data->AddTriggerPlane(trigger->representation_vectors, trigger);
        }
        if (trigger->initial_action == 0x34 && trigger->recipients.empty()) {
            trigger->range_minimum = 0.0f;
            trigger->range_maximum = 0.0f;
        }
        if (trigger->name[0] != 0) {
            trigger->setName(trigger->name);
        }
        if (searchable == 1) {
            RegisterSearchableTrigger(trigger);
        }
        if (trigger->initial_action == 0x0c) {
            if (trigger->m_lData1 < 0 ||
                trigger->m_lData1 >= static_cast<int>(g_missile_table.size())) {
                srAssertFail("((pTrigger->m_lData1 >= 0) && (pTrigger->m_lData1 < "
                             "Missile::GetNumTypes()))",
                             "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0xf14,
                             fmt::format("Trigger {}: You must enter a valid missile number",
                                         trigger->name));
            }
            if (trigger->m_lData2 == -1) {
                srAssertFail(
                    "(pTrigger->m_lData2!=(-1))",
                    "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0xf15,
                    fmt::format("Trigger {}: You must enter a time value in Data2", trigger->name));
            }
            if (trigger->m_lData2 < 0) {
                trigger->m_pEvent = std::make_unique<W8TriggerEvent>();
                trigger->m_pEvent->action = static_cast<short>(trigger->initial_action);
                trigger->m_pEvent->timer.SetDuration(abs(trigger->m_lData2) * 0.001f);
                trigger->m_pEvent->timer.Restart();
                trigger->m_pEvent->trigger = trigger;
                trigger->flags |= W8_TRIGGER_RUNNING;
                g_timed_events.push_back(trigger->m_pEvent.get());
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

// VTABLE: WIZ8 0x005ec0e4
// class Trigger
// FUNCTION: WIZ8 0x00441750
void Trigger::GetPosition(glm::vec3* position) const
{
    *position = this->position;
}

// FUNCTION: WIZ8 0x00441780
bool Trigger::HasActionMessage()
{
    return (flags & W8_TRIGGER_CAN_RUN_LINKED) != 0;
}

// FUNCTION: WIZ8 0x00441790
bool Trigger::RequiresItem()
{
    if (required_item_id >= 0) {
        return true;
    }
    if (m_pActionData != 0 && m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR &&
        m_pActionData != 0 &&
        static_cast<W8DoorTriggerActionData*>(m_pActionData.get())->item != -1) {
        return true;
    }
    return false;
}

// VTABLE: WIZ8 0x005ec104
// FUNCTION: WIZ8 0x0043ba10
Trigger::Trigger()
{
    trigger_kind = 0;
    trigger_id = 0;
    flags = 0;
    range_minimum = 0.0f;
    range_maximum = 0.0f;
    action_value = 0;
    state_count = 0;
    state_index = 0;
    state_direction = 0;
    cycle_bounce = 0;
    state_mod_mode = 0;
    searchable = 0;
    angle = 0.0f;
    m_bRepType = W8_TRIGGER_REP_NONE;
    m_pProp = 0;
    rep_item = 0;
    m_pWorld = 0;
    initial_action = 0;
    alternate_action = 0;
    fallback_action = 0;
    action = 0;
    action_state = 1;
    world_item_group = 0;
    items_generated = 0;
    activation_callback = 0;
    running = false;

    surface_id = -1;
    sound_volume = -1;
    required_item_id = -1;
    lock_state.device_id = -1;
    lock_state.key_id = -1;
    lock_state.last_interaction_clock = -1;
    lock_state.lock_type = 0;
    lock_state.difficulty = 0;
    lock_state.device_state.completed = 0;
    lock_state.lock_countdown = 0;
    memset(lock_state.device_state.pins, 0, sizeof(lock_state.device_state.pins));

    flags |= W8_TRIGGER_ON;
    name[0] = 0;
    position = glm::vec3(0);
    action_data[0] = 0;
    item_group_seed = static_cast<unsigned int>(w8_get_ticks() + Random(30000));
    gold = 0;
    uses_remaining = 0;
    trigger_id = g_status.next_trigger_id++;
}

// FUNCTION: WIZ8 0x00440d00
void Trigger::UpdateActionAnimation()
{
    char* action_data = this->action_data;

    if ((flags & W8_TRIGGER_ANIMATE_ACTION) == 0 && (flags & W8_TRIGGER_ALTERNATE_ACTION) == 0) {
        return;
    }
    if ((flags & W8_TRIGGER_ALTERNATE_ACTION) != 0) {
        if (action_data_mode == 0) {
            if ((flags & W8_TRIGGER_ALTERNATE_SELECTED) == 0) {
                flags |= W8_TRIGGER_ALTERNATE_SELECTED;
            } else {
                action_data = alternate_action_data;
                flags &= ~W8_TRIGGER_ALTERNATE_SELECTED;
            }
        } else if (action_data_mode == 1) {
            if (action == alternate_action) {
                PlayActionSound(alternate_action_data, sound_volume);
                return;
            }
        } else if (action_data_mode == 2 && action == fallback_action) {
            PlayActionSound(alternate_action_data, sound_volume);
            return;
        }
    }
    PlayActionSound(action_data, sound_volume);
}

// FUNCTION: WIZ8 0x00441110
void Trigger::FinishAction()
{
    bool was_running = ((flags & W8_TRIGGER_RUNNING) != 0);
    bool action_completed = false;
    const char* recipient;

    flags &= ~W8_TRIGGER_RUNNING;

    if (trigger_kind == 1) {
        if (action != 0x39) {
            goto reactivate_linked_triggers;
        }
        if (m_pEvent != 0) {
            m_pEvent->completed = true;
        }
        g_trigger_action_active = false;
        goto finish_linked_triggers;
    }

    if (trigger_kind != 2) {
        goto reactivate_linked_triggers;
    }

    switch (action) {
    case 0x0c:
        if (m_pEvent != 0 && m_lData2 > 0) {
            m_pEvent->completed = true;
        }
        action_completed = true;
        break;

    case 0x23: {
        wiz8::erase_first(g_timed_events, m_pEvent.get());
        action_completed = true;
        break;
    }

    case 0x39:
        if (m_pEvent != 0) {
            m_pEvent->completed = true;
        }
        g_trigger_action_active = false;
        action_completed = true;
        break;
    }

    if ((flags & W8_TRIGGER_KEEP_ON_FINISH) == 0 && action != 0) {
        if (action == 4) {
            recipient = (recipients.empty() ? nullptr : recipients.c_str());
            action_completed = false;
            while (recipient != 0) {
                const auto recipient_name = NextTriggerRecipient(recipient);

                stLight* light = FindLightByName(recipient_name.c_str());
                if (light != 0) {
                    light->m_save_marked = true;
                    if (light->testFlag(srNode::FLAG_DISABLE) == 0) {
                        light->setFlag(srNode::FLAG_DISABLE);
                    } else {
                        light->clearFlag(srNode::FLAG_DISABLE);
                    }
                    action_completed = true;
                }
            }
        } else if (action == 0x22) {
            if (m_pActionData == 0) {
                srAssertFail("m_pActionData", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp",
                             2822, "Trigger.cpp: Dark Area doesn't have action data");
            }
            SetWorldEnvironmentIntensity(
                g_world.get(), static_cast<W8EnvironmentTriggerActionData*>(m_pActionData.get())
                                   ->previous_environment);
            m_pActionData.reset();
            goto finish_linked_triggers;
        }

        if (!action_completed) {
            goto reactivate_linked_triggers;
        }
    }

finish_linked_triggers:
    if ((flags & W8_TRIGGER_FIRE_LINKED) != 0) {
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        while (recipient != 0) {
            Trigger* trigger = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (trigger != 0) {
                trigger->FinishAction();
            }
        }
    }

reactivate_linked_triggers:
    if (was_running && (flags & W8_TRIGGER_ON) != 0 &&
        (flags & W8_TRIGGER_REACTIVATE_LINKED) != 0) {
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        while (recipient != 0) {
            Trigger* trigger = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (trigger != 0) {
                trigger->Run(-1);
            }
        }
    }
}

// FUNCTION: WIZ8 0x00445480
/* The location variable a state-changing action writes. A missing variable is a retail assert. */
static int FindModifiedLocationVar(const char* name, int line)
{
    const int id = GetLocationVarIDByName(name);
    if (id == -1) {
        srAssertFail("iVar != BAD_INDEX", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp",
                     line, 0);
    }
    return id;
}

// FUNCTION: WIZ8 0x004409b0
void Trigger::CommitActionResult(bool apply_state_changes)
{
    const char* recipient;

    UpdateActionAnimation();

    if (!recipients.empty() && (flags & W8_TRIGGER_FIRE_LINKED) != 0 &&
        (flags & W8_TRIGGER_LINK_ON_DEACTIVATE) == 0) {
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        while (recipient != 0) {
            Trigger* trigger = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (trigger != 0) {
                bool was_running = ((flags & W8_TRIGGER_RUNNING) != 0);
                flags |= W8_TRIGGER_RUNNING;
                trigger->Run(m_lData1);
                flags = (flags & ~W8_TRIGGER_RUNNING) | (was_running ? W8_TRIGGER_RUNNING : 0);
            }
        }
    }

    if (!state_to_modify.empty() && apply_state_changes) {
        int state_id;
        int state_value;

        if (state_mod_mode == 1) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str(), 4297);
            state_value = 1;
        } else if (state_mod_mode == 2) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str(), 4297);
            state_value = 0;
        } else if (state_mod_mode == 3) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str(), 4317);
            state_value = location_variables[state_id].value == 0;
            state_id = FindModifiedLocationVar(state_to_modify.c_str(), 4297);
        } else {
            goto show_action_message;
        }
        location_variables[state_id].value = state_value;
    }

show_action_message:
    if ((flags & W8_TRIGGER_CAN_RUN_LINKED) != 0) {
        const char* level_folder = GetLevelFolderName(g_status.current_level);
        int message_id;

        if (action_state == 2) {
            message_id = m_lData1;
        } else if (action_state == 3) {
            message_id = m_lData2;
        } else if (action_state == 4) {
            message_id = m_lData3;
        } else {
            goto action_complete;
        }

        if (message_id != -1) {
            char path[512];
            std::string text;

            if (level_folder == 0) {
                level_folder = "";
            }
            snprintf(path, sizeof(path), "Data\\Messages\\%s.msg", level_folder);
            if ((!(text = GetStringFromStringDatabase(path, message_id, 0, 0)).empty()) != 0) {
                ShowString(text);
            }
        }
    }

action_complete:
    flags |= W8_TRIGGER_FIRED;
}

/* Resolve an entity or a five-character level/entrance code, then either
   request the level transition or move the current world to the resolved
   destination. The destination Trigger supplies the local portal orientation
   when the name was not one of the world's named entities. */
// FUNCTION: WIZ8 0x00440dd0
void Trigger::RunDestination(const char* destination)
{
    glm::vec3 destination_position;
    glm::vec3 destination_direction;
    glm::mat3 rotation;
    /* Retail's named-entity path reads saved storage for these integers;
       natively a named entity stays in the current level. */
    int location_id;
    int entrance = 0;
    int current_location;
    float angle;
    bool named_entity;

    if (g_modal_owner != 0) {
        return;
    }

    ResetInactiveLevelDataVectors();
    current_location = g_status.current_level;
    location_id = current_location;
    named_entity =
        FindEntityByName(destination, &destination_position, &angle, &destination_direction);
    if (!named_entity) {
        char location_code[4];
        char entrance_code[3];

        strncpy(location_code, destination, 3);
        location_code[3] = '\0';
        location_id = FindLevelIdByLocationCode(location_code);
        if (location_id == -1) {
            return;
        }
        strncpy(entrance_code, destination + 3, 2);
        entrance_code[2] = '\0';
        entrance = atoi(entrance_code);
    }
    ResetInactiveLevelDataVectors();

    if (location_id != current_location) {
        RequestLevelTransition(location_id, entrance,
                               m_lData1 < 0 ? 0 : static_cast<unsigned char>(m_lData1));
        return;
    }

    if (!named_entity) {
        Trigger* target = FindTriggerByName(destination);

        destination_position = target->position;
        angle = target->angle;
        destination_direction = target->direction;
    }

    SetWorldScenePosition(g_world.get(), &destination_position);

    rotation = glm::mat3(1);
    if (angle != 0.0f) {
        rotation *=
            glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                          normalizedOrZero(glm::vec3(destination_direction))));
    }
    ApplyCameraRotation(&rotation);
    SpawnCameraSpellEffect("set_portal", 1, 0, 0);
}

/* Materialize this trigger's item table once. The two dice fields are the
   item-count and gold rolls at ItemTable record offsets 0x1cd and 0x1d5.
   The trigger's persistent seed reseeds the CRT RNG before generation. */
// FUNCTION: WIZ8 0x00445500
void Trigger::GenerateItemGroup()
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    unsigned int table_id;
    unsigned int maximum_items;
    int index;

    if (inline_action_data[0] == '\0' || items_generated != 0) {
        return;
    }

    SeedRandom(item_group_seed);
    table_id = FindItemTableByName(inline_action_data);
    if (table_id == static_cast<unsigned int>(-1)) {
        return;
    }

    maximum_items = RollDice(&g_item_tables[table_id].item_count_dice);
    GenerateItemsFromTable(items, table_id, maximum_items, position);
    if (world_item_group == 0) {
        world_item_group = SpawnItem(0x23c, &position, 0);
    }
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        ItemInfoAddToGroup(world_item_group.get(), std::move(items[index]));
    }
    gold = RollDice(&g_item_tables[table_id].gold_dice);
    items_generated = 1;
}

/* The trigger's container world item, materialized on demand: when asked to
   create and none exists yet a bare container item is spawned into the world
   and remembered. */
// FUNCTION: WIZ8 0x00445670
W8WorldItem* Trigger::GetOrCreateItemGroup(bool create)
{
    if (create && world_item_group == 0) {
        world_item_group = SpawnItem(0x23c, &position, 0);
    }
    return world_item_group.get();
}

/* After a selected-prop Run: while g_trigger_feedback is clear, post either the
   special-item notice (required_item_id != -1) or the nothing-happened notice. */
// FUNCTION: WIZ8 0x004456E0
void Trigger::PrintNothingHappenedOrSpecialItemRequired()
{
    if (g_trigger_feedback) {
        return;
    }
    if (required_item_id != -1) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, g_localized_strings[0x96b]);
        return;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, g_localized_strings[0x964]);
}

/* Execute the selected Trigger action. The original keeps the three trigger
   kinds in one dispatcher: kind two handles invisible/timed actions first,
   kind one handles Prop-backed actions, and everything else falls through to
   the common action table. */
// FUNCTION: WIZ8 0x0043d940
void Trigger::Run(int source)
{
    ActivationCallback callback = activation_callback;
    bool apply_state_changes = true;
    bool action_succeeded = false;

    if (!SelectAction()) {
        return;
    }
    if (callback != 0 && !callback(this)) {
        return;
    }

    if (trigger_kind == 2) {
        switch (action) {
        case 0:
            goto commit_action;

        case 9:
            if (recipients.empty()) {
                break;
            }
            UpdateCameraPathStateByName(m_pWorld, recipients.c_str(), 1);
            FinishAction();
            goto commit_action;

        case 0x0a: {
            int camera = -1;
            int switch0;
            int switch1;
            int switch2;
            int switch3;
            int switch4;
            int switch5;
            int switch6;

            switch0 = GetLocationVarValueByName("Switch0");
            switch1 = GetLocationVarValueByName("Switch1");
            switch2 = GetLocationVarValueByName("Switch2");
            switch3 = GetLocationVarValueByName("Switch3");
            switch4 = GetLocationVarValueByName("Switch4");
            switch5 = GetLocationVarValueByName("Switch5");
            switch6 = GetLocationVarValueByName("Switch6");

            if (switch0 == 1 && switch1 == 1) {
                camera = 2;
            } else if (switch0 == 0 && switch1 == 1) {
                if (switch4 == 0 && switch6 == 1) {
                    camera = 1;
                } else if (switch3 == 1 && switch4 == 1) {
                    camera = 3;
                } else if (switch4 == 0 && switch6 == 0) {
                    camera = 4;
                } else if (switch3 == 0 && switch4 == 1) {
                    camera = 5;
                }
            } else if (switch1 == 0) {
                if (switch2 == 1) {
                    if (switch3 == 1 && switch5 == 1) {
                        camera = 6;
                    } else if (switch5 == 0 && switch6 == 0) {
                        camera = 7;
                    } else if (switch3 == 0 && switch5 == 1) {
                        camera = 9;
                    } else if (switch5 == 0 && switch6 == 1) {
                        camera = 10;
                    }
                } else if (switch2 == 0) {
                    camera = 8;
                }
            }

            if (camera != -1) {
                char name[24];

                snprintf(name, sizeof(name), "Camera0%d", camera);
                UpdateCameraPathStateByName(m_pWorld, name, 1);
            }
            FinishAction();
            goto commit_action;
        }

        case 0x0e:
            if (recipients.empty() || SDL_strcasecmp(recipients.c_str(), "party") != 0) {
                break;
            }
            ApplyItemEffectToRandomCharacter(Random(2) != 0 ? kConditionReaction
                                                            : kConditionReactionAlt,
                                             -1, 0, kCharacterEventNoFlags);
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x11:
            if ((flags & W8_TRIGGER_RUNNING) != 0) {
                break;
            }
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x22: {
            float previous_value = g_world->environment_intensity;

            m_pActionData = std::make_unique<W8EnvironmentTriggerActionData>();
            m_pActionData->type = W8_TRIGGER_PAYLOAD_ENVIRONMENT;
            static_cast<W8EnvironmentTriggerActionData*>(m_pActionData.get())
                ->previous_environment = previous_value;
            SetWorldEnvironmentIntensity(g_world.get(), 0.0f);
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;
        }

        case 0x23:
            if (m_pEvent == 0) {
                m_pEvent = std::make_unique<W8TriggerEvent>();
                m_pEvent->trigger = this;
                m_pEvent->action = static_cast<short>(action);
                m_pEvent->timer.SetDuration(0.5f);
            } else {
                if (std::ranges::contains(g_timed_events, m_pEvent.get())) {
                    srAssertFail("glsTimedEvents.Find(m_pEvent) == -1",
                                 "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x603, 0);
                }
            }
            m_pEvent->timer.Restart();
            if (m_pEvent->m_pCountdown.has_value()) {
                m_pEvent->m_pCountdown->Restart();
            }
            g_timed_events.push_back(m_pEvent.get());
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x34:
            if (recipients.empty() || recipients[0] == '\0') {
                break;
            }
            RunDestination(recipients.c_str());
            goto commit_action;

        default:
            break;
        }
    }

    if (trigger_kind == 1) {
        switch (action) {
        case 1: {
            W8DoorTriggerActionData* action_data = 0;

            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0 || state_index != 0 ||
                m_pProp->Rep()->animation_playing != 0) {
                break;
            }
            if (m_pActionData != 0 && m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR) {
                action_data = static_cast<W8DoorTriggerActionData*>(m_pActionData.get());
            }
            if (action_data != 0 && action_data->locked && action_data->item != -1) {
                if (!FindItemOnParty(action_data->item, 0, 0, 2, 0)) {
                    ShowNotice(W8_FONT_PALETTE_BLUE, "Your party doesn't have required key.");
                    break;
                }
                action_data->locked = false;
            }

            m_pProp->SetRepresentationActive(1, true);
            state_index = 1;
            if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
                m_pWorld->game_data->SetInterfaceState(surface_id, 1);
            }
            flags |= W8_TRIGGER_RUNNING;
            if (action_data != 0) {
                action_data->open = true;
            }

            if (m_lData1 != 0) {
                if (m_pEvent != 0 && std::ranges::contains(g_timed_events, m_pEvent.get())) {
                    m_pEvent->timer.Restart();
                    if (m_pEvent->m_pCountdown.has_value()) {
                        m_pEvent->m_pCountdown->Restart();
                    }
                    goto commit_action;
                }

                m_pEvent = std::make_unique<W8TriggerEvent>();
                m_pEvent->trigger = this;
                m_pEvent->action = 2;
                m_pEvent->timer.SetDuration(m_lData1 < 0 ? 10.0f : static_cast<float>(m_lData1));
                m_pEvent->timer.Restart();
                m_pEvent->repeat = true;
                g_timed_events.push_back(m_pEvent.get());
            }
            goto commit_action;
        }

        case 2: {
            bool active;

            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
                break;
            }
            active = m_pProp->Rep()->animation_playing;
            if (active) {
                break;
            }
            state_index = state_index == 1 ? 0 : 1;
            /* Retail starts the representation for both toggle directions; it
               never passes the new state, which would stop it on 1 -> 0. */
            m_pProp->SetRepresentationActive(1, true);
            if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
                m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
            }
            flags |= W8_TRIGGER_RUNNING;
            if (m_pActionData != 0 && m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR) {
                if (state_index == 0) {
                    static_cast<W8DoorTriggerActionData*>(m_pActionData.get())->open = false;
                } else {
                    static_cast<W8DoorTriggerActionData*>(m_pActionData.get())->open = true;
                }
            }
            goto commit_action;
        }

        case 0x2c: {
            W8DoorTriggerActionData* action_data = 0;
            bool was_active;

            if (m_pActionData != 0 && m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR) {
                action_data = static_cast<W8DoorTriggerActionData*>(m_pActionData.get());
            }
            if (action_data != 0 && action_data->locked && action_data->item != -1) {
                if (!FindItemOnParty(action_data->item, 0, 0, 2, 0)) {
                    ShowNotice(W8_FONT_PALETTE_BLUE, "Your party doesn't have required key.");
                    break;
                }
                action_data->locked = false;
            }
            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
                break;
            }
            was_active = m_pProp->Rep()->animation_playing;
            m_pProp->SetRepresentationActive(!was_active, true);
            state_index = state_index == 0;
            if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
                m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
            }
            if (!was_active) {
                flags |= W8_TRIGGER_RUNNING;
            } else {
                flags &= ~W8_TRIGGER_RUNNING;
            }
            if (action_data != 0) {
                action_data->open = (state_index & 1) != 0;
            }
            goto commit_action;
        }

        case 8: {
            signed char previous = static_cast<signed char>(state_index);

            if (state_count > 1) {
                signed char next = static_cast<signed char>(state_index) +
                                   static_cast<signed char>(state_direction);
                state_index = static_cast<unsigned char>(next);
                if (static_cast<unsigned char>(next) == state_count) {
                    if (cycle_bounce == 0) {
                        state_index = 0;
                    } else {
                        state_index = state_count - 2;
                        state_direction = 0xff;
                    }
                } else if (next < 0) {
                    state_index = 1;
                    state_direction = 1;
                }
            }

            if ((flags & W8_TRIGGER_ANIMATE_STATES) != 0) {
                W8AnimObj* animation;
                /* 0x0043E4C3 tests the AnimObjListCount result signed. */
                int count;
                int index;

                if (m_pProp == 0 || m_bRepType != W8_TRIGGER_REP_PROP) {
                    srAssertFail("m_pProp && m_bRepType == TRIGGER_REP_PROP",
                                 "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x592, 0);
                }
                animation = m_pProp->Rep()->Animation();
                if (animation->path_lists == 1) {
                    count = AnimObjListCount(animation, 2);
                    for (index = 0; index < count; ++index) {
                        W8PathAI* path =
                            AnimObjListEntry(animation, 2, static_cast<signed char>(index));
                        PathAIUpdate(path,
                                     previous <= static_cast<signed char>(state_index) ? 1 : -1);
                    }
                } else {
                    m_pProp->SetPendingAnimationSubcycle(static_cast<char>(state_index));
                }
            }
            goto commit_action;
        }

        case 0x37: {
            int tag = source == -1 ? m_lData1 : source;

            if (m_bRepType == W8_TRIGGER_REP_PROP && m_pProp != 0 && tag != -1) {
                /* Retail looks the slot up by the raw source argument, not by
                   the resolved tag, so a Run(-1) asks for tag 0xFF. */
                m_pProp->Rep()->SelectAnimationSlot(static_cast<unsigned char>(source));
                m_pProp->SetRepresentationActive(1, false);
                state_index = static_cast<unsigned char>(tag);
                goto commit_action;
            }
            break;
        }

        default:
            break;
        }
    }

    switch (action) {
    case 4:
    case 0x30:
    case 0x31: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        while (recipient != 0) {
            stLight* light = FindLightByName(NextTriggerRecipient(recipient).c_str());
            if (light != 0) {
                light->m_save_marked = true;
                if (action == 4) {
                    if (light->testFlag(srNode::FLAG_DISABLE) == 0) {
                        light->setFlag(srNode::FLAG_DISABLE);
                    } else {
                        light->clearFlag(srNode::FLAG_DISABLE);
                    }
                } else if (action == 0x30) {
                    if (light->testFlag(srNode::FLAG_DISABLE) != 0) {
                        light->clearFlag(srNode::FLAG_DISABLE);
                    }
                } else if (light->testFlag(srNode::FLAG_DISABLE) == 0) {
                    light->setFlag(srNode::FLAG_DISABLE);
                }
                action_succeeded = true;
            }
        }
        if (trigger_kind == 2) {
            flags |= W8_TRIGGER_RUNNING;
        }
        if (!action_succeeded) {
            return;
        }
        break;
    }

    case 0:
    case 0x38:
        break;

    case 0x2d:
    case 0x2e:
    case 0x2f: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0) {
            return;
        }
        while (recipient != 0) {
            Trigger* target = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (target != 0) {
                if (action == 0x2d) {
                    target->flags |= W8_TRIGGER_ON;
                } else if (action == 0x2e) {
                    target->flags &= ~W8_TRIGGER_ON;
                } else {
                    target->flags ^= W8_TRIGGER_ON;
                }
                action_succeeded = true;
            }
        }
        if (!action_succeeded) {
            return;
        }
        break;
    }

    case 3: {
        bool was_active;
        bool action_succeeded = true;

        if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
            return;
        }
        was_active = m_pProp->Rep()->animation_playing;

        if (inline_action_data[0] != '\0') {
            if (g_status.item_in_cursor) {
                glm::vec3 item_position;
                auto item = CreateWorldItem(&g_status.item_in_hand, &item_position,
                                            W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE);

                if (item != 0) {
                    if (world_item_group == 0) {
                        glm::vec3 group_position;
                        world_item_group = SpawnItem(0x23c, &group_position, 0);
                    }
                    ItemInfoAddToGroup(world_item_group.get(), std::move(item));
                }
                g_trigger_feedback = true;
                return;
            }
            if ((flags & W8_TRIGGER_ITEM_PICKER) != 0) {
                return;
            }

            g_trigger_feedback = true;
            if (items_generated == 0) {
                GenerateItemGroup();
            }
            if (world_item_group != 0) {
                int item_count = ItemInfoGetNumInGroup(world_item_group.get()) - 1;
                W8WorldItem* item;
                int contained_items = 0;

                if (item_count != 0 && m_pProp->Rep()->subcycle != 0) {
                    action_succeeded = false;
                }

                item = world_item_group->next.get();
                while (item != 0) {
                    ++contained_items;
                    if (!item->item.identified) {
                        PartyAttemptsToIdentifyItem(&item->item, 0);
                    }
                    item = item->next.get();
                }
                if (contained_items > 1) {
                    gXStatus.item_pick_pending = true;
                }

                if (gold != 0) {
                    AddPartyGold(gold, true);
                    gold = 0;
                }

                if (item_count == 0) {
                    if (m_pProp->Rep()->subcycle == 0) {
                        ApplyItemEffectToRandomCharacter(Random(2) != 0 ? g_container_event
                                                                        : g_container_event_alt,
                                                         -1, 0, kCharacterEventNoFlags);
                    }
                } else if (item_count == 1 && !g_status.item_in_cursor) {
                    item = world_item_group->next.get();
                    CopyItemInstance(&g_status.item_in_hand, &item->item, 0, true);
                    ItemInfoRemoveFromGroup(world_item_group.get(), item);
                    if (m_pProp->Rep()->subcycle != 0) {
                        goto toggle_item_prop;
                    }
                } else {
                    flags |= W8_TRIGGER_ITEM_PICKER;
                }

                if (!action_succeeded) {
                    return;
                }
            }
        }

    toggle_item_prop:
        m_pProp->SetRepresentationActive(!was_active, true);
        state_index = state_index == 0;
        if (m_pWorld->game_data != 0 && surface_id >= 0) {
            m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
        }
        if (!was_active) {
            flags |= W8_TRIGGER_RUNNING;
        } else {
            flags &= ~W8_TRIGGER_RUNNING;
        }
        break;
    }

    case 0x32:
    case 0x33:
        if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
            return;
        }
        if ((action == 0x32 && m_pProp->Rep()->animation_playing != 0) ||
            (action == 0x33 && m_pProp->Rep()->animation_playing == 0)) {
            return;
        }
        m_pProp->SetRepresentationActive(action == 0x32, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
            m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
        }
        if (action == 0x32) {
            flags |= W8_TRIGGER_RUNNING;
        } else {
            flags &= ~W8_TRIGGER_RUNNING;
        }
        break;

    case 0x0f: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0) {
            return;
        }
        while (recipient != 0) {
            Trigger* target = FindTriggerByName(NextTriggerRecipient(recipient).c_str());
            if (target != 0) {
                bool was_running = ((flags & W8_TRIGGER_RUNNING) != 0);
                flags |= W8_TRIGGER_RUNNING;
                target->Run(m_lData1);
                flags = (flags & ~W8_TRIGGER_RUNNING) | (was_running ? W8_TRIGGER_RUNNING : 0);
                action_succeeded = true;
            }
        }
        if (!action_succeeded) {
            return;
        }
        break;
    }

    case 0x10:
        SetCameraSwayMode(m_pWorld->camera, source > 0 ? 1 : -1);
        return;

    case 0x36:
        if (action_state == 4 && action_data_mode == 2) {
            PlayActionSound(alternate_action_data, 0);
        } else {
            PlayActionSound(this->action_data, 0);
        }
        return;

    case 0x24: {
        W8Dice dice;

        if (m_lData1 < 0) {
            m_lData1 = 1;
        }
        if (m_lData2 < 0) {
            m_lData2 = 6;
        }
        if (m_lData3 < 0) {
            m_lData3 = 2;
        }
        SetDice(&dice, static_cast<unsigned char>(m_lData1), static_cast<unsigned char>(m_lData2),
                static_cast<short>(m_lData3));
        ApplyRolledHealthChangeToParty(&dice, 0, 1);
        if (trigger_kind == 2) {
            flags |= W8_TRIGGER_RUNNING;
        }
        break;
    }

    case 0x0b: {
        W8MonsterGroup* group = 0;
        W8MonsterInfo* monster_info = 0;
        int monster_id;
        int index;

        if (recipients.empty()) {
            return;
        }
        monster_id = atoi(recipients.c_str());
        for (index = 0; index < static_cast<int>(gXStatus.monster_groups.size()); ++index) {
            group = gXStatus.monster_groups[index].get();
            if (group->monster_id == monster_id) {
                int location_id = group->MemberLocationId(0);
                monster_info = FindMonster(location_id);
                break;
            }
        }
        if (group == 0 || monster_info == 0) {
            return;
        }

        group->members_active = true;
        monster_info->p3D->m_pRep->active = 1;
        monster_info->p3D->m_pRep->animation_playing = 1;
        monster_info->p3D->m_pRep->timer = GameClockMilliseconds();
        monster_info->p3D->ResetRepresentation();
        monster_info->p3D->ResetPathAI();
        monster_info->p3D->reactivated = true;
        return;
    }

    case 0x39: {
        glm::vec3 party_position;
        W8TriggerShakeEvent* event;

        party_position = cameraLocation();
        if (m_pEvent == 0) {
            m_pEvent = std::make_unique<W8TriggerShakeEvent>();
            event = static_cast<W8TriggerShakeEvent*>(m_pEvent.get());
            if (m_pEvent == 0) {
                srAssertFail("m_pEvent", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp",
                             0x7c4, 0);
            }
            event->trigger = this;
            event->action = static_cast<short>(action);
            event->timer.SetDuration(m_lData2 == -1 ? 0.07f : m_lData2 * 0.001f);
            event->timer.Restart();
            event->intensity = m_lData1 == -1 ? 800 : m_lData1;

            if (m_lData3 != -1) {
                event->m_pCountdown.emplace();
                event->m_pCountdown->SetDuration(abs(m_lData3) * 0.001f);
                event->m_pCountdown->Restart();
                if (m_lData3 < 0) {
                    event->reverse = true;
                }
            }
        } else {
            event = static_cast<W8TriggerShakeEvent*>(m_pEvent.get());
            event->timer.Restart();
            if (event->m_pCountdown.has_value()) {
                event->m_pCountdown->Restart();
            }
        }

        if ((flags & W8_TRIGGER_RUNNING) == 0) {
            g_timed_events.push_back(m_pEvent.get());
            if (trigger_kind == 2) {
                flags |= W8_TRIGGER_RUNNING;
            } else if (m_lData3 == -1) {
                srAssertFail("m_lData3!=-1", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp",
                             0x7e4, "Non-invisible triggers with shake camera must be timed");
            }
        }
        break;
    }

    case 0x3a:
        if (m_pProp == 0 || source != m_lData1) {
            return;
        }
        m_pProp->SetActivationState(0);
        break;

    case 0x3b:
        if (m_pProp == 0 || source != m_lData1 || m_pProp->Rep()->animation_playing == 0) {
            return;
        }
        m_pProp->SetRepresentationActive(m_pProp->Rep()->animation_playing == 0, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
            m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
        }
        break;

    case 0x3c:
        if (m_pProp == 0 || source != m_lData1) {
            return;
        }
        m_pProp->SetRepresentationActive(m_pProp->Rep()->animation_playing == 0, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->game_data != 0 && surface_id >= 0) {
            m_pWorld->game_data->SetInterfaceState(surface_id, state_index);
        }
        break;

    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b: {
        if (uses_remaining != 0 || m_lData1 == -1) {
            if (action == 0x25) {
                RestorePartyStaminaByDice(0, 0, static_cast<short>(m_lData3));
                PlayActionSound("Data\\Sound\\misc\\fountain_magic.wav", 0);
            } else if (action == 0x26) {
                HealPartyByDice(0, 0, static_cast<short>(m_lData3));
                PlayActionSound("Data\\Sound\\misc\\fountain_magic.wav", 0);
            } else if (action == 0x27) {
                RestorePartySpellPoints(m_lData3);
                ShowString(g_localized_strings[0x722]);
                PlayActionSound("Data\\Sound\\misc\\fountain_magic.wav", 0);
            } else {
                W8SpellId spell_id;
                glm::dvec3 position = g_world->camera->getLocation();

                if (action == 0x28) {
                    spell_id = W8_SPELL_HEX;
                } else if (action == 0x29) {
                    spell_id = W8_SPELL_SLEEP;
                } else if (action == 0x2a) {
                    spell_id = W8_SPELL_NOXIOUS_FUMES;
                } else {
                    spell_id = W8_SPELL_HEAL_ALL;
                }
                PointCastSpell(glm::vec3(static_cast<float>(position.x),
                                         static_cast<float>(position.y),
                                         static_cast<float>(position.z)),
                               spell_id, static_cast<unsigned int>(m_lData3));
                if (action == 0x2b) {
                    RemoveAllConditionsFromParty();
                }
            }
        }

        if (uses_remaining == 0) {
            if (m_lData1 != -1) {
                return;
            }
        } else {
            --uses_remaining;
            if (m_lData2 > 0 && m_pEvent == 0) {
                m_pEvent = std::make_unique<W8TriggerEvent>();
                m_pEvent->action = static_cast<short>(action);
                m_pEvent->timer.SetDuration(m_lData2 * 720.0f);
                m_pEvent->timer.Restart();
                m_pEvent->trigger = this;
                m_pEvent->timer.SetMode(W8_TIMER_CLOCK_GAME);
                g_timed_events.push_back(m_pEvent.get());
            }
        }
        if (trigger_kind == 2) {
            flags |= W8_TRIGGER_RUNNING;
        }
        break;
    }

    case 0x0c:
        if (m_lData1 < 0) {
            return;
        }
        if (m_lData2 == 0) {
            glm::vec3 source_position;
            glm::vec3 target_position;
            glm::vec3 transformed;
            glm::mat3 rotation;

            source_position = glm::vec3(this->position.x, this->position.y, this->position.z);
            target_position = source_position;
            target_position.z += 100.0f;
            rotation = glm::mat3(1);
            /* Retail rotates the identity matrix around the trigger's direction
               field (ebp+0x100), not around identity Z. */
            if (angle != 0.0f) {
                rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                          normalizedOrZero(glm::vec3(direction))));
            }
            transformed = rotation * target_position;
            FireMissile(static_cast<unsigned int>(m_lData1), &source_position, &transformed, 0, 1,
                        1, 50000.0f);
        } else if (m_pEvent == 0) {
            float duration = abs(m_lData2) * 0.001f;

            if (m_lData2 > 0 && trigger_kind != 2) {
                srAssertFail("0", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x87f,
                             "Continous firing missile must be invisible trigger.");
            }
            m_pEvent = std::make_unique<W8TriggerEvent>();
            m_pEvent->action = static_cast<short>(action);
            m_pEvent->timer.SetDuration(duration);
            m_pEvent->timer.Restart();
            m_pEvent->trigger = this;
            flags |= W8_TRIGGER_RUNNING;
            g_timed_events.push_back(m_pEvent.get());
        }
        break;

    case 0x3d:
        if (m_pProp == 0) {
            return;
        }
        m_pProp->SetActivationState(1);
        break;

    case 0x3e:
        if (m_pProp == 0) {
            return;
        }
        m_pProp->SetActivationState(0);
        break;

    case 0x3f:
        if (m_pEvent == 0) {
            m_pEvent = std::make_unique<W8TriggerEvent>();
            m_pEvent->action = static_cast<short>(action);
            m_pEvent->timer.SetDuration(m_lData1 * 0.001f);
            m_pEvent->timer.Restart();
            m_pEvent->trigger = this;
            m_pEvent->repeat = true;
        } else {
            if (std::ranges::contains(g_timed_events, m_pEvent.get())) {
                srAssertFail("glsTimedEvents.Find(m_pEvent) == -1",
                             "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x8ae, 0);
            }
            m_pEvent->timer.Restart();
            if (m_pEvent->m_pCountdown.has_value()) {
                m_pEvent->m_pCountdown->Restart();
            }
        }
        g_timed_events.push_back(m_pEvent.get());
        if (trigger_kind == 2) {
            flags |= W8_TRIGGER_RUNNING;
        }
        break;

    case 0x40:
        if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
            return;
        }
        if (!state_to_modify.empty()) {
            char state_name[132];

            snprintf(state_name, sizeof(state_name), "%s%d", state_to_modify.c_str(),
                     static_cast<int>(static_cast<signed char>(state_index)));
            SetTriggerVariableByName(state_name, 0);
        }
        state_index = m_pProp->Rep()->AdvanceAnimationSegment();
        m_pProp->SetRepresentationActive(1, false);
        if (!state_to_modify.empty()) {
            char state_name[132];

            snprintf(state_name, sizeof(state_name), "%s%d", state_to_modify.c_str(),
                     static_cast<int>(static_cast<signed char>(state_index)));
            SetTriggerVariableByName(state_name, 1);
        }
        apply_state_changes = false;
        break;

    case 0x41:
    case 0x42:
    case 0x43: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0) {
            return;
        }
        while (recipient != 0) {
            const auto name = NextTriggerRecipient(recipient);
            if (action == 0x41) {
                PositionAmbientSoundByName(g_world.get(), name.c_str());
            } else if (action == 0x42) {
                StopAmbientSoundByName(g_world.get(), name.c_str());
            } else {
                ToggleAmbientSoundByName(name.c_str());
            }
            action_succeeded = true;
        }
        break;
    }

    case 0x44:
    case 0x45:
    case 0x46: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0) {
            return;
        }
        while (recipient != 0) {
            stParticle* particle = FindParticleByName(NextTriggerRecipient(recipient).c_str());
            if (particle != 0) {
                particle->persisted = true;
                if (action == 0x44) {
                    particle->SetActive(1);
                } else if (action == 0x45) {
                    particle->SetActive(0);
                } else {
                    particle->SetActive(particle->emitting == 0);
                }
                action_succeeded = true;
            }
        }
        if (!action_succeeded) {
            return;
        }
        break;
    }

    case 0x47: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0 || m_pEvent != 0) {
            return;
        }
        action_succeeded = false;
        while (recipient != 0) {
            stParticle* particle = FindParticleByName(NextTriggerRecipient(recipient).c_str());
            if (particle != 0) {
                particle->persisted = true;
                particle->SetActive(1);
                action_succeeded = true;
            }
        }
        if (!action_succeeded) {
            return;
        }

        m_pEvent = std::make_unique<W8TriggerEvent>();
        g_timed_events.push_back(m_pEvent.get());
        m_pEvent->trigger = this;
        m_pEvent->action = static_cast<short>(action);
        m_pEvent->m_pCountdown.emplace();
        m_pEvent->m_pCountdown->SetDuration(m_lData1 == -1 ? 10.0f : m_lData1 * 0.001f);
        m_pEvent->m_pCountdown->Restart();
        m_pEvent->repeat = true;
        break;
    }

    case 0x48: {
        unsigned int count;

        if (m_pProp == 0 || m_lData1 < 0) {
            return;
        }
        count = AnimObjValue(m_pProp->Rep()->Animation(), 2);
        if (static_cast<int>(count) <= m_lData1) {
            return;
        }
        m_pProp->SetPendingAnimationSubcycle(static_cast<char>(m_lData1));
        break;
    }

    case 0x49:
        if (m_pProp == 0 || m_lData1 < 0) {
            return;
        }
        m_pProp->SetAnimationSpeed(static_cast<float>(m_lData1));
        break;

    case 0x4a: {
        const char* recipient = (recipients.empty() ? nullptr : recipients.c_str());

        if (recipient == 0 || m_lData1 < 0) {
            return;
        }
        while (recipient != 0) {
            W8Prop* prop = FindPropByName(g_world.get(), NextTriggerRecipient(recipient).c_str());
            if (prop != 0) {
                prop->SetAnimationSpeed(static_cast<float>(m_lData1));
            }
        }
        break;
    }

    case 0x4b: {
        const char* recipient;
        std::string destination;
        unsigned char count = 0;
        unsigned char selected;
        unsigned char index = 0;

        if (recipients.empty() || recipients[0] == '\0') {
            return;
        }
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        while (recipient != 0) {
            NextTriggerRecipient(recipient);
            ++count;
        }
        selected = static_cast<unsigned char>(w8_get_ticks() % count);
        recipient = (recipients.empty() ? nullptr : recipients.c_str());
        do {
            destination = NextTriggerRecipient(recipient);
            if (index == selected) {
                break;
            }
            ++index;
        } while (recipient != 0);
        RunDestination(destination.c_str());
        break;
    }

    default:
        return;
    }

commit_action:
    if (!running) {
        g_trigger_feedback = true;
    }
    CommitActionResult(apply_state_changes);
}

// FUNCTION: WIZ8 0x00444600
bool Trigger::CanRunLinkedTriggers()
{
    const char* recipient;

    if (m_pProp != 0 && m_pProp->Rep()->animation_playing != 0) {
        return false;
    }
    recipient = (recipients.empty() ? nullptr : recipients.c_str());
    while (recipient != 0) {
        Trigger* trigger;

        const auto recipient_name = NextTriggerRecipient(recipient);
        trigger = FindTriggerByName(recipient_name.c_str());
        if (trigger != 0 && !trigger->CanRunLinkedTriggers()) {
            return false;
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x0043d340
bool Trigger::SelectAction()
{
    bool fallback_selected = false;
    bool result = true;

    if (g_combat_inactive == 0 && m_pActionData != 0 &&
        m_pActionData->type == W8_TRIGGER_PAYLOAD_DOOR &&
        static_cast<W8DoorTriggerActionData*>(m_pActionData.get())->open) {
        return false;
    }

    if (((flags & W8_TRIGGER_RUNNING) != 0 && action != 0x39) || (flags & W8_TRIGGER_ON) == 0 ||
        ((flags & W8_TRIGGER_ONCE) != 0 && (flags & W8_TRIGGER_FIRED) != 0)) {
        action_state = 1;
        return false;
    }

    if (!required_states.empty()) {
        const char* cursor = required_states.c_str();
        while (cursor) {
            const auto state_name = NextTriggerRecipient(cursor);
            const int state_id = FindModifiedLocationVar(state_name.c_str(), 4317);
            if (location_variables[state_id].value == 0) {
                action = fallback_action;
                action_state = 4;
                fallback_selected = true;
                break;
            }
        }
    }

    if (m_pActionData == 0 || m_pActionData->type != W8_TRIGGER_PAYLOAD_DOOR) {
        if (required_item_id >= 0) {
            if (GetItemInHand() == required_item_id) {
                if ((flags & W8_TRIGGER_CONSUME_ITEM) != 0) {
                    RemovePartyItemByID(required_item_id, false);
                    required_item_id = -1;
                }
            } else {
                if (m_lData2 == 1 && activation_callback != 0) {
                    activation_callback(this);
                }
                action = fallback_action;
                action_state = 4;
                fallback_selected = true;
            }
        }
    } else {
        W8DoorTriggerActionData* action_data =
            static_cast<W8DoorTriggerActionData*>(m_pActionData.get());
        bool linked_trigger_blocked = false;

        if (m_pProp != 0 && m_pProp->Rep()->animation_playing != 0) {
            linked_trigger_blocked = true;
        } else {
            const char* cursor = (recipients.empty() ? nullptr : recipients.c_str());
            while (cursor) {
                const auto name = NextTriggerRecipient(cursor);
                Trigger* trigger = FindTriggerByName(name.c_str());
                if (trigger != 0 && !trigger->CanRunLinkedTriggers()) {
                    linked_trigger_blocked = true;
                    break;
                }
            }
        }

        if (linked_trigger_blocked) {
            if (m_pEvent == 0 || !std::ranges::contains(g_timed_events, m_pEvent.get())) {
                return false;
            }
            m_pEvent->timer.Restart();
            if (m_pEvent->m_pCountdown.has_value()) {
                m_pEvent->m_pCountdown->Restart();
            }
            if (!running) {
                g_trigger_feedback = true;
            }
            return false;
        }

        if (action_data->locked && action_data->item != -1) {
            if (GetItemInHand() != action_data->item) {
                if (m_lData2 == 1 && activation_callback != 0) {
                    activation_callback(this);
                }
                action = fallback_action;
                action_state = 4;
                fallback_selected = true;
            } else {
                lock_state.device_state.completed = 1;
                action_data->locked = false;
                if (action_data->linked_trigger[0] != '\0') {
                    Trigger* linked_trigger;
                    action_state = 1;
                    result = false;
                    linked_trigger = FindTriggerByName(action_data->linked_trigger);
                    if (linked_trigger != 0) {
                        linked_trigger->Run(-1);
                        if (!running) {
                            g_trigger_feedback = true;
                        }
                    }
                }
            }
        }
    }

    if (lock_state.lock_type != 0 && lock_state.device_state.completed == 0 && !running) {
        if (lock_state.lock_type == 1) {
            g_trigger_feedback = true;
            OpenLockInteraction(this);
            return false;
        }
        if (lock_state.lock_type == 2) {
            g_trigger_feedback = true;
            OpenTrapInteraction(this);
            return false;
        }
    }

    if (!fallback_selected) {
        if ((flags & W8_TRIGGER_USE_ALTERNATE) == 0) {
            action = initial_action;
            action_state = 2;
            if ((flags & W8_TRIGGER_HAS_ALTERNATE) != 0) {
                flags |= W8_TRIGGER_USE_ALTERNATE;
            }
        } else {
            action = alternate_action;
            action_state = 3;
            if ((flags & W8_TRIGGER_ALTERNATE_TOGGLES) != 0) {
                flags &= ~W8_TRIGGER_USE_ALTERNATE;
            }
        }
    } else if (action == 0) {
        action_state = 1;
        return false;
    }
    return result;
}

// FUNCTION: WIZ8 0x0043bca0
Trigger::~Trigger()
{
    UnregisterSearchableTrigger(this);
    if (m_pEvent) wiz8::erase_first(g_timed_events, m_pEvent.get());
}

// FUNCTION: WIZ8 0x00445a10
stLight* FindLightByName(const char* name)
{
    return FindWorldNode<stLight>(name);
}

// FUNCTION: WIZ8 0x00443a50
int ResetNextTriggerId(void)
{
    g_status.next_trigger_id = 1;
    return 1;
}

// FUNCTION: WIZ8 0x00443A60
void DestroyAllWorldTriggers(W8World* world)
{
    if (world != 0) {
        if (world == g_world.get()) {
            g_timed_events.clear();
            standalone_events.clear();
        }
        world->triggers.clear();
    }
}

// FUNCTION: WIZ8 0x00444170
int GetLocationVarIDByName(const char* name)
{
    const auto found = std::find_if(location_variables.begin(), location_variables.end(),
                                    [name](const auto& variable) {
                                        return variable.level == g_status.current_level &&
                                               SDL_strcasecmp(variable.name.c_str(), name) == 0;
                                    });
    return found == location_variables.end() ? -1
                                             : static_cast<int>(found - location_variables.begin());
}

// FUNCTION: WIZ8 0x00443dc0
void CreateLocationVar(const char* name, int value)
{
    if (!name) throw std::invalid_argument("null location variable name");
    if (GetLocationVarIDByName(name) >= 0) return;
    if (location_variables.size() >= std::numeric_limits<std::int32_t>::max())
        throw std::length_error("Too many location variables");
    location_variables.push_back({name, value, g_status.current_level});
}

// FUNCTION: WIZ8 0x004440d0
int GetLocationVarValueByName(const char* name)
{
    const int index = GetLocationVarIDByName(name);
    if (index < 0) srAssertFail("Unknown location variable", __FILE__, __LINE__, "%s", name);
    return location_variables[index].value;
}

// Retail records contain a four-byte value, a 128-byte C string, and a four-byte level.
// FUNCTION: WIZ8 0x004441e0
void SaveLocationVariables(wiz8::File& handle)
{
    const auto count = static_cast<std::int32_t>(location_variables.size());
    handle.write(&count, sizeof(count));
    for (const auto& variable : location_variables) {
        const std::int32_t value = variable.value;
        const std::int32_t level = variable.level;
        handle.write(&value, sizeof(value));
        handle.write_padded(variable.name, 128);
        handle.write(&level, sizeof(level));
    }
}

// Append only after the complete section has been decoded and validated.
// FUNCTION: WIZ8 0x00444310
bool LoadLocationVariables(W8Chunk& chunk)
try {
    std::int32_t count;
    chunk.ReadBytes(&count, sizeof(count));
    constexpr std::int64_t record_bytes = 4 + 128 + 4;
    const auto remaining = chunk.RemainingBytes();
    if (count < 0 || remaining < 0 || count > remaining / record_bytes ||
        std::cmp_greater(location_variables.size(),
                         std::numeric_limits<std::int32_t>::max() - count))
        return false;
    std::vector<LocationVariable> loaded;
    loaded.reserve(count);
    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t value, level;
        std::array<char, 128> name;
        chunk.ReadBytes(&value, sizeof(value));
        chunk.ReadBytes(name.data(), name.size());
        chunk.ReadBytes(&level, sizeof(level));
        const auto terminator = std::find(name.begin(), name.end(), '\0');
        if (terminator == name.end()) return false;
        loaded.push_back({std::string(name.begin(), terminator), value, level});
    }
    location_variables.insert(location_variables.end(), std::make_move_iterator(loaded.begin()),
                              std::make_move_iterator(loaded.end()));
    return true;
} catch (const std::exception&) {
    return false;
}

bool HasLocationVariables()
{
    return !location_variables.empty();
}

// FUNCTION: WIZ8 0x004445b0
void ClearLocationVariables()
{
    location_variables.clear();
}

/* Index of the prop whose trigger last tested in view; -1 until a prop
   matches. */
// GLOBAL: WIZ8 0x00606998
static int s_last_prop_index = -1;

/* Report whether any world prop's trigger representation is within its
   activation range and projects onto the screen. The remembered index is
   checked first so consecutive frames start at the prop that matched. */
// FUNCTION: WIZ8 0x00445140
bool AnyPropTriggerInView()
{
    glm::vec3 position;
    unsigned int prop_count;
    int index;

    if (g_world == nullptr) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\Trigger.cpp", 0x1395, 0);
    }
    position = cameraLocation();
    prop_count = g_world->plsProps.size();
    if (0 <= s_last_prop_index && s_last_prop_index < static_cast<int>(prop_count)) {
        W8Prop* prop = GetWorldProp(g_world.get(), s_last_prop_index);

        if (prop->IsTriggerInView(&position)) {
            return true;
        }
    }
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        W8Prop* prop = GetWorldProp(g_world.get(), index);

        if (prop->IsTriggerInView(&position)) {
            s_last_prop_index = index;
            return true;
        }
    }
    return false;
}

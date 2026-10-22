#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/locks.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/math.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/string_database.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

class W8DialogBase;

std::vector<std::string> ParseTriggerNames(std::string_view text)
{
    std::vector<std::string> names;
    if (text.empty()) return names;
    for (;;) {
        const auto comma = text.find(',');
        names.emplace_back(text.substr(0, comma));
        if (comma == text.npos) return names;
        text.remove_prefix(comma + 1);
    }
}

bool g_trigger_feedback = true;

int g_container_event_alt = kFirstRemappedEvent + 13;

int g_container_event = kFirstRemappedEvent + 14;

/* Trigger names are stored uppercase. Like the retail instance registry, the
   newest trigger wins when a name repeats. */
Trigger* FindTriggerInWorld(World& world, std::string_view name)
{
    if (name.empty()) return nullptr;
    for (const auto& trigger : world.triggers | std::views::reverse)
        if (CompareTextIgnoreAsciiCase(trigger->name, name) == 0) return trigger.get();
    return nullptr;
}

Trigger* FindTriggerByName(std::string_view name)
{
    for (auto* world : {GetWorld(), GetSecondaryWorld()})
        if (world)
            if (auto* trigger = FindTriggerInWorld(*world, name)) return trigger;
    return nullptr;
}

Trigger* FindRequiredTriggerByName(std::string_view name)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger == nullptr) throw std::runtime_error(fmt::format("missing trigger {}", name));
    return trigger;
}

/* Resolve the trigger that drives a prop's use action. A kind-one or kind-two
   trigger without type-10 action data is followed along its recipient chain —
   each step picks the first recipient that is not the link we came from — until
   a trigger carrying type-10 action data is found, five hops at most. */
Trigger* FindTriggerForProp(World* world, Prop* prop)
{
    Trigger* trigger = prop->GetTrigger();

    if (trigger != 0) {
        if (trigger->doorData() != nullptr) {
            return trigger;
        }
        if (trigger->trigger_kind == W8TriggerKind::item ||
            trigger->trigger_kind == W8TriggerKind::prop) {
            int trigger_count = static_cast<int>(world->triggers.size());

            for (int index = 0; index < trigger_count; ++index) {
                Trigger* other = world->triggers[index].get();
                Trigger* previous = trigger;
                int hops = 0;

                if (other == trigger) {
                    continue;
                }
                while (hops < 5 && !other->recipients.empty() &&
                       (other->trigger_kind == W8TriggerKind::item ||
                        other->trigger_kind == W8TriggerKind::prop)) {
                    if (std::ranges::none_of(other->recipients, [previous](const auto& name) {
                            return FindTriggerByName(name) == previous;
                        }))
                        goto next_trigger;
                    if (other->doorData() != nullptr) {
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

/* True while a type-0x34 destination trigger holds (x, y, z) inside its
   activation annulus: under range_maximum and, unless flags bit 6
   waives the minimum, at or beyond range_minimum. */
// FUNCTION: WIZ8 0x00445940
bool InsideDestinationTrigger(const World& world, float x, float y, float z)
{
    int count = static_cast<int>(world.triggers.size());

    for (int index = 0; index < count; ++index) {
        Trigger* trigger = world.triggers[index].get();
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

/* Saves record lock progress only; the full lock record form (restoring
   flag set) is still accepted on load. */
TriggerEvent::TriggerEvent() : action(-1), timer(), trigger(0), repeat(0), completed(0) {}

TriggerEvent::~TriggerEvent()
{
    if (shake && shake->effect) CameraShakes().Stop(*shake->effect);
}

glm::vec3 g_trigger_camera;

/* Advance every world trigger: raise the item picker when a prop-bearing
   activation asks for one, then run proximity activations for kind-two
   triggers. The camera position is cached when no trigger fired. */
void UpdateWorldTriggers(World* world)
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
             trigger->m_pProp->animation_playing == 0)) {
            trigger->GenerateItemGroup();
            if (g_modal_owner == 0 && trigger->world_item_group != 0) {
                auto dialog = std::make_unique<W8TriggerItemPickerDialog>();
                dialog->SetItemGroup(trigger->world_item_group.get());
                dialog->on_complete = [trigger](W8WorldItem*) {
                    trigger->flags &= ~W8_TRIGGER_ITEM_PICKER;
                };
                CurrentGame().item_pick_pending = false;
                OpenModal(std::move(dialog));
            }
        }
        if (g_environment_load_flag != 0 && trigger->trigger_kind == W8TriggerKind::prop &&
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
    if (world == GetWorld() && !activated) {
        g_trigger_camera = camera;
    }
}

TriggerEvent* World::findTimedAction(const Trigger* trigger) const
{
    for (const auto& action : timed_events)
        if (action->trigger == trigger && !action->completed) return action.get();
    return nullptr;
}

TriggerEvent& World::addTimedAction(Trigger* trigger)
{
    if (auto* previous = findTimedAction(trigger)) previous->completed = true;
    auto action = std::make_unique<TriggerEvent>();
    action->trigger = trigger;
    auto& result = *action;
    timed_events.push_back(std::move(action));
    return result;
}

void UpdateTimedTriggerEvents(void)
{
    for (auto* world : {GetSecondaryWorld(), GetWorld()}) {
        if (!world) continue;
        auto& events = world->timed_events;
        // Re-read size: actions appended by this step run in this step, in order.
        for (std::size_t index = 0; index < events.size();) {
            auto* event = events[index].get();
            event->Update();
            if (event->completed)
                events.erase(events.begin() + index);
            else
                ++index;
        }
    }
}

void TriggerEvent::UpdateShake()
{
    auto& effect = shake->effect;
    if (!effect) {
        float intensity = shake->intensity / 2.5e+02f;

        if (intensity < 1.0f) {
            intensity = 1.0f;
        }
        effect.emplace(m_pCountdown ? m_pCountdown->m_duration_seconds : 1.0f, false, intensity, 0,
                       nullptr);
        effect->continuous = !m_pCountdown;
        if (shake->reverse) {
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

void Trigger::CompleteItemInteraction()
{
    lock_state.device_state.completed = 1;
    if (auto* door = doorData()) {
        door->locked = false;
    }
}

void Trigger::Activate()
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    if (const auto* door = doorData();
        door &&
        (lock_state.lock_type == LockType::none || lock_state.device_state.completed != 0) &&
        !door->locked) {
        if (!door->open) {
            running = true;
            Run(-1);
            running = false;
        } else if (timed_action) {
            timed_action->timer.Restart();
            if (timed_action->m_pCountdown) timed_action->m_pCountdown->Restart();
        }
    }
}

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
        std::vector<std::uint32_t> locations;
        extent = glm::vec3(radius, radius, radius);
        lower = center - extent;
        upper = center + extent;

        locations =
            GetWorldSpatialIndex()->queryObjects({lower, upper}, W8_OCTREE_KIND_LOCATION, -1);
        const auto count = locations.size();
        for (unsigned int index = 0; index < count; ++index) {
            int location_id = locations[index];
            if (location_id == 0) {
                break;
            }
            W8MonsterInfo* monster_info = FindMonster(location_id);
            if (monster_info != 0 && monster_info->entity) {
                glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
                if (glm::length((monster_position - center)) <= radius) {
                    return true;
                }
            }
        }
    }

    if (include_party) {
        glm::vec3 party_position = PartyNavigator()->GetPosition();
        if (glm::length((party_position - center)) <= radius) {
            return true;
        }
    }
    return false;
}

bool Trigger::PlayActionSound(std::string_view sound_name, int volume)
{
    glm::vec3 position;

    if (volume == 0) {
        volume = 100;
    }
    if ((flags & W8_TRIGGER_POSITIONED) == 0) {
        if (m_pProp == 0) {
            wiz8::sound::play(sound_name, {.volume = std::uint32_t(volume)});
            return false;
        }
        const auto center = m_pProp->GetCenterPosition();
        if (!center) return false;
        position = *center;
    } else {
        position = glm::vec3(this->position.x, this->position.y, this->position.z);
    }

    auto sound = std::make_unique<Sound3D>(sound_name);
    sound->volume = volume;
    sound->position = position;
    if (!sound->Play(false)) return false;
    if (!GetWorld()) return false;
    GetWorld()->audio.one_shots.push_back(std::move(sound));
    return true;
}

/* Timed actions pause their private timer with the game clock, then dispatch
   the small set of delayed Trigger effects once the timer completes. */
void TriggerEvent::Update()
{
    if (completed) return;
    if (shake) {
        UpdateShake();
        return;
    }
    if (action == 2) {
        W8GameTimer::Flags flags = timer.m_flags;

        if (CurrentGame().combat.has_value()) {
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
        Trigger* target = FindTriggerByName(trigger->recipient_name.c_str());
        if (target != 0) {
            target->Run(-1);
        }
        break;
    }

    case 0x47: {
        for (const auto& recipient_name : trigger->recipients) {
            ParticleSystem* particle = FindParticleByName(recipient_name.c_str());
            if (particle != 0) {
                particle->persisted = true;
                particle->SetActive(false);
            }
        }
        break;
    }

    case 2:
        if (const auto* door = trigger->doorData(); door && door->open) {
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
void InitializeStateDrivenPropVariables(Trigger* trigger)
{
    int slot;

    if (trigger->state_to_modify.empty()) {
        return;
    }
    for (slot = 0; slot < static_cast<signed char>(trigger->m_pProp->slots.size()); ++slot) {
        char name[132];

        assert(trigger->m_bRepType == W8_TRIGGER_REP_PROP);
        snprintf(name, sizeof(name), "%s%d", trigger->state_to_modify.c_str(), slot);
        CreateLocationVar(name, slot == 0);
    }
}

void SetTriggerVariableByName(std::string_view name, int value)
{
    const int index = GetLocationVarIDByName(name);
    if (index < 0) throw std::out_of_range(fmt::format("unknown location variable {}", name));
    CurrentGame().story.location_variables[index].value = value;
}

/* Clear the running bit and run every comma-separated recipient trigger once
   when the link-out and state-gate bits are set. */
void Trigger::RunLinkedTriggers()
{

    flags &= ~W8_TRIGGER_RUNNING;
    if ((flags & W8_TRIGGER_FIRE_LINKED) != 0 && (flags & W8_TRIGGER_LINK_ON_DEACTIVATE) != 0 &&
        !recipients.empty()) {
        for (const auto& recipient : recipients) {
            Trigger* trigger = FindTriggerByName(recipient.c_str());
            if (trigger != 0) {
                trigger->Run(-1);
            }
        }
    }
}

void Trigger::GetPosition(glm::vec3* position) const
{
    *position = this->position;
}

bool Trigger::HasActionMessage()
{
    return (flags & W8_TRIGGER_CAN_RUN_LINKED) != 0;
}

bool Trigger::RequiresItem()
{
    if (required_item_id >= 0) {
        return true;
    }
    if (const auto* door = doorData(); door && door->item != -1) {
        return true;
    }
    return false;
}

Trigger::~Trigger() = default;

Trigger::Trigger(World* world)
{
    trigger_kind = W8TriggerKind::none;
    trigger_id = 0;
    flags = 0;
    range_minimum = 0.0f;
    range_maximum = 0.0f;
    action_value = 0;
    state_count = 0;
    state_index = 0;
    state_direction = W8TriggerStateDirection::stopped;
    cycle_bounce = 0;
    state_mod_mode = W8TriggerStateModificationMode::none;
    searchable = 0;
    angle = 0.0f;
    m_bRepType = W8_TRIGGER_REP_NONE;
    m_pProp = 0;
    m_pWorld = world;
    initial_action = 0;
    alternate_action = 0;
    fallback_action = 0;
    action = 0;
    action_state = W8TriggerActionState::inactive;
    world_item_group = 0;
    items_generated = 0;
    activation_callback = nullptr;
    running = false;

    surface_id = -1;
    sound_volume = -1;
    required_item_id = -1;
    lock_state = {.device_id = -1, .key_id = -1, .last_interaction_clock = -1};

    flags |= W8_TRIGGER_ON;
    name[0] = 0;
    position = glm::vec3(0);
    action_data[0] = 0;
    action_data_mode = W8TriggerActionDataMode::toggle_alternate;
    item_group_seed =
        static_cast<unsigned int>(MonotonicMilliseconds() + CurrentGame().rng.below(30000));
    gold = 0;
    uses_remaining = 0;
    trigger_id = world ? world->next_trigger_id++ : 0;
}

void Trigger::UpdateActionAnimation()
{
    char* action_data = this->action_data;

    if ((flags & W8_TRIGGER_ANIMATE_ACTION) == 0 && (flags & W8_TRIGGER_ALTERNATE_ACTION) == 0) {
        return;
    }
    if ((flags & W8_TRIGGER_ALTERNATE_ACTION) != 0) {
        if (action_data_mode == W8TriggerActionDataMode::toggle_alternate) {
            if ((flags & W8_TRIGGER_ALTERNATE_SELECTED) == 0) {
                flags |= W8_TRIGGER_ALTERNATE_SELECTED;
            } else {
                action_data = alternate_action_data;
                flags &= ~W8_TRIGGER_ALTERNATE_SELECTED;
            }
        } else if (action_data_mode == W8TriggerActionDataMode::alternate_action) {
            if (action == alternate_action) {
                PlayActionSound(alternate_action_data, sound_volume);
                return;
            }
        } else if (action_data_mode == W8TriggerActionDataMode::fallback_action &&
                   action == fallback_action) {
            PlayActionSound(alternate_action_data, sound_volume);
            return;
        }
    }
    PlayActionSound(action_data, sound_volume);
}

void Trigger::FinishAction()
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    bool was_running = ((flags & W8_TRIGGER_RUNNING) != 0);
    bool action_completed = false;

    flags &= ~W8_TRIGGER_RUNNING;

    if (trigger_kind == W8TriggerKind::item) {
        if (action != 0x39) {
            goto reactivate_linked_triggers;
        }
        if (timed_action != 0) {
            timed_action->completed = true;
        }
        g_camera_shake_active = false;
        goto finish_linked_triggers;
    }

    if (trigger_kind != W8TriggerKind::prop) {
        goto reactivate_linked_triggers;
    }

    switch (action) {
    case 0x0c:
        if (timed_action != 0 && m_lData2 > 0) {
            timed_action->completed = true;
        }
        action_completed = true;
        break;

    case 0x23: {
        // Action 0x23 is reconstructed with its 0.5-second period on reactivation.
        if (timed_action) timed_action->completed = true;
        action_completed = true;
        break;
    }

    case 0x39:
        if (timed_action != 0) {
            timed_action->completed = true;
        }
        g_camera_shake_active = false;
        action_completed = true;
        break;
    }

    if ((flags & W8_TRIGGER_KEEP_ON_FINISH) == 0 && action != 0) {
        if (action == 4) {
            action_completed = false;
            for (const auto& recipient : recipients) {
                const auto& recipient_name = recipient;

                Light* light = FindLightByName(recipient_name.c_str());
                if (light != 0) {
                    light->m_save_marked = true;
                    if ((!light->visible) == 0) {
                        light->visible = false;
                    } else {
                        light->visible = true;
                    }
                    action_completed = true;
                }
            }
        } else if (action == 0x22) {
            const auto* env = std::get_if<W8EnvironmentTriggerActionData>(&action_payload);
            assert(env);
            SetWorldEnvironmentIntensity(GetWorld(), env->previous_environment);
            action_payload = std::monostate{};
            goto finish_linked_triggers;
        }

        if (!action_completed) {
            goto reactivate_linked_triggers;
        }
    }

finish_linked_triggers:
    if ((flags & W8_TRIGGER_FIRE_LINKED) != 0) {
        for (const auto& recipient : recipients) {
            Trigger* trigger = FindTriggerByName(recipient.c_str());
            if (trigger != 0) {
                trigger->FinishAction();
            }
        }
    }

reactivate_linked_triggers:
    if (was_running && (flags & W8_TRIGGER_ON) != 0 &&
        (flags & W8_TRIGGER_REACTIVATE_LINKED) != 0) {
        for (const auto& recipient : recipients) {
            Trigger* trigger = FindTriggerByName(recipient.c_str());
            if (trigger != 0) {
                trigger->Run(-1);
            }
        }
    }
}

/* The location variable a state-changing action writes; the level must define it. */
static int FindModifiedLocationVar(const char* name)
{
    const int id = GetLocationVarIDByName(name);
    if (id == -1) throw std::out_of_range(fmt::format("unknown location variable {}", name));
    return id;
}

void Trigger::CommitActionResult(bool apply_state_changes)
{

    UpdateActionAnimation();

    if (!recipients.empty() && (flags & W8_TRIGGER_FIRE_LINKED) != 0 &&
        (flags & W8_TRIGGER_LINK_ON_DEACTIVATE) == 0) {
        for (const auto& recipient : recipients) {
            Trigger* trigger = FindTriggerByName(recipient.c_str());
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

        if (state_mod_mode == W8TriggerStateModificationMode::set) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str());
            state_value = 1;
        } else if (state_mod_mode == W8TriggerStateModificationMode::clear) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str());
            state_value = 0;
        } else if (state_mod_mode == W8TriggerStateModificationMode::toggle) {
            state_id = FindModifiedLocationVar(state_to_modify.c_str());
            state_value = CurrentGame().story.location_variables[state_id].value == 0;
            state_id = FindModifiedLocationVar(state_to_modify.c_str());
        } else {
            goto show_action_message;
        }
        CurrentGame().story.location_variables[state_id].value = state_value;
    }

show_action_message:
    if ((flags & W8_TRIGGER_CAN_RUN_LINKED) != 0) {
        const std::string_view level_folder = GetLevelFolderName(GetLoadedLevelID());
        int message_id;

        if (action_state == W8TriggerActionState::initial_action) {
            message_id = m_lData1;
        } else if (action_state == W8TriggerActionState::alternate_action) {
            message_id = m_lData2;
        } else if (action_state == W8TriggerActionState::fallback_action) {
            message_id = m_lData3;
        } else {
            goto action_complete;
        }

        if (message_id != -1) {
            char path[512];
            std::string text;

            snprintf(path, sizeof(path), "Data\\Messages\\%.*s.msg",
                     static_cast<int>(level_folder.size()), level_folder.data());
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
void Trigger::RunDestination(std::string_view destination)
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
    current_location = GetLoadedLevelID();
    location_id = current_location;
    named_entity =
        FindEntityByName(destination, &destination_position, &angle, &destination_direction);
    if (!named_entity) {
        if (destination.size() < 5) {
            return;
        }
        location_id = FindLevelIdByLocationCode(destination.substr(0, 3));
        if (location_id == -1) {
            return;
        }
        entrance = atoi(std::string(destination.substr(3, 2)).c_str());
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

    placeParty(*GetWorld(), destination_position);

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
   The trigger's persistent seed reseeds the game's random stream first. */
void Trigger::GenerateItemGroup()
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    unsigned int table_id;
    unsigned int maximum_items;
    int index;

    if (inline_action_data[0] == '\0' || items_generated != 0) {
        return;
    }

    CurrentGame().rng.seed(item_group_seed);
    table_id = FindItemTableByName(inline_action_data);
    if (table_id == static_cast<unsigned int>(-1)) {
        return;
    }

    maximum_items = RollDice(&Data().item_tables[table_id].item_count_dice);
    GenerateItemsFromTable(items, table_id, maximum_items, position);
    if (world_item_group == 0) {
        world_item_group = SpawnItem(0x23c, &position, 0);
    }
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        world_item_group->contents.push_back(std::move(items[index]));
    }
    gold = RollDice(&Data().item_tables[table_id].gold_dice);
    items_generated = 1;
}

/* The trigger's container world item, materialized on demand: when asked to
   create and none exists yet a bare container item is spawned into the world
   and remembered. */
W8WorldItem* Trigger::GetOrCreateItemGroup(bool create)
{
    if (create && world_item_group == 0) {
        world_item_group = SpawnItem(0x23c, &position, 0);
    }
    return world_item_group.get();
}

/* After a selected-prop Run: while g_trigger_feedback is clear, post either the
   special-item notice (required_item_id != -1) or the nothing-happened notice. */
void Trigger::PrintNothingHappenedOrSpecialItemRequired()
{
    if (g_trigger_feedback) {
        return;
    }
    if (required_item_id != -1) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x96b]);
        return;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x964]);
}

/* Execute the selected Trigger action. The original keeps the three trigger
   kinds in one dispatcher: kind two handles invisible/timed actions first,
   kind one handles Prop-backed actions, and everything else falls through to
   the common action table. */
/* Wake the first placed group of `monster_id` and restart its first member's animation
   and pathing. The member is looked up when the trigger fires: it may have left the
   group, and an unloaded one has no representation to restart until it loads. */
void WakeMonsterGroup(int monster_id)
{
    const auto& groups = CurrentLevelPopulation().monster_groups;
    const auto found = std::ranges::find_if(
        groups, [monster_id](const auto& group) { return group->monster_id == monster_id; });
    if (found == groups.end()) return;
    W8MonsterGroup& group = **found;
    W8MonsterInfo* monster_info = FindMonster(group.MemberLocationId(0), false);
    if (monster_info == nullptr) return;

    group.members_active = true;
    Monster* monster = monster_info->entity.find();
    if (monster == nullptr) return;
    monster->visual.rep_active = true;
    monster->visual.animation_playing = 1;
    monster->visual.timer = GameClockMilliseconds();
    monster->visual.ResetRepresentation();
    monster->nav.ResetPathAI();
    monster->nav.reactivated = true;
}

void Trigger::Run(int source)
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    ActivationCallback callback = activation_callback;
    bool apply_state_changes = true;
    bool action_succeeded = false;

    if (!SelectAction()) {
        return;
    }
    if (callback && !callback(this)) {
        return;
    }

    if (trigger_kind == W8TriggerKind::prop) {
        switch (action) {
        case 0:
            goto commit_action;

        case 9:
            if (recipients.empty()) {
                break;
            }
            UpdateCameraPathStateByName(m_pWorld, recipient_name.c_str(), 1);
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
            if (recipients.empty() || SDL_strcasecmp(recipient_name.c_str(), "party") != 0) {
                break;
            }
            ApplyItemEffectToRandomCharacter(
                CurrentGame().rng.below(2) != 0 ? kConditionReaction : kConditionReactionAlt, -1,
                0);
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x11:
            if ((flags & W8_TRIGGER_RUNNING) != 0) {
                break;
            }
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x22: {
            float previous_value = GetWorld()->environment.intensity;
            action_payload = W8EnvironmentTriggerActionData{previous_value};
            SetWorldEnvironmentIntensity(GetWorld(), 0.0f);
            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;
        }

        case 0x23:
            if (timed_action == 0) {
                timed_action = &m_pWorld->addTimedAction(this);
                timed_action->action = static_cast<short>(action);
                timed_action->timer.SetDuration(0.5f);
            }
            timed_action->timer.Restart();
            if (timed_action->m_pCountdown.has_value()) {
                timed_action->m_pCountdown->Restart();
            }

            flags |= W8_TRIGGER_RUNNING;
            goto commit_action;

        case 0x34:
            if (recipients.empty()) {
                break;
            }
            RunDestination(recipient_name.c_str());
            goto commit_action;

        default:
            break;
        }
    }

    if (trigger_kind == W8TriggerKind::item) {
        switch (action) {
        case 1: {
            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0 || state_index != 0 ||
                m_pProp->animation_playing != 0) {
                break;
            }
            W8DoorTriggerActionData* action_data = doorData();
            if (action_data != 0 && action_data->locked && action_data->item != -1) {
                if (!PartyHasItem(CurrentGame().party, action_data->item)) {
                    ShowNotice(W8_FONT_PALETTE_BLUE, "Your party doesn't have required key.");
                    break;
                }
                action_data->locked = false;
            }

            m_pProp->SetRepresentationActive(1, true);
            state_index = 1;
            if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
                m_pWorld->collisionIndex()->SetInterfaceState(surface_id, 1);
            }
            flags |= W8_TRIGGER_RUNNING;
            if (action_data != 0) {
                action_data->open = true;
            }

            if (m_lData1 != 0) {
                if (timed_action != nullptr) {
                    timed_action->timer.Restart();
                    if (timed_action->m_pCountdown.has_value()) {
                        timed_action->m_pCountdown->Restart();
                    }
                    goto commit_action;
                }

                timed_action = &m_pWorld->addTimedAction(this);
                timed_action->action = 2;
                timed_action->timer.SetDuration(m_lData1 < 0 ? 10.0f
                                                             : static_cast<float>(m_lData1));
                timed_action->timer.Restart();
                timed_action->repeat = true;
            }
            goto commit_action;
        }

        case 2: {
            bool active;

            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
                break;
            }
            active = m_pProp->animation_playing;
            if (active) {
                break;
            }
            state_index = state_index == 1 ? 0 : 1;
            m_pProp->SetRepresentationActive(1, true);
            if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
                m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
            }
            flags |= W8_TRIGGER_RUNNING;
            if (auto* door = doorData()) {
                door->open = (state_index != 0);
            }
            goto commit_action;
        }

        case 0x2c: {
            W8DoorTriggerActionData* action_data = doorData();
            bool was_active;

            if (action_data != 0 && action_data->locked && action_data->item != -1) {
                if (!PartyHasItem(CurrentGame().party, action_data->item)) {
                    ShowNotice(W8_FONT_PALETTE_BLUE, "Your party doesn't have required key.");
                    break;
                }
                action_data->locked = false;
            }
            if (m_bRepType != W8_TRIGGER_REP_PROP || m_pProp == 0) {
                break;
            }
            was_active = m_pProp->animation_playing;
            m_pProp->SetRepresentationActive(!was_active, true);
            state_index = state_index == 0;
            if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
                m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
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
                        state_direction = W8TriggerStateDirection::backward;
                    }
                } else if (next < 0) {
                    state_index = 1;
                    state_direction = W8TriggerStateDirection::forward;
                }
            }

            if ((flags & W8_TRIGGER_ANIMATE_STATES) != 0) {
                Animation* animation;
                /* 0x0043E4C3 tests the AnimObjListCount result signed. */
                int count;
                int index;

                assert(m_pProp != 0 && m_bRepType == W8_TRIGGER_REP_PROP);
                animation = m_pProp->currentAnimation();
                if (animation->path_lists == 1) {
                    count = AnimObjListCount(animation, 2);
                    for (index = 0; index < count; ++index) {
                        KeyframeTrack* path =
                            AnimObjListEntry(animation, 2, static_cast<signed char>(index));
                        PathAIUpdate(path,
                                     previous <= static_cast<signed char>(state_index) ? 1 : -1);
                    }
                } else {
                    m_pProp->SetPendingAnimationSubcycle(static_cast<signed char>(state_index));
                }
            }
            goto commit_action;
        }

        case 0x37: {
            int tag = source == -1 ? m_lData1 : source;

            if (m_bRepType == W8_TRIGGER_REP_PROP && m_pProp != 0 && tag != -1) {
                /* Retail looks the slot up by the raw source argument, not by
                   the resolved tag, so a Run(-1) asks for tag 0xFF. */
                m_pProp->SelectAnimationSlot(static_cast<unsigned char>(source));
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

        for (const auto& recipient : recipients) {
            Light* light = FindLightByName(recipient.c_str());
            if (light != 0) {
                light->m_save_marked = true;
                if (action == 4) {
                    if ((!light->visible) == 0) {
                        light->visible = false;
                    } else {
                        light->visible = true;
                    }
                } else if (action == 0x30) {
                    if ((!light->visible) != 0) {
                        light->visible = true;
                    }
                } else if ((!light->visible) == 0) {
                    light->visible = false;
                }
                action_succeeded = true;
            }
        }
        if (trigger_kind == W8TriggerKind::prop) {
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

        if (recipients.empty()) {
            return;
        }
        for (const auto& recipient : recipients) {
            Trigger* target = FindTriggerByName(recipient.c_str());
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
        was_active = m_pProp->animation_playing;

        if (inline_action_data[0] != '\0') {
            if ((CurrentGame().party.held.item.iItemNo != -1)) {
                glm::vec3 item_position;
                auto item = CreateWorldItem(&CurrentGame().party.held.item, &item_position,
                                            W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE);

                if (item != 0) {
                    if (world_item_group == 0) {
                        glm::vec3 group_position;
                        world_item_group = SpawnItem(0x23c, &group_position, 0);
                    }
                    world_item_group->contents.push_back(std::move(item));
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
                int item_count = static_cast<int>(world_item_group->contents.size());
                W8WorldItem* item;

                if (item_count != 0 && m_pProp->subcycle != 0) {
                    action_succeeded = false;
                }

                for (const auto& contained : world_item_group->contents) {
                    if (!contained->item.identified) {
                        PartyAttemptsToIdentifyItem(&contained->item, 0);
                    }
                }
                if (item_count > 1) {
                    CurrentGame().item_pick_pending = true;
                }

                if (gold != 0) {
                    AddPartyGold(CurrentGame(), gold, true);
                    gold = 0;
                }

                if (item_count == 0) {
                    if (m_pProp->subcycle == 0) {
                        ApplyItemEffectToRandomCharacter(CurrentGame().rng.below(2) != 0
                                                             ? g_container_event
                                                             : g_container_event_alt,
                                                         -1, 0);
                    }
                } else if (item_count == 1 && !(CurrentGame().party.held.item.iItemNo != -1)) {
                    item = world_item_group->contents.front().get();
                    ReceiveItemInHand(CurrentGame(), item->item);
                    world_item_group->contents.clear();
                    if (m_pProp->subcycle != 0) {
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
        if (m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
            m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
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
        if ((action == 0x32 && m_pProp->animation_playing != 0) ||
            (action == 0x33 && m_pProp->animation_playing == 0)) {
            return;
        }
        m_pProp->SetRepresentationActive(action == 0x32, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
            m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
        }
        if (action == 0x32) {
            flags |= W8_TRIGGER_RUNNING;
        } else {
            flags &= ~W8_TRIGGER_RUNNING;
        }
        break;

    case 0x0f: {

        if (recipients.empty()) {
            return;
        }
        for (const auto& recipient : recipients) {
            Trigger* target = FindTriggerByName(recipient.c_str());
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
        m_pWorld->party_camera.applySway(*m_pWorld->camera,
                                         source > 0 ? CameraSwayMode::Start : CameraSwayMode::Stop);
        return;

    case 0x36:
        if (action_state == W8TriggerActionState::fallback_action &&
            action_data_mode == W8TriggerActionDataMode::fallback_action) {
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
        if (trigger_kind == W8TriggerKind::prop) {
            flags |= W8_TRIGGER_RUNNING;
        }
        break;
    }

    case 0x0b:
        if (!recipients.empty()) WakeMonsterGroup(recipient_group);
        return;

    case 0x39: {
        glm::vec3 party_position;
        TriggerEvent* event;

        party_position = cameraLocation();
        if (timed_action == 0) {
            timed_action = &m_pWorld->addTimedAction(this);
            event = timed_action;
            if (!event->shake) event->shake.emplace();
            event->trigger = this;
            event->action = static_cast<short>(action);
            event->timer.SetDuration(m_lData2 == -1 ? 0.07f : m_lData2 * 0.001f);
            event->timer.Restart();
            event->shake->intensity = m_lData1 == -1 ? 800 : m_lData1;

            if (m_lData3 != -1) {
                event->m_pCountdown.emplace();
                event->m_pCountdown->SetDuration(abs(m_lData3) * 0.001f);
                event->m_pCountdown->Restart();
                if (m_lData3 < 0) {
                    event->shake->reverse = true;
                }
            }
        } else {
            event = timed_action;
            if (!event->shake) event->shake.emplace();
            event->timer.Restart();
            if (event->m_pCountdown.has_value()) {
                event->m_pCountdown->Restart();
            }
        }

        if ((flags & W8_TRIGGER_RUNNING) == 0) {

            if (trigger_kind == W8TriggerKind::prop) {
                flags |= W8_TRIGGER_RUNNING;
            } else if (m_lData3 == -1) {
                throw std::runtime_error(
                    fmt::format("trigger {}: a visible camera shake needs a duration", name));
            }
        }
        break;
    }

    case 0x3a:
        if (m_pProp == 0 || source != m_lData1) {
            return;
        }
        m_pProp->SetActivationState(false);
        break;

    case 0x3b:
        if (m_pProp == 0 || source != m_lData1 || m_pProp->animation_playing == 0) {
            return;
        }
        m_pProp->SetRepresentationActive(m_pProp->animation_playing == 0, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
            m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
        }
        break;

    case 0x3c:
        if (m_pProp == 0 || source != m_lData1) {
            return;
        }
        m_pProp->SetRepresentationActive(m_pProp->animation_playing == 0, true);
        state_index = state_index == 0;
        if (m_pWorld != 0 && m_pWorld->collisionIndex() != 0 && surface_id >= 0) {
            m_pWorld->collisionIndex()->SetInterfaceState(surface_id, state_index);
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
                ShowString(Data().strings[0x722]);
                PlayActionSound("Data\\Sound\\misc\\fountain_magic.wav", 0);
            } else {
                W8SpellId spell_id;
                const glm::vec3 position = cameraLocation();

                if (action == 0x28) {
                    spell_id = W8_SPELL_HEX;
                } else if (action == 0x29) {
                    spell_id = W8_SPELL_SLEEP;
                } else if (action == 0x2a) {
                    spell_id = W8_SPELL_NOXIOUS_FUMES;
                } else {
                    spell_id = W8_SPELL_HEAL_ALL;
                }
                CurrentGame().castTriggerSpell(position, spell_id,
                                               static_cast<unsigned int>(m_lData3));
            }
        }

        if (uses_remaining == 0) {
            if (m_lData1 != -1) {
                return;
            }
        } else {
            --uses_remaining;
            if (m_lData2 > 0 && timed_action == 0) {
                timed_action = &m_pWorld->addTimedAction(this);
                timed_action->action = static_cast<short>(action);
                timed_action->timer.SetDuration(m_lData2 * 720.0f);
                timed_action->timer.Restart();

                timed_action->timer.SetMode(W8_TIMER_CLOCK_GAME);
            }
        }
        if (trigger_kind == W8TriggerKind::prop) {
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
        } else if (timed_action == 0) {
            float duration = abs(m_lData2) * 0.001f;

            if (m_lData2 > 0 && trigger_kind != W8TriggerKind::prop)
                throw std::runtime_error(
                    fmt::format("trigger {}: continuous missiles need an invisible trigger", name));
            timed_action = &m_pWorld->addTimedAction(this);
            timed_action->action = static_cast<short>(action);
            timed_action->timer.SetDuration(duration);
            timed_action->timer.Restart();

            flags |= W8_TRIGGER_RUNNING;
        }
        break;

    case 0x3d:
        if (m_pProp == 0) {
            return;
        }
        m_pProp->SetActivationState(true);
        break;

    case 0x3e:
        if (m_pProp == 0) {
            return;
        }
        m_pProp->SetActivationState(false);
        break;

    case 0x3f:
        if (timed_action == 0) {
            timed_action = &m_pWorld->addTimedAction(this);
            timed_action->action = static_cast<short>(action);
            timed_action->timer.SetDuration(m_lData1 * 0.001f);
            timed_action->timer.Restart();

            timed_action->repeat = true;
        } else {
            timed_action->timer.Restart();
            if (timed_action->m_pCountdown.has_value()) {
                timed_action->m_pCountdown->Restart();
            }
        }

        if (trigger_kind == W8TriggerKind::prop) {
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
        state_index = m_pProp->AdvanceAnimationSegment();
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

        if (recipients.empty()) {
            return;
        }
        for (const auto& recipient : recipients) {
            const auto& name = recipient;
            if (action == 0x41) {
                PositionAmbientSoundByName(*m_pWorld, name.c_str());
            } else if (action == 0x42) {
                StopAmbientSoundByName(*m_pWorld, name.c_str());
            } else {
                ToggleAmbientSoundByName(*m_pWorld, name.c_str());
            }
            action_succeeded = true;
        }
        break;
    }

    case 0x44:
    case 0x45:
    case 0x46: {

        if (recipients.empty()) {
            return;
        }
        for (const auto& recipient : recipients) {
            ParticleSystem* particle = FindParticleByName(recipient.c_str());
            if (particle != 0) {
                particle->persisted = true;
                if (action == 0x44) {
                    particle->SetActive(true);
                } else if (action == 0x45) {
                    particle->SetActive(false);
                } else {
                    particle->SetActive(!particle->emitting);
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

        if (recipients.empty() || timed_action != 0) {
            return;
        }
        action_succeeded = false;
        for (const auto& recipient : recipients) {
            ParticleSystem* particle = FindParticleByName(recipient.c_str());
            if (particle != 0) {
                particle->persisted = true;
                particle->SetActive(true);
                action_succeeded = true;
            }
        }
        if (!action_succeeded) {
            return;
        }

        timed_action = &m_pWorld->addTimedAction(this);
        timed_action->action = static_cast<short>(action);
        timed_action->m_pCountdown.emplace();
        timed_action->m_pCountdown->SetDuration(m_lData1 == -1 ? 10.0f : m_lData1 * 0.001f);
        timed_action->m_pCountdown->Restart();
        timed_action->repeat = true;
        break;
    }

    case 0x48: {
        unsigned int count;

        if (m_pProp == 0 || m_lData1 < 0) {
            return;
        }
        count = AnimObjValue(m_pProp->currentAnimation(), 2);
        if (static_cast<int>(count) <= m_lData1) {
            return;
        }
        m_pProp->SetPendingAnimationSubcycle(static_cast<signed char>(m_lData1));
        break;
    }

    case 0x49:
        if (m_pProp == 0 || m_lData1 < 0) {
            return;
        }
        m_pProp->SetAnimationSpeed(static_cast<float>(m_lData1));
        break;

    case 0x4a: {

        if (recipients.empty() || m_lData1 < 0) {
            return;
        }
        for (const auto& recipient : recipients) {
            Prop* prop = FindPropByName(GetWorld(), recipient.c_str());
            if (prop != 0) {
                prop->SetAnimationSpeed(static_cast<float>(m_lData1));
            }
        }
        break;
    }

    case 0x4b: {
        if (recipients.empty()) return;
        const auto selected = MonotonicMilliseconds() % recipients.size();
        RunDestination(recipients[selected].c_str());
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

bool Trigger::CanRunLinkedTriggers()
{

    if (m_pProp != 0 && m_pProp->animation_playing != 0) {
        return false;
    }
    for (const auto& recipient : recipients) {
        Trigger* trigger;

        const auto& recipient_name = recipient;
        trigger = FindTriggerByName(recipient_name.c_str());
        if (trigger != 0 && !trigger->CanRunLinkedTriggers()) {
            return false;
        }
    }
    return true;
}

bool Trigger::SelectAction()
{
    auto* timed_action = m_pWorld ? m_pWorld->findTimedAction(this) : nullptr;
    bool fallback_selected = false;
    bool result = true;

    if (CurrentGame().combat.has_value() && doorData() != nullptr && doorData()->open) {
        return false;
    }

    if (((flags & W8_TRIGGER_RUNNING) != 0 && action != 0x39) || (flags & W8_TRIGGER_ON) == 0 ||
        ((flags & W8_TRIGGER_ONCE) != 0 && (flags & W8_TRIGGER_FIRED) != 0)) {
        action_state = W8TriggerActionState::inactive;
        return false;
    }

    if (!required_states.empty()) {
        for (const auto& state_name : required_states) {
            const int state_id = FindModifiedLocationVar(state_name.c_str());
            if (CurrentGame().story.location_variables[state_id].value == 0) {
                action = fallback_action;
                action_state = W8TriggerActionState::fallback_action;
                fallback_selected = true;
                break;
            }
        }
    }

    if (auto* action_data = doorData(); action_data == nullptr) {
        if (required_item_id >= 0) {
            if (GetItemInHand(CurrentGame().party) == required_item_id) {
                if ((flags & W8_TRIGGER_CONSUME_ITEM) != 0) {
                    RemovePartyItemByID(CurrentGame(), required_item_id, false);
                    required_item_id = -1;
                }
            } else {
                if (m_lData2 == 1 && activation_callback) {
                    activation_callback(this);
                }
                action = fallback_action;
                action_state = W8TriggerActionState::fallback_action;
                fallback_selected = true;
            }
        }
    } else {
        bool linked_trigger_blocked = false;

        if (m_pProp != 0 && m_pProp->animation_playing != 0) {
            linked_trigger_blocked = true;
        } else {
            for (const auto& name : recipients) {
                Trigger* trigger = FindTriggerByName(name.c_str());
                if (trigger != 0 && !trigger->CanRunLinkedTriggers()) {
                    linked_trigger_blocked = true;
                    break;
                }
            }
        }

        if (linked_trigger_blocked) {
            if (timed_action == nullptr) {
                return false;
            }
            timed_action->timer.Restart();
            if (timed_action->m_pCountdown.has_value()) {
                timed_action->m_pCountdown->Restart();
            }
            if (!running) {
                g_trigger_feedback = true;
            }
            return false;
        }

        if (action_data->locked && action_data->item != -1) {
            if (GetItemInHand(CurrentGame().party) != action_data->item) {
                if (m_lData2 == 1 && activation_callback) {
                    activation_callback(this);
                }
                action = fallback_action;
                action_state = W8TriggerActionState::fallback_action;
                fallback_selected = true;
            } else {
                lock_state.device_state.completed = 1;
                action_data->locked = false;
                if (action_data->linked_trigger[0] != '\0') {
                    Trigger* linked_trigger;
                    action_state = W8TriggerActionState::inactive;
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

    if (lock_state.lock_type != LockType::none && lock_state.device_state.completed == 0 &&
        !running) {
        if (lock_state.lock_type == LockType::pickable_lock) {
            g_trigger_feedback = true;
            OpenLockInteraction(this);
            return false;
        }
        if (lock_state.lock_type == LockType::trap) {
            g_trigger_feedback = true;
            OpenTrapInteraction(this);
            return false;
        }
    }

    if (!fallback_selected) {
        if ((flags & W8_TRIGGER_USE_ALTERNATE) == 0) {
            action = initial_action;
            action_state = W8TriggerActionState::initial_action;
            if ((flags & W8_TRIGGER_HAS_ALTERNATE) != 0) {
                flags |= W8_TRIGGER_USE_ALTERNATE;
            }
        } else {
            action = alternate_action;
            action_state = W8TriggerActionState::alternate_action;
            if ((flags & W8_TRIGGER_ALTERNATE_TOGGLES) != 0) {
                flags &= ~W8_TRIGGER_USE_ALTERNATE;
            }
        }
    } else if (action == 0) {
        action_state = W8TriggerActionState::inactive;
        return false;
    }
    return result;
}

Light* FindLightByName(std::string_view name)
{
    return FindWorldLight(name);
}

int ResetNextTriggerId(void)
{
    CurrentGame().status.next_trigger_id = 1;
    return 1;
}

int GetLocationVarIDByName(std::string_view name)
{
    const auto& variables = CurrentGame().story.location_variables;
    const auto found = std::ranges::find_if(variables, [name](const auto& variable) {
        return variable.level == GetLoadedLevelID() &&
               CompareTextIgnoreAsciiCase(variable.name, name) == 0;
    });
    return found == variables.end() ? -1 : static_cast<int>(found - variables.begin());
}

void CreateLocationVar(std::string_view name, int value)
{
    if (name.empty()) throw std::invalid_argument("empty location variable name");
    if (GetLocationVarIDByName(name) >= 0) return;
    auto& variables = CurrentGame().story.location_variables;
    if (variables.size() >= std::numeric_limits<std::int32_t>::max())
        throw std::length_error("Too many location variables");
    variables.push_back({std::string(name), value, GetLoadedLevelID()});
}

int GetLocationVarValueByName(std::string_view name)
{
    const int index = GetLocationVarIDByName(name);
    if (index < 0) throw std::out_of_range(fmt::format("unknown location variable {}", name));
    return CurrentGame().story.location_variables[index].value;
}

/* Index of the prop whose trigger last tested in view; -1 until a prop
   matches. */

/* Report whether any world prop's trigger representation is within its
   activation range and projects onto the screen. The remembered index is
   checked first so consecutive frames start at the prop that matched. */
bool AnyPropTriggerInView()
{
    glm::vec3 position;
    std::size_t prop_count;
    int index;

    assert(GetWorld() != nullptr);
    position = cameraLocation();
    prop_count = GetWorld()->plsProps.size();
    if (0 <= GetWorld()->last_visible_prop_index &&
        GetWorld()->last_visible_prop_index < static_cast<int>(prop_count)) {
        Prop* prop = GetWorldProp(GetWorld(), GetWorld()->last_visible_prop_index);

        if (prop->IsTriggerInView(&position)) {
            return true;
        }
    }
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        Prop* prop = GetWorldProp(GetWorld(), index);

        if (prop->IsTriggerInView(&position)) {
            GetWorld()->last_visible_prop_index = index;
            return true;
        }
    }
    return false;
}

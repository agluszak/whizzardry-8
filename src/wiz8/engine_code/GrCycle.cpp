#include "wiz8/engine_code/GrCycle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/geometric.hpp>

#include "Navigator.h"
#include "application.h"
#include "asset_paths.h"
#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/world.h"
#include "particle_modes.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SpellVisual.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/filesystem.h"
#include "wiz8/ground_shadow.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/unicode.h"

constexpr float kLodLevelTwoExitThreshold = 0.699999988079071f;
constexpr float kLodLevelZeroExitThreshold = 0.30000001192092896f;

float g_lod_range_default0 = 3500.0f;
float g_lod_range_default1 = 8500.0f;

#define BEHAVIOUR_FIRST W8_ANIMATION_PLAY_ONCE
#define BEHAVIOUR_LAST W8_ANIMATION_NEVER_STOP

static bool g_camera_shake_enabled = true;
ShakeDefinition::ShakeDefinition(float duration, bool preset, float intensity, float distance_cap,
                                 const glm::vec3* position)
    : duration(duration), intensity(intensity), distance_cap(distance_cap), limit_distance(preset),
      quadratic_falloff(preset), fade_out(preset)
{
    if (position) {
        this->position = *position;
    }
}

ShakeSystem& CameraShakes()
{
    static ShakeSystem system;
    return system;
}

ShakeSystem::Instance::Instance(const ShakeDefinition& definition, const ShakeDefinition* key)
    : definition(definition), timer(definition.duration, false), key(key)
{
    timer.Restart();
}

void ShakeSystem::Start(const ShakeDefinition& definition, const ShakeDefinition* key)
{
    auto instance = std::make_unique<Instance>(definition, key);
    if (key) {
        for (auto& current : active) {
            if (current->key == key) {
                current.swap(instance);
                return;
            }
        }
    }
    active.push_back(std::move(instance));
}

void ShakeSystem::Stop(const ShakeDefinition& key)
{
    std::erase_if(active, [&key](const auto& instance) { return instance->key == &key; });
}

bool ShakeSystem::IsActive(const ShakeDefinition& key) const
{
    return std::ranges::any_of(active,
                               [&key](const auto& instance) { return instance->key == &key; });
}

bool ShakeSystem::Instance::Evaluate(const glm::vec3& camera, float& amount)
{
    if (definition.duration <= 0.0f) {
        return false;
    }
    const float progress = timer.GetProgress();
    if (progress >= 1.0f) {
        return false;
    }
    amount = 1.0f;
    if (definition.limit_distance) {
        const auto delta = definition.position - camera;
        const float distance = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (definition.distance_cap < distance) {
            amount = 0.0f;
        } else if (definition.quadratic_falloff) {
            const float weight = distance / definition.distance_cap - 1.0f;
            amount = weight * weight;
        }
    }
    if (definition.fade_out) {
        amount *= 1.0f - progress;
    } else if (definition.fade_in) {
        amount *= progress;
    }
    return true;
}

float ShakeSystem::Update(const glm::vec3& camera)
{
    float intensity = 0.0f;
    std::vector<std::function<void()>> callbacks;
    callbacks.reserve(active.size());
    std::erase_if(active, [&](const auto& instance) {
        float amount;
        if (instance->Evaluate(camera, amount)) {
            intensity += amount;
            return false;
        }
        if (instance->definition.completion_callback) {
            callbacks.push_back(std::move(instance->definition.completion_callback));
        }
        return true;
    });
    // Callbacks may start/stop shakes or destroy the definitions that keyed
    // them. Retire all completed instances before invoking any callback.
    for (const auto& callback : callbacks) {
        callback();
    }
    return intensity;
}

static void TriggerShakeEvents(std::deque<ShakeDefinition>& events, int cycle, unsigned int frame,
                               int subcycle, const glm::vec3& position)
{
    for (auto& event : events) {
        if (event.cycle == cycle && event.frame == static_cast<int>(frame) &&
            event.subcycle == subcycle) {
            auto definition = event;
            definition.position = position;
            CameraShakes().Start(definition, &event);
        }
    }
}

void UpdateShakeEffects()
{
    if (!g_camera_shake_enabled) {
        return;
    }
    const float intensity = CameraShakes().Update(cameraLocation());
    if (intensity > 0.0f) {
        const float amplitude = std::min(intensity, 1.0f) * 2.5e+02f;
        const int span = static_cast<int>(amplitude) << 1;
        g_trigger_action_scene_offset.x = Random(span) - amplitude;
        g_trigger_action_scene_offset.y = Random(span) - amplitude;
        g_trigger_action_scene_offset.z = Random(span) - amplitude;
        g_trigger_action_active = true;
    } else {
        g_trigger_action_active = false;
    }
}

/* Build the two paths used while reading a .mon resource, verify its one-byte
   version, and hand the open file plus its resource context to the typed cycle
   reader. */
bool LoadGrCycle(std::string_view mon_name, W8GrCycle* cycle, int cycle_index, int value,
                 std::string_view directory, unsigned char object_type,
                 std::string_view bitmap_directory)
try {
    W8ReadLevelInfo info;
    unsigned char version;
    bool success;

    if (mon_name.empty() || cycle == 0) {
        Fail("pacName && ppCycle");
    }

    const auto mon_path = fmt::format("{}/{}.mon", directory, mon_name);
    const auto bitmap_path = bitmap_directory.empty() ? fmt::format("{}/Bitmaps", directory)
                                                      : std::string(bitmap_directory);

    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path(mon_path), wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle == 0) {
        ShutdownWithErrorBox(fmt::format("Couldn't open {}", mon_path));
    }

    info.world = cycle->owner_world;
    info.hFile = handle.get();
    info.bitmap_folder = wiz8::asset_path(bitmap_path);
    success = true;

    if ((handle->read(&version, 1).bytes == static_cast<std::size_t>(1)) == 0 || version != 1 ||
        ReadGrCycleData(&info, cycle, cycle_index, value, object_type) == 0) {
        success = false;
    }

    if (handle) handle->close();

    handle.reset();
    if (!success) {
        Fail("fSuccess",
             fmt::format("GrCycle::Read: ERROR - Read {} in {} failed", mon_path, bitmap_path));
    }
    return success;
} catch (const std::exception&) {
    return false;
}

/* Read the shared path and particle prefix, then dispatch the object-specific
   representation payload.  A particle attachment keeps the source node plus
   its local transform; the live cycle owns the attachment record and vector,
   while the particle node remains scene-owned. */
unsigned char ReadGrCycleData(W8ReadLevelInfo* info, W8GrCycle* cycle, int cycle_index, int value,
                              unsigned char object_type)
try {
    unsigned char has_path;
    unsigned char has_particles;
    unsigned char success = 0;

    if (info == 0) {
        Fail("pInfo");
    }

    info->hFile->read(&has_path, 1);
    if (has_path != 0) {
        cycle->m_pAI = LoadPathAI(*info->hFile);
    }

    info->hFile->read(&has_particles, 1);
    if (has_particles != 0) {
        std::vector<std::unique_ptr<stParticle>> particles;

        success = ReadWorldParticles(info, info->world ? info->world->dynamic_scene : nullptr,
                                     &particles);
        if (!particles.empty()) {
            int index;

            for (index = 0; index < static_cast<int>(particles.size()); ++index) {
                // The attachment takes ownership below.
                stParticle* particle = particles[index].get();
                auto* event = &cycle->particles.emplace_back();
                glm::dvec3 location;

                event->cycle = cycle_index;
                event->subcycle = -1;
                event->m_pstParticles = std::move(particles[index]);
                location = particle->getLocation();
                event->position = glm::vec3(location);
                event->rotation = particle->getRotation<glm::mat3>();
            }
        }
    }

    switch (object_type) {
    case 0:
        success = static_cast<W8Monster*>(cycle)->ReadCycleData(info, cycle_index, value);
        break;
    case 1:
        success = static_cast<W8Missile*>(cycle)->ReadCycleData(info, cycle_index, value);
        break;
    case 2:
        success = static_cast<W8SpellVisual*>(cycle)->ReadCycleData(info, cycle_index, value);
        break;
    }
    return success;
} catch (const std::exception&) {
    return false;
}

W8GrCycle::W8GrCycle(W8World* owner) : W8Navigator(owner)
{
    timer = GameClockMilliseconds();
    lod_near = g_lod_range_default0;
    lod_far = g_lod_range_default1;
    current_model_instance = 0;
    m_plsLights = 0;
    last_subcycle = 0;
    wrapped = false;
    enabled = true;
    mirror_x = false;
    aim_set = false;
    frame_fraction = 0;
    opacity = 1.0f;
}

// Copy runtime state, particles and the shadow; representations own borrowed lights.
W8GrCycle::W8GrCycle(const W8GrCycle& other, W8World* owner)
    : W8Navigator(other, owner), location(other.location), local_location(other.local_location),
      parent_location(other.parent_location), rotation(other.rotation),
      highlight_colour(other.highlight_colour), instance_scale(1.0f), apply_instance_scale(false),
      subcycle(other.subcycle), pending_subcycle(other.pending_subcycle),
      timer(GameClockMilliseconds()), rep_active(other.rep_active),
      animation_playing(other.animation_playing), frame_direction(other.frame_direction),
      frame_method(other.frame_method), animation_behaviour(other.animation_behaviour),
      pending_behaviour(other.pending_behaviour), first_frame(other.first_frame),
      last_frame(other.last_frame), m_bLOD(other.m_bLOD), lod_near(other.lod_near),
      lod_far(other.lod_far), current_cycle(0), current_subcycle(0), forced_subcycle(-1),
      pending_cycle(-1), animation_radius(other.animation_radius)
{
    sound_events.reserve(other.sound_events.size());
    for (const auto& event : other.sound_events) {
        sound_events.push_back(std::make_unique<W8SoundEvent>(event->kind, event->cycle,
                                                              event->frame, event->subcycle,
                                                              event->wave_name.c_str(), false));
    }
    m_pAI = other.m_pAI ? other.m_pAI->Clone() : nullptr;
    current_model_instance = 0;
    m_plsLights = 0;
    last_subcycle = other.last_subcycle;
    wrapped = false;
    enabled = true;
    mirror_x = other.mirror_x;
    aim_set = false;
    opacity = other.opacity;
    frame_fraction = 0;
    shake_events = other.shake_events;
    particles.reserve(other.particles.size());
    for (const auto& source : other.particles) {
        auto& copy = particles.emplace_back();
        copy.cycle = source.cycle;
        copy.subcycle = source.subcycle;
        if (source.m_pstParticles)
            copy.m_pstParticles = std::make_unique<stParticle>(*source.m_pstParticles);
        copy.position = source.position;
        copy.rotation = source.rotation;
    }
    if (other.m_ground_shadow)
        m_ground_shadow = std::make_unique<stGroundShadow>(*other.m_ground_shadow);
}

void W8GrCycle::AddSoundEvent(std::unique_ptr<W8SoundEvent> event)
{
    if (event) sound_events.push_back(std::move(event));
}

void W8GrCycle::SetLocation(const glm::vec3* new_location)
{
    local_location = *new_location;
    location = parent_location + local_location;
}

glm::vec3 W8GrCycle::GetLocation() const
{
    return location;
}

glm::vec3 W8GrCycle::GetLocalLocation() const
{
    return local_location;
}

void W8GrCycle::SetRotation(const glm::mat3* new_rotation)
{
    rotation = *new_rotation;
}

glm::mat3 W8GrCycle::GetRotation() const
{
    return rotation;
}

void W8GrCycle::SetFrameMethod(W8AnimationFrameMethod method)
{
    if (method < 1 || method > 4) {
        Fail("bFrameMethod >= DIR_FIRST && bFrameMethod <= DIR_LAST");
    }
    frame_method = method;
}

W8GrCycle::~W8GrCycle()
{
    UnregisterGrCycle(this);
    for (const auto& event : shake_events) {
        CameraShakes().Stop(event);
    }
}

void W8GrCycle::RetireParticles(W8World& world)
{
    for (auto& event : particles) {
        auto owner = std::move(event.m_pstParticles);
        if (owner) owner->CancelAnimationCallback();
        if (owner && owner->active_particle_count != 0) {
            owner->SetActive(true);
            owner->emission_limit = 1;
            RetireParticle(world, std::move(owner));
        }
    }
}

void W8GrCycle::SetCyclePosition(glm::vec3* position)
{
    SetLocation(position);
    SetPositionInternal(position);
}

void W8GrCycle::TickAnimation(float scale)
{
    ApplyPendingCycle();
    if (animation_playing != 0) {
        signed char subcycle_count = GetNumSubCycles();

        if (subcycle_count != 0) {
            unsigned int now = GameClockMilliseconds();
            unsigned int elapsed = now - timer;
            float rate;
            float progress;
            int frames;

            if (elapsed > 1000) {
                elapsed = 1000;
                timer = now - 1000;
            }

            rate = GetCurrentAnimationScale() * scale;
            progress = elapsed * rate * 0.001f;
            /* A non-finite or negative rate would convert to a frame count the
               countdown loop below never reaches zero from. */
            if (!(progress >= 0.0f && progress < 1.0e6f)) {
                progress = 0.0f;
            }
            frames = static_cast<int>(progress);
            frame_fraction = progress - frames;

            if (frames != 0) {
                glm::vec3 position;

                timer += static_cast<int>(frames * 1e+03f / rate);
                wrapped = false;
                position = GetPosition();
                do {
                    --frames;
                    AdvanceAnimationFrame(subcycle_count, 0);
                    if (ApplyPendingCycle() != 0) {
                        frames = 0;
                    }
                    UpdateSoundEvents(sound_events, position,
                                      W8_SOUND_EVENT_FRAME | W8_SOUND_EVENT_FOOTSTEP, current_cycle,
                                      subcycle, current_subcycle);
                    if (!shake_events.empty()) {
                        TriggerShakeEvents(shake_events, current_cycle, subcycle, current_subcycle,
                                           position);
                    }
                } while (frames != 0);
            }
        }

        if (m_plsLights != 0 && !m_plsLights->empty()) {
            UpdateLights();
        }
        last_subcycle = subcycle;
    }
}

unsigned char W8GrCycle::ApplyPendingCycle()
{
    if (pending_cycle != -1 && CanEnterCycle(pending_cycle) != 0) {
        signed char next_cycle = pending_cycle;
        SetCycle(next_cycle);
        pending_cycle = -1;

        if (pending_subcycle != 0xffff) {
            unsigned char requested_subcycle = static_cast<unsigned char>(pending_subcycle);
            signed char subcycle_count = GetNumSubCycles();

            if (static_cast<int>(requested_subcycle) < static_cast<int>(subcycle_count)) {
                subcycle = requested_subcycle;
            }
            pending_subcycle = 0xffff;
        }

        if (pending_behaviour != W8_ANIMATION_NO_PENDING_BEHAVIOUR) {
            const auto behaviour = pending_behaviour;

            if (behaviour < BEHAVIOUR_FIRST || behaviour > BEHAVIOUR_LAST) {
                Fail("(bBehaviour >= BEHAVIOUR_FIRST) && (bBehaviour <= BEHAVIOUR_LAST)");
            }
            animation_behaviour = behaviour;
            pending_behaviour = W8_ANIMATION_NO_PENDING_BEHAVIOUR;
        }

        glm::vec3 position = GetPosition();
        UpdateSoundEvents(sound_events, position,
                          W8_SOUND_EVENT_FRAME | W8_SOUND_EVENT_CYCLE | W8_SOUND_EVENT_FOOTSTEP,
                          current_cycle, subcycle, current_subcycle);
        if (!shake_events.empty()) {
            TriggerShakeEvents(shake_events, current_cycle, subcycle, current_subcycle, position);
        }

        signed char subcycle_count = GetNumSubCycles();
        if (subcycle >= static_cast<unsigned char>(subcycle_count)) {
            subcycle = subcycle_count - 1;
        }
        animation_playing = 1;
        timer = GameClockMilliseconds();
        return 1;
    }
    return 0;
}

void W8GrCycle::UpdateLights()
{
    int count = static_cast<int>(m_plsLights->size());
    int index;

    for (index = 0; index < count; ++index) {
        stLight* light = (*m_plsLights)[index].get();
        stLightDefinition* definition = light->definition();

        if (definition == 0) {
            continue;
        }
        if (definition->kind == W8_LIGHT_DEFINITION_PARAMETRIC) {
            stParametricLightDefinition* cycle_definition =
                static_cast<stParametricLightDefinition*>(definition);
            if ((cycle_definition->flags & 3) == 3 && (cycle_definition->flags & 0x40) != 0) {
                if (!cycle_definition->IsEnabledForSubcycle(subcycle)) {
                    if (light->getParent() != 0) {
                        light->setParent(0, 0);
                    }
                } else if ((subcycle == 0 && cycle_definition->subcycle_min == 0) ||
                           light->getParent() == nullptr) {
                    light->setParent(owner_world ? owner_world->dynamic_scene : nullptr, 0);
                    light->m_path_index = 0;
                    light->m_path_direction = 1;
                    light->Reset();
                }
            }
        } else if (definition->kind == W8_LIGHT_DEFINITION_KEYFRAMED) {
            if (subcycle == 0 || wrapped) {
                light->Reset();
            }
            light->SetDefinitionTime(subcycle + frame_fraction);
            if (!definition->IsEnabledForSubcycle(0)) {
                if (light->getParent() != 0) {
                    light->setParent(0, 0);
                }
            } else if (light->getParent() == nullptr) {
                light->setParent(owner_world ? owner_world->dynamic_scene : nullptr, 0);
            }
        }
    }
}

std::optional<srBoundingBox> W8GrCycle::GetAnimationBounds()
{
    return AnimObjGetBounds(GetCurrentAnimation(), m_bLOD, subcycle);
}

unsigned char W8GrCycle::GetAnimationRadius(float* arg_radius)
{
    W8AniMesh* mesh = GetCurrentAniMesh();

    if (mesh == 0) {
        Fail("0");
        return 0;
    }
    return AniMeshRadius(mesh, arg_radius);
}

void W8GrCycle::AdvanceAnimationFrame(int, int)
{
    W8AnimObj* animation = GetCurrentAnimation();
    unsigned char frame;

    if (animation_behaviour == W8_ANIMATION_PLAY_ONCE) {
        if (frame_direction == W8_ANIMATION_FORWARD) {
            frame = subcycle;
            if (frame < last_frame) {
                subcycle = frame + 1;
            } else {
                animation_playing = 0;
            }
        } else if (frame_direction == W8_ANIMATION_REVERSE) {
            frame = subcycle;
            if (frame > first_frame) {
                subcycle = frame - 1;
            } else {
                animation_playing = 0;
            }
        }
    } else if (frame_direction == W8_ANIMATION_FORWARD) {
        frame = subcycle;
        if (frame != last_frame) {
            subcycle = frame + 1;
        } else if (frame_method == W8_ANIMATION_PING_PONG) {
            subcycle = frame - 1;
            frame_direction = W8_ANIMATION_REVERSE;
        } else if (frame_method == W8_ANIMATION_WRAP) {
            subcycle = first_frame;
            wrapped = true;
        }
    } else if (frame_direction == W8_ANIMATION_REVERSE) {
        frame = subcycle;
        if (frame > first_frame) {
            subcycle = frame - 1;
        } else if (frame_method == W8_ANIMATION_PING_PONG) {
            subcycle = first_frame + 1;
            frame_direction = W8_ANIMATION_FORWARD;
        } else if (frame_method == W8_ANIMATION_WRAP) {
            subcycle = last_frame;
        }
    }

    if (animation->path_lists == 1) {
        unsigned int count = AnimObjListCount(animation, m_bLOD);
        unsigned int index;

        for (index = 0; index < count; ++index) {
            W8PathAI* path = AnimObjListEntry(animation, m_bLOD, static_cast<signed char>(index));
            if (path != 0) {
                PathAISetValue(path, static_cast<float>(subcycle));
            }
        }
    }
}

void W8GrCycle::ResetRepresentation()
{
    PathAIResetRecord(m_pAI.get());
    frame_direction = W8_ANIMATION_FORWARD;
    subcycle = 0;
    timer = GameClockMilliseconds();
}

/* Push the cycle's animation state into the live scene.

   A running animation places every model instance its current list holds; a
   stopped one places the single instance the cycle/frame/LOD selector returns,
   and then walks that instance's children when it has any. Either way the
   representation's four-dword render block and its optional scale ride along,
   and the last instance placed becomes the cached one.

   The ground shadow and owned lights attach to the supplied world as well. */
void W8GrCycle::SetPresentationColour(glm::vec4 colour)
{
    if (!rep_active) return;
    auto* animation = GetCurrentAnimation();
    if (!animation) return;
    if (animation->path_lists == 1) {
        const int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));
        for (int index = 0; index < count; ++index) {
            auto* model = static_cast<stModelInstance*>(
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index)));
            if (model) model->highlight_colour = colour;
        }
    } else if (auto* model = static_cast<stModelInstance*>(GetCurrentModelInstance())) {
        model->highlight_colour = colour;
    }
}

void W8GrCycle::UpdateRepresentation(W8World* pWorld)
{
    W8AnimObj* animation;
    glm::mat3 rot;
    glm::dvec3 loc;
    glm::vec3 vecPos;
    int index;

    if (pWorld == 0) {
        Fail("pWorld");
    }
    if (rep_active == 0) {
        return;
    }
    animation = GetCurrentAnimation();
    if (animation->path_lists == 1) {
        int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));

        for (index = 0; index < count; ++index) {
            stModelInstance* psrMesh = static_cast<stModelInstance*>(
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index)));
            W8PathAI* path;
            glm::mat3 current;

            if (psrMesh == 0) {
                Fail("psrMesh");
            }
            psrMesh->highlight_colour = highlight_colour;
            if (apply_instance_scale) {
                if (instance_scale == 1.0f) {
                    psrMesh->diffuse_scale_enabled = false;
                } else {
                    psrMesh->diffuse_scale_enabled = true;
                    psrMesh->diffuse_scale = instance_scale;
                }
            }
            psrMesh->clearFlag(srNode::FLAG_DISABLE);
            psrMesh->setParent(pWorld->dynamic_scene, 0);
            path = AnimObjListEntry(animation, m_bLOD, static_cast<signed char>(index));
            if (path != 0) {
                PathAIApply(path, psrMesh);
            }
            vecPos = movement.position;
            loc = glm::dvec3(vecPos);
            loc.y += movement.vertical_offset;
            psrMesh->setLocation(loc);
            rot = GetRotation();
            current = psrMesh->getRotation<glm::mat3>();
            rot = rot * current;
            psrMesh->setRotation(rot);
            current_model_instance = psrMesh;
        }
    } else {
        stModelInstance* psrMesh =
            static_cast<stModelInstance*>(SelectCycleFrameLod(current_cycle, subcycle, m_bLOD));
        srNode* child;

        if (psrMesh == 0) {
            Fail("psrMesh");
        }
        SetAniMeshCacheProtected(GetEmitterAniMesh(current_cycle), true);
        psrMesh->highlight_colour = highlight_colour;
        if (apply_instance_scale) {
            if (instance_scale == 1.0f) {
                psrMesh->diffuse_scale_enabled = false;
            } else {
                psrMesh->diffuse_scale_enabled = true;
                psrMesh->diffuse_scale = instance_scale;
            }
        }
        vecPos = movement.position;
        loc = glm::dvec3(vecPos);
        loc.y += movement.vertical_offset;
        rot = GetRotation();
        child = psrMesh->getChild();
        if (child == 0) {
            psrMesh->setLocation(loc);
            psrMesh->setRotation(rot);
            psrMesh->clearFlag(srNode::FLAG_DISABLE);
        } else {
            do {
                child->setLocation(loc);
                child->setRotation(rot);
                child = child->getNext();
            } while (child != 0);
        }
        current_model_instance = psrMesh;
        psrMesh->clearFlag(srNode::FLAG_TERMINATE);
        psrMesh->setParent(pWorld->dynamic_scene, 0);
    }
    UpdateParticleAttachments();
    if (m_ground_shadow != 0) {
        glm::vec3 position = GetPosition();

        loc = glm::dvec3(position);
        m_ground_shadow->setLocation(loc);
        m_ground_shadow->angle = GetYaw();
        m_ground_shadow->clearFlag(srNode::FLAG_TERMINATE);
        m_ground_shadow->setParent(pWorld->dynamic_scene, 0);
    }
    if (m_plsLights != 0) {
        int count = static_cast<int>(m_plsLights->size());
        glm::vec3 origin;

        if (animation->path_lists == 1) {
            origin = GetLocation();
        } else {
            vecPos = movement.position;
            origin = vecPos;
            origin.y += movement.vertical_offset;
        }
        rot = GetRotation();
        for (index = 0; index < count; ++index) {
            stLight* light = (*m_plsLights)[index].get();
            glm::vec3 offset = light->m_position;

            glm::vec3 placed = rot * offset + origin;
            loc = glm::dvec3(placed);
            light->setLocation(loc);
        }
    }
}

void W8GrCycle::DetachRepresentation(W8World* world)
{
    W8AnimObj* animation = GetCurrentAnimation();

    if (animation->path_lists == 1) {
        int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));
        for (int index = 0; index < count; ++index) {
            srModelInstance* mesh =
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index));
            if (mesh == 0) {
                Fail("psrMesh");
            }
            W8AniMesh* ani_mesh = GetEmitterAniMesh(current_cycle);
            if (ani_mesh != 0) {
                SetAniMeshCacheProtected(ani_mesh, false);
            }
            mesh->setFlag(srNode::FLAG_DISABLE);
            mesh->setParent(0, 1);
        }
    } else if (current_model_instance != 0) {
        if (world == 0) {
            Fail("pWorld");
        }
        W8AniMesh* ani_mesh = GetEmitterAniMesh(current_cycle);
        SetAniMeshCacheProtected(ani_mesh, false);
        current_model_instance->setFlag(srNode::FLAG_DISABLE);
        current_model_instance->setFlag(srNode::FLAG_TERMINATE);
        current_model_instance->setParent(0, 0);
        current_model_instance = 0;
    }

    if (m_ground_shadow != 0) {
        m_ground_shadow->setFlag(srNode::FLAG_TERMINATE);
        m_ground_shadow->setParent(0, 1);
    }
}

/* Place every particle this cycle has attached to its model.

   An attachment normally rides a named vertex of the model's mesh: the
   particle's own key is looked up through the mesh-model chain, and the vertex
   it maps to gives the offset. An attachment whose key resolves to nothing
   falls back to the offset it carries itself. Either way the offset is scaled
   and rotated into the model instance's frame and added to its location.

   The particle's own mode then decides its orientation: mode three aims it
   along the cycle's stored axis, mode four leaves the orientation alone, and
   anything else composes the attachment's own rotation with the model's. */
void W8GrCycle::UpdateParticleAttachments()
{
    int count;
    int index;
    srModelInstance* psrMesh;
    stMeshModel* pMeshModel = nullptr;
    glm::mat3 rot;
    glm::dvec3 scale_vec;
    glm::dvec3 loc;
    glm::dvec3 target;
    glm::dvec3 axis;
    glm::mat3 combined;
    glm::dmat3 world;
    glm::vec3 offset;
    glm::vec3 placed;
    int vertex;
    float scale_x;
    float scale_y;
    float scale_z;

    count = static_cast<int>(particles.size());
    if (count == 0) {
        return;
    }
    psrMesh = current_model_instance;
    if (psrMesh == 0) {
        W8AnimObj* animation = GetCurrentAnimation();

        if (animation->path_lists == 1) {
            psrMesh = AnimObjDispatchList(animation, m_bLOD, 0);
        } else {
            psrMesh = SelectCycleFrameLod(current_cycle, subcycle, m_bLOD);
        }
    }
    /* A null selected mesh implies a null cached instance; the transform
       reads below dereference that instance before the model is consumed. */
    if (psrMesh != 0) {
        pMeshModel = static_cast<stMeshModel*>(psrMesh->getModel());
    }

    rot = current_model_instance->getRotation<glm::mat3>();
    scale_vec = current_model_instance->getScale();
    scale_x = static_cast<float>(scale_vec.x);
    scale_y = static_cast<float>(scale_vec.y);
    scale_z = static_cast<float>(scale_vec.z);

    for (index = 0; index < count; ++index) {
        W8GrCycleParticleAttachment* attachment = &particles[index];
        stParticle* particle = attachment->m_pstParticles.get();
        if (!particle->emitting) {
            continue;
        }
        if (pMeshModel != 0) {
            short key = particle->attachment_key;

            if (key < 0) {
                vertex = -1;
            } else {
                stMeshModel* scan = pMeshModel;

                do {
                    vertex = scan->FindMappedIndex(key);
                    if (vertex != -1) {
                        break;
                    }
                    scan = scan->Next();
                } while (scan != 0);
                pMeshModel = scan;
            }
        } else {
            vertex = -1;
        }
        if (pMeshModel == 0 || vertex == -1) {
            offset = attachment->position;
        } else {
            glm::vec3* locations;

            if ((pMeshModel->flags & W8_MESH_HAS_FRAME_STORAGE) == 0) {
                locations = pMeshModel->getVertexLoc();
            } else {
                locations = pMeshModel->GetVertexLocations(subcycle, true, 0);
            }
            if (vertex >= pMeshModel->vertex_location_count) {
                vertex = 0;
            }
            offset = locations[vertex];
        }
        offset.x *= scale_x;
        offset.y *= scale_y;
        offset.z *= scale_z;
        placed = rot * offset;
        loc = current_model_instance->getLocation();
        glm::vec3 anchor;
        anchor = glm::vec3(loc);
        placed += anchor;

        if (aim_set && particle->direction_mode == W8_PARTICLE_DIRECTION_CONE) {
            target = glm::dvec3(placed);
            axis = glm::dvec3(m_axis);
            particle->setRotation(axis, target, 0.0);
        } else if (particle->direction_mode != W8_PARTICLE_DIRECTION_RANDOM) {
            combined = rot;
            combined = combined * (attachment->rotation);
            world = glm::dmat3(combined);
            particle->setWorldSpaceRotation(world);
        }
        loc = glm::dvec3(placed);
        particle->setLocation(loc);
    }
}

/* Choose the level of detail for the current cycle from the distance to the
   listener and from which LODs the cycle actually has.

   Availability is probed by asking the representation for each LOD's frame
   zero; a cycle with none of the three is a content error and says so. The
   distance thresholds are the representation's own pair scaled by the detail
   slider, and LOD 2 is the near one. The tail then lets the fog/detail setting
   override the distance choice in either direction, but only towards a LOD the
   cycle has. */
void W8GrCycle::SelectLOD(const glm::vec3* position)
{
    glm::vec3 delta = location - *position;
    float distance = glm::length(delta);
    bool has_lod_2 = SetCycleFrameLod(current_cycle, 0, 2) != 0;
    bool has_lod_1 = SetCycleFrameLod(current_cycle, 0, 1) != 0;
    bool has_lod_0 = SetCycleFrameLod(current_cycle, 0, 0) != 0;

    if (!has_lod_2 && !has_lod_1 && !has_lod_0) {
        ShutdownWithErrorBox("Monster has no valid LODs!");
    }
    if (g_render_brightness * lod_near > distance) {
        if (has_lod_2) {
            m_bLOD = 2;
        } else if (has_lod_1) {
            m_bLOD = 1;
        } else {
            m_bLOD = 0;
        }
    } else if (g_render_brightness * lod_far > distance) {
        if (has_lod_1) {
            m_bLOD = 1;
        } else if (has_lod_2) {
            m_bLOD = 2;
        } else {
            m_bLOD = 0;
        }
    } else {
        if (has_lod_0) {
            m_bLOD = 0;
        } else if (has_lod_1) {
            m_bLOD = 1;
        } else {
            m_bLOD = 2;
        }
    }
    if (g_render_fog_distance <= 0.1f && m_bLOD != 2) {
        if (has_lod_2) {
            m_bLOD = 2;
        }
        return;
    }
    if ((g_render_fog_distance <= kLodLevelZeroExitThreshold && m_bLOD == 0) ||
        (g_render_fog_distance >= kLodLevelTwoExitThreshold && m_bLOD == 2)) {
        if (has_lod_1) {
            m_bLOD = 1;
        }
        return;
    }
    if (g_render_fog_distance >= 0.9f && m_bLOD != 0 && has_lod_0) {
        m_bLOD = 0;
    }
}

/* Select the model instance for the representation's current cycle, frame,
   and LOD. AnimObj's frame lookup is the source of the returned instance; the
   callers immediately use it as an srNode/srModelInstance. */
srModelInstance* W8GrCycle::GetCurrentModelInstance()
{
    if (current_model_instance != 0) {
        return current_model_instance;
    }

    W8AnimObj* animation = GetCurrentAnimation();
    if (animation->path_lists == 1) {
        return AnimObjDispatchList(animation, m_bLOD, 0);
    }

    return SelectCycleFrameLod(current_cycle, subcycle, m_bLOD);
}

srModelInstance* W8GrCycle::SelectCycleFrameLod(signed char cycle, signed char frame,
                                                signed char lod)
{
    if (lod < 0 || lod >= 3) {
        Fail("bLOD >= 0 && bLOD < NUM_LODS");
    }
    if (frame < 0) {
        Fail("bFrame >= 0");
    }
    if (cycle < 0) {
        Fail("bCycle >= 0");
    }
    if (m_pAI != 0) {
        PathAIApplyToRep(m_pAI.get(), this);
    }
    return SetCycleFrameLod(cycle, frame, lod);
}

unsigned char W8GrCycle::ReplacePath(W8PathAI* path)
{
    m_pAI.reset(path);
    return 1;
}

void W8GrCycle::SubmitTargetValue()
{
    GetAnimationRadius(&animation_radius);
}

struct RegisteredCycles {
    std::string name;
    std::vector<W8GrCycle*> cycles; // Borrowed; each cycle unregisters on destruction.
};

// Stable records keep borrowed name strings valid while other names are registered.
static std::list<RegisteredCycles> g_registered_cycles;

void W8GrCycle::SetSubCycle(unsigned char new_subcycle)
{
    signed char count = GetNumSubCycles();

    if (new_subcycle < count) {
        subcycle = new_subcycle;
    }
}

// FUNCTION: WIZ8 0x004a8460
void W8GrCycle::SetBehaviour(W8AnimationBehaviour bBehaviour)
{
    if (bBehaviour < BEHAVIOUR_FIRST || bBehaviour > BEHAVIOUR_LAST) {
        Fail("(bBehaviour >= BEHAVIOUR_FIRST) && (bBehaviour <= BEHAVIOUR_LAST)");
    }
    animation_behaviour = bBehaviour;
}

void W8GrCycleParticleAttachment::SelectCycle(signed char selected_cycle)
{
    if (cycle == selected_cycle) {
        m_pstParticles->SetActive(true);
        m_pstParticles->emission_count = 0;
    } else {
        m_pstParticles->SetActive(false);
    }
}

void W8GrCycle::DetachCycleLights(std::vector<std::unique_ptr<stLight>>* lights)
{
    if (lights != 0) {
        for (const auto& owned : *lights) {
            auto* light = owned.get();

            light->setParent(0, 1);
        }
    }
}

void W8GrCycle::AttachCycleLights(std::vector<std::unique_ptr<stLight>>* lights)
{
    if (owner_world && g_render_missile_lights && lights != 0) {
        for (const auto& owned : *lights) {
            auto* light = owned.get();

            light->setParent(owner_world ? owner_world->dynamic_scene : nullptr, 1);
        }
    }
}

void W8GrCycle::SetLights(std::vector<std::unique_ptr<stLight>>* lights)
{
    m_plsLights = lights;
}

void W8GrCycle::AddShakeEffect(ShakeDefinition effect)
{
    shake_events.push_back(effect);
}

static const RegisteredCycles* FindRegistration(const W8GrCycle* cycle)
{
    const auto entry = std::ranges::find_if(g_registered_cycles, [cycle](const auto& entry) {
        return std::ranges::contains(entry.cycles, cycle);
    });
    return entry == g_registered_cycles.end() ? nullptr : &*entry;
}

std::string_view W8GrCycle::GetRegisteredName() const
{
    const auto* entry = FindRegistration(this);
    return entry ? std::string_view(entry->name) : std::string_view{};
}

bool W8GrCycle::IsSoleRegisteredCycleForName() const
{
    const auto* entry = FindRegistration(this);
    return !entry || entry->cycles.size() == 1;
}

W8GrCycle* FindFirstGrCycleByName(std::string_view name)
{
    const auto entry = std::ranges::find_if(g_registered_cycles, [name](const auto& entry) {
        return CompareTextIgnoreAsciiCase(name, entry.name) == 0;
    });
    return entry == g_registered_cycles.end() ? nullptr : entry->cycles.front();
}

unsigned char UnregisterGrCycle(W8GrCycle* cycle)
{
    for (auto entry = g_registered_cycles.begin(); entry != g_registered_cycles.end(); ++entry) {
        if (!wiz8::erase_first(entry->cycles, cycle)) {
            continue;
        }
        if (!entry->cycles.empty()) {
            return false;
        }
        g_registered_cycles.erase(entry);
        return true;
    }
    return false;
}

void RegisterGrCycle(std::string_view name, W8GrCycle* cycle)
{
    const auto entry = std::ranges::find_if(g_registered_cycles, [name](const auto& entry) {
        return CompareTextIgnoreAsciiCase(name, entry.name) == 0;
    });
    if (entry != g_registered_cycles.end()) {
        entry->cycles.push_back(cycle);
    } else {
        g_registered_cycles.push_back({std::string(name), {cycle}});
    }
}

int FindMappedIndexInMeshChain(stMeshModel** mesh, int key)
{
    int result;
    stMeshModel* current;

    if (key < 0 || mesh == 0 || *mesh == 0) {
        return -1;
    }

    current = *mesh;
    while (current != 0) {
        result = current->FindMappedIndex(static_cast<short>(key));
        if (result != -1) {
            break;
        }
        current = current->Next();
    }

    *mesh = current;
    return result;
}

void W8GrCycle::CreateGroundShadow(float width, float depth)
{
    m_ground_shadow = std::make_unique<stGroundShadow>(nullptr);
    m_ground_shadow->setName("Ground Shadow");
    m_ground_shadow->width = width;
    m_ground_shadow->depth = depth;
}

void W8GrCycle::SetGroundShadowVisible(bool visible)
{
    if (m_ground_shadow != 0) {
        if (visible) {
            m_ground_shadow->clearFlag(srNode::FLAG_DISABLE);
        } else {
            m_ground_shadow->setFlag(srNode::FLAG_DISABLE);
        }
    }
}

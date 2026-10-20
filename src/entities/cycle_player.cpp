#include "wiz8/entities/cycle_player.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/geometric.hpp>

#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/application.h"
#include "wiz8/asset_paths.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/ground_shadow.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/quality.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

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
    : definition(definition),
      timer(std::isfinite(definition.duration) ? definition.duration : 0.0f, false), key(key)
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
    if (!definition.continuous && !(definition.duration > 0.0f)) {
        return false;
    }
    const float progress =
        (definition.continuous || std::isinf(definition.duration)) ? 0.0f : timer.GetProgress();
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
        g_trigger_action_scene_offset.x = PresentationRng().below(span) - amplitude;
        g_trigger_action_scene_offset.y = PresentationRng().below(span) - amplitude;
        g_trigger_action_scene_offset.z = PresentationRng().below(span) - amplitude;
        g_trigger_action_active = true;
    } else {
        g_trigger_action_active = false;
    }
}

/* Build the two paths used while reading a .mon resource, verify its one-byte
   version, and hand the open file plus its resource context to the typed cycle
   reader. */
static bool readCyclePrefix(LevelLoadContext* info, CyclePlayer& cycle, int cycle_index)
try {
    unsigned char has_path;
    unsigned char has_particles;
    bool success = true;

    if (info == 0) {
        Fail("pInfo");
    }

    info->hFile->read(&has_path, 1);
    if (has_path != 0) {
        cycle.m_pAI = LoadPathAI(*info->hFile);
    }

    info->hFile->read(&has_particles, 1);
    if (has_particles != 0) {
        std::vector<std::unique_ptr<ParticleSystem>> particles;

        success = ReadWorldParticles(info, info->world ? (info->world->scene.get()) : nullptr,
                                     &particles);
        if (!particles.empty()) {
            int index;

            for (index = 0; index < static_cast<int>(particles.size()); ++index) {
                // The attachment takes ownership below.
                ParticleSystem* particle = particles[index].get();
                auto* event = &cycle.particles.emplace_back();
                glm::dvec3 location;

                event->cycle = cycle_index;
                event->subcycle = -1;
                event->m_pstParticles = std::move(particles[index]);
                location = particle->transform.getLocation();
                event->position = glm::vec3(location);
                event->rotation = particle->transform.getRotation<glm::mat3>();
            }
        }
    }

    return success;
} catch (const std::exception&) {
    return false;
}

bool loadAnimationCycle(std::string_view mon_name, CyclePlayer& cycle, int cycle_index, int value,
                        std::string_view directory,
                        const std::function<bool(LevelLoadContext&, int, int)>& read_payload,
                        std::string_view bitmap_directory)
try {
    LevelLoadContext info;
    unsigned char version;
    bool success;

    if (mon_name.empty()) {
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

    info.world = cycle.owner_world;
    info.hFile = handle.get();
    info.bitmap_folder = wiz8::asset_path(bitmap_path);
    success = true;

    if ((handle->read(&version, 1).bytes == static_cast<std::size_t>(1)) == 0 || version != 1 ||
        !readCyclePrefix(&info, cycle, cycle_index) || !read_payload(info, cycle_index, value)) {
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
CyclePlayer::CyclePlayer(World* owner) : owner_world(owner)
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
CyclePlayer::CyclePlayer(const CyclePlayer& other, World* owner)
    : owner_world(owner), location(other.location), local_location(other.local_location),
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
    sound_events = other.sound_events;
    m_pAI = other.m_pAI;
    if (m_pAI) m_pAI->timed = false;
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
        if (source.m_pstParticles) {
            copy.m_pstParticles = std::make_unique<ParticleSystem>(*source.m_pstParticles);
            copy.m_pstParticles->setScene(owner ? owner->scene.get() : nullptr);
        }
        copy.position = source.position;
        copy.rotation = source.rotation;
    }
    if (other.m_ground_shadow)
        m_ground_shadow = std::make_unique<GroundShadow>(*other.m_ground_shadow);
}

W8SoundEvent& CyclePlayer::AddSoundEvent(W8SoundEvent event)
{
    return sound_events.emplace_back(std::move(event));
}

void CyclePlayer::SetLocation(const glm::vec3* new_location)
{
    local_location = *new_location;
    location = parent_location + local_location;
}

glm::vec3 CyclePlayer::GetLocation() const
{
    return location;
}

glm::vec3 CyclePlayer::GetLocalLocation() const
{
    return local_location;
}

void CyclePlayer::SetRotation(const glm::mat3* new_rotation)
{
    rotation = *new_rotation;
}

glm::mat3 CyclePlayer::GetRotation() const
{
    return rotation;
}

void CyclePlayer::SetFrameMethod(W8AnimationFrameMethod method)
{
    if (method < 1 || method > 4) {
        Fail("bFrameMethod >= DIR_FIRST && bFrameMethod <= DIR_LAST");
    }
    frame_method = method;
}

CyclePlayer::~CyclePlayer()
{
    for (const auto& event : shake_events) {
        CameraShakes().Stop(event);
    }
}

void CyclePlayer::RetireParticles(World& world)
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

void CyclePlayer::TickAnimation(float rate, const glm::vec3& position, bool reset_frame_bounds,
                                const std::function<bool(unsigned char)>& frame_event)
{
    if (animation_playing != 0) {
        signed char subcycle_count = frameCount();

        if (subcycle_count != 0) {
            unsigned int now = GameClockMilliseconds();
            unsigned int elapsed = now - timer;
            float progress;
            int frames;

            if (elapsed > 1000) {
                elapsed = 1000;
                timer = now - 1000;
            }

            progress = elapsed * rate * 0.001f;
            /* A non-finite or negative rate would convert to a frame count the
               countdown loop below never reaches zero from. */
            if (!(progress >= 0.0f && progress < 1.0e6f)) {
                progress = 0.0f;
            }
            frames = static_cast<int>(progress);
            frame_fraction = progress - frames;

            if (frames != 0) {

                timer += static_cast<int>(frames * 1e+03f / rate);
                wrapped = false;
                do {
                    --frames;
                    if (reset_frame_bounds) {
                        first_frame = 0;
                        last_frame = frameCount() - 1;
                    }
                    const auto previous_frame = subcycle;
                    advanceFrame();
                    if (frame_event(previous_frame)) {
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

void CyclePlayer::finishCycleSelection(const glm::vec3& position)
{
    pending_cycle = -1;

    if (pending_subcycle != 0xffff) {
        unsigned char requested_subcycle = static_cast<unsigned char>(pending_subcycle);
        signed char subcycle_count = frameCount();

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

    UpdateSoundEvents(sound_events, position,
                      W8_SOUND_EVENT_FRAME | W8_SOUND_EVENT_CYCLE | W8_SOUND_EVENT_FOOTSTEP,
                      current_cycle, subcycle, current_subcycle);
    if (!shake_events.empty()) {
        TriggerShakeEvents(shake_events, current_cycle, subcycle, current_subcycle, position);
    }

    signed char subcycle_count = frameCount();
    if (subcycle >= static_cast<unsigned char>(subcycle_count)) {
        subcycle = subcycle_count - 1;
    }
    animation_playing = 1;
    timer = GameClockMilliseconds();
}

void CyclePlayer::UpdateLights()
{
    int count = static_cast<int>(m_plsLights->size());
    int index;

    for (index = 0; index < count; ++index) {
        Light* light = (*m_plsLights)[index].get();
        if (auto* cycle_definition = std::get_if<ParametricLightAnim>(&light->animator)) {
            if ((cycle_definition->flags & 3) == 3 && (cycle_definition->flags & 0x40) != 0) {
                if (!cycle_definition->IsEnabledForSubcycle(subcycle)) {
                    if (light->scene != 0) {
                        light->bake_into_mesh = true;
                        light->setScene(0);
                    }
                } else if ((subcycle == 0 && cycle_definition->subcycle_min == 0) ||
                           light->scene == nullptr) {
                    light->bake_into_mesh = true;
                    light->setScene(owner_world ? (owner_world->scene.get()) : nullptr);
                    light->m_path_index = 0;
                    light->m_path_direction = 1;
                    light->Reset();
                }
            }
        } else if (auto* definition = std::get_if<KeyframedLightAnim>(&light->animator)) {
            if (subcycle == 0 || wrapped) {
                light->Reset();
            }
            light->SetDefinitionTime(subcycle + frame_fraction);
            if (!definition->IsEnabledForSubcycle(0)) {
                if (light->scene != 0) {
                    light->bake_into_mesh = true;
                    light->setScene(0);
                }
            } else if (light->scene == nullptr) {
                light->bake_into_mesh = true;
                light->setScene(owner_world ? (owner_world->scene.get()) : nullptr);
            }
        }
    }
}

std::optional<BoundingBox> CyclePlayer::GetAnimationBounds()
{
    return AnimObjGetBounds(active_animation, m_bLOD, subcycle);
}

unsigned char CyclePlayer::GetAnimationRadius(float* arg_radius)
{
    AnimationMesh* mesh = AnimObjEntry(active_animation, m_bLOD, 0);

    if (mesh == 0) {
        Fail("0");
        return 0;
    }
    return AniMeshRadius(mesh, arg_radius);
}

void CyclePlayer::advanceFrame()
{
    Animation* animation = active_animation;
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
            KeyframeTrack* path =
                AnimObjListEntry(animation, m_bLOD, static_cast<signed char>(index));
            if (path != 0) {
                PathAISetValue(path, static_cast<float>(subcycle));
            }
        }
    }
}

void CyclePlayer::ResetRepresentation()
{
    PathAIResetRecord((m_pAI ? &*m_pAI : nullptr));
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
void CyclePlayer::SetPresentationColour(glm::vec4 colour)
{
    if (!rep_active) return;
    auto* animation = active_animation;
    if (!animation) return;
    if (animation->path_lists == 1) {
        const int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));
        for (int index = 0; index < count; ++index) {
            auto* model = static_cast<MeshInstance*>(
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index)));
            if (model) model->highlight_colour = colour;
        }
    } else if (auto* model = static_cast<MeshInstance*>(GetCurrentModelInstance())) {
        model->highlight_colour = colour;
    }
}

void CyclePlayer::UpdateRepresentation(World* pWorld, const Navigator& nav)
{
    Animation* animation;
    glm::mat3 rot;
    glm::dvec3 loc;
    glm::vec3 vecPos;
    int index;

    if (pWorld == 0) {
        Fail("pWorld");
    }
    if (!rep_active) {
        return;
    }
    animation = active_animation;
    if (animation->path_lists == 1) {
        int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));

        for (index = 0; index < count; ++index) {
            MeshInstance* psrMesh = static_cast<MeshInstance*>(
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index)));
            KeyframeTrack* path;
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
            psrMesh->visible = true;
            psrMesh->setScene((pWorld->scene.get()));
            path = AnimObjListEntry(animation, m_bLOD, static_cast<signed char>(index));
            if (path != 0) {
                (psrMesh)->transform = sampleKeyframeTrack(*(path), (psrMesh)->transform);
            }
            vecPos = nav.movement.position;
            loc = glm::dvec3(vecPos);
            loc.y += nav.movement.vertical_offset;
            psrMesh->transform.setLocation(loc);
            rot = GetRotation();
            current = psrMesh->transform.getRotation<glm::mat3>();
            rot = rot * current;
            psrMesh->transform.setRotation(rot);
            current_model_instance = psrMesh;
        }
    } else {
        MeshInstance* psrMesh = static_cast<MeshInstance*>(SelectCycleFrameLod(subcycle, m_bLOD));

        if (psrMesh == 0) {
            Fail("psrMesh");
        }
        SetAniMeshCacheProtected(AnimObjEntry(active_animation, m_bLOD, 0), true);
        psrMesh->highlight_colour = highlight_colour;
        if (apply_instance_scale) {
            if (instance_scale == 1.0f) {
                psrMesh->diffuse_scale_enabled = false;
            } else {
                psrMesh->diffuse_scale_enabled = true;
                psrMesh->diffuse_scale = instance_scale;
            }
        }
        vecPos = nav.movement.position;
        loc = glm::dvec3(vecPos);
        loc.y += nav.movement.vertical_offset;
        rot = GetRotation();

        psrMesh->transform.setLocation(loc);
        psrMesh->transform.setRotation(rot);
        psrMesh->visible = true;

        current_model_instance = psrMesh;
        psrMesh->draw_parts = true;
        psrMesh->setScene((pWorld->scene.get()));
    }
    UpdateParticleAttachments();
    if (m_ground_shadow != 0) {
        glm::vec3 position = nav.GetPosition();

        loc = glm::dvec3(position);
        m_ground_shadow->transform.setLocation(loc);
        m_ground_shadow->angle = nav.GetYaw();

        m_ground_shadow->setScene((pWorld->scene.get()));
    }
    if (m_plsLights != 0) {
        int count = static_cast<int>(m_plsLights->size());
        glm::vec3 origin;

        if (animation->path_lists == 1) {
            origin = GetLocation();
        } else {
            vecPos = nav.movement.position;
            origin = vecPos;
            origin.y += nav.movement.vertical_offset;
        }
        rot = GetRotation();
        for (index = 0; index < count; ++index) {
            Light* light = (*m_plsLights)[index].get();
            if (animation->path_lists != 1 && current_model_instance) {
                // The light is a child of the visual. VSG composes it for
                // rendering; the bake query reads that same parentage.
                light->transform.setLocation(glm::dvec3(light->m_position));
                if (light->scene) light->attachTo(*current_model_instance);
            } else {
                const auto placed = rot * light->m_position + origin;
                light->transform.setLocation(glm::dvec3(placed));
            }
        }
    }
}

void CyclePlayer::DetachRepresentation(World* world)
{
    Animation* animation = active_animation;

    if (animation->path_lists == 1) {
        int count = static_cast<int>(AnimObjListCount(animation, m_bLOD));
        for (int index = 0; index < count; ++index) {
            MeshInstance* mesh =
                AnimObjDispatchList(animation, m_bLOD, static_cast<signed char>(index));
            if (mesh == 0) {
                Fail("psrMesh");
            }
            AnimationMesh* ani_mesh = AnimObjEntry(active_animation, m_bLOD, 0);
            if (ani_mesh != 0) {
                SetAniMeshCacheProtected(ani_mesh, false);
            }
            mesh->visible = false;
            mesh->setScene(0);
        }
    } else if (current_model_instance != 0) {
        if (world == 0) {
            Fail("pWorld");
        }
        AnimationMesh* ani_mesh = AnimObjEntry(active_animation, m_bLOD, 0);
        SetAniMeshCacheProtected(ani_mesh, false);
        current_model_instance->visible = false;
        current_model_instance->draw_parts = false;
        current_model_instance->setScene(0);
        current_model_instance = 0;
    }

    if (m_ground_shadow != 0) {

        m_ground_shadow->setScene(0);
    }
}

/* Place every particle this cycle has attached to its model.

   An attachment normally rides a named vertex of the model's mesh: the
   particle's own key is looked up through the mesh-model chain, and the vertex
   it maps to gives the offset. An attachment whose key resolves to nothing
   falls back to the offset it carries itself. The same sampled pose used by
   rendering supplies the vertex; the instance's accumulated transform places it.

   The particle's own mode then decides its orientation: mode three aims it
   along the cycle's stored axis, mode four leaves the orientation alone, and
   anything else composes the attachment's own rotation with the model's. */
void CyclePlayer::UpdateParticleAttachments()
{
    if (particles.empty()) return;
    auto* psrMesh = current_model_instance;
    if (!psrMesh) {
        auto* animation = active_animation;
        if (!animation) return;
        psrMesh = animation->path_lists == 1 ? AnimObjDispatchList(animation, m_bLOD, 0)
                                             : SelectCycleFrameLod(subcycle, m_bLOD);
    }
    if (!psrMesh || !psrMesh->getModel()) return;
    const auto matrix = psrMesh->worldMatrix();
    auto rotation = glm::dmat3(matrix);
    for (int column = 0; column < 3; ++column) {
        const auto length = glm::length(rotation[column]);
        if (length > 0) rotation[column] /= length;
    }
    for (auto& attachment : particles) {
        auto* particle = attachment.m_pstParticles.get();
        if (!particle->emitting) {
            continue;
        }
        auto offset = attachment.position;
        if (const auto mapped = findMappedVertex(psrMesh->getModel(), particle->attachment_key)) {
            const auto vertex = mapped->index < mapped->mesh->vertex_location_count
                                    ? mapped->index : 0;
            if (const auto sampled = psrMesh->sampledPosition(*mapped->mesh, vertex))
                offset = *sampled;
        }
        const auto placed = glm::dvec3(matrix * glm::dvec4(offset, 1));

        if (aim_set && particle->direction_mode == W8_PARTICLE_DIRECTION_CONE) {
            particle->transform.setRotation(glm::dvec3(m_axis), placed, 0.0);
        } else if (particle->direction_mode != W8_PARTICLE_DIRECTION_RANDOM) {
            particle->transform.setRotation(rotation * glm::dmat3(attachment.rotation));
        }
        particle->transform.setLocation(placed);
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
void CyclePlayer::SelectLOD(const glm::vec3* position)
{
    glm::vec3 delta = location - *position;
    float distance = glm::length(delta);
    bool has_lod_2 = frameModel(0, 2) != 0;
    bool has_lod_1 = frameModel(0, 1) != 0;
    bool has_lod_0 = frameModel(0, 0) != 0;

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
   callers borrow a concrete MeshInstance. */
MeshInstance* CyclePlayer::GetCurrentModelInstance()
{
    if (current_model_instance != 0) {
        return current_model_instance;
    }

    Animation* animation = active_animation;
    if (animation->path_lists == 1) {
        return AnimObjDispatchList(animation, m_bLOD, 0);
    }

    return SelectCycleFrameLod(subcycle, m_bLOD);
}

MeshInstance* CyclePlayer::SelectCycleFrameLod(signed char frame, signed char lod)
{
    if (lod < 0 || lod >= 3) {
        Fail("bLOD >= 0 && bLOD < NUM_LODS");
    }
    if (frame < 0) {
        Fail("bFrame >= 0");
    }
    if (m_pAI.has_value()) {
        PathAIPosition(&*m_pAI, &parent_location);
        location = parent_location;
    }
    return frameModel(frame, lod);
}

void CyclePlayer::SubmitTargetValue()
{
    GetAnimationRadius(&animation_radius);
}

void CyclePlayer::SetSubCycle(unsigned char new_subcycle)
{
    signed char count = frameCount();

    if (new_subcycle < count) {
        subcycle = new_subcycle;
    }
}

// FUNCTION: WIZ8 0x004a8460
void CyclePlayer::SetBehaviour(W8AnimationBehaviour bBehaviour)
{
    if (bBehaviour < BEHAVIOUR_FIRST || bBehaviour > BEHAVIOUR_LAST) {
        Fail("(bBehaviour >= BEHAVIOUR_FIRST) && (bBehaviour <= BEHAVIOUR_LAST)");
    }
    animation_behaviour = bBehaviour;
}

void ParticleAttachment::SelectCycle(signed char selected_cycle)
{
    if (cycle == selected_cycle) {
        m_pstParticles->SetActive(true);
        m_pstParticles->emission_count = 0;
    } else {
        m_pstParticles->SetActive(false);
    }
}

void CyclePlayer::DetachCycleLights(std::vector<std::unique_ptr<Light>>* lights)
{
    if (lights != 0) {
        for (const auto& owned : *lights) {
            auto* light = owned.get();

            light->bake_into_mesh = true;

            light->setScene(0);
        }
    }
}

void CyclePlayer::AttachCycleLights(std::vector<std::unique_ptr<Light>>* lights)
{
    if (owner_world && g_render_missile_lights && lights != 0) {
        for (const auto& owned : *lights) {
            auto* light = owned.get();

            light->bake_into_mesh = true;

            light->setScene(owner_world ? (owner_world->scene.get()) : nullptr);
        }
    }
}

void CyclePlayer::SetLights(std::vector<std::unique_ptr<Light>>* lights)
{
    m_plsLights = lights;
}

void CyclePlayer::AddShakeEffect(ShakeDefinition effect)
{
    shake_events.push_back(effect);
}

void CyclePlayer::CreateGroundShadow(float width, float depth)
{
    m_ground_shadow = std::make_unique<GroundShadow>(nullptr);
    m_ground_shadow->setName("Ground Shadow");
    m_ground_shadow->width = width;
    m_ground_shadow->depth = depth;
}

void CyclePlayer::SetGroundShadowVisible(bool visible)
{
    if (m_ground_shadow != 0) {
        if (visible) {
            m_ground_shadow->visible = true;
        } else {
            m_ground_shadow->visible = false;
        }
    }
}

signed char CyclePlayer::frameCount() const
{
    return active_animation ? static_cast<signed char>(AnimObjValue(active_animation, m_bLOD)) : 0;
}
MeshInstance* CyclePlayer::frameModel(signed char frame, signed char lod) const
{
    if (!active_animation) return nullptr;
    return active_animation->path_lists ? AnimObjDispatchList(active_animation, lod, 0)
                                        : AnimObjDispatch(active_animation, lod, frame);
}

#include "wiz8/entities/cycle_player.h"

#include <algorithm>
#include <cassert>
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
      timer(std::isfinite(definition.duration) ? definition.duration : 0.0f, TimerClock::World),
      key(key)
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

bool g_camera_shake_active;
glm::vec3 g_camera_shake_offset;

void UpdateShakeEffects()
{
    if (!g_camera_shake_enabled) {
        return;
    }
    const float intensity = CameraShakes().Update(cameraLocation());
    if (intensity > 0.0f) {
        const float amplitude = std::min(intensity, 1.0f) * 2.5e+02f;
        const int span = static_cast<int>(amplitude) << 1;
        g_camera_shake_offset.x = PresentationRng().below(span) - amplitude;
        g_camera_shake_offset.y = PresentationRng().below(span) - amplitude;
        g_camera_shake_offset.z = PresentationRng().below(span) - amplitude;
        g_camera_shake_active = true;
    } else {
        g_camera_shake_active = false;
    }
}

/* Build the two paths used while reading a .mon resource, verify its one-byte
   version, and hand the open file plus its resource context to the typed cycle
   reader. */
static bool readCyclePrefix(LevelLoadContext* info, CycleDefinition& cycle, int cycle_index)
try {
    unsigned char has_path;
    unsigned char has_particles;
    bool success = true;

    assert(info != 0);

    info->hFile->read(&has_path, 1);
    if (has_path != 0) {
        cycle.path = LoadPathAI(*info->hFile);
    }

    info->hFile->read(&has_particles, 1);
    if (has_particles != 0) {
        std::vector<std::unique_ptr<ParticleSystem>> particles;

        success = ReadWorldParticles(info, nullptr, &particles);
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

bool loadAnimationCycle(std::string_view mon_name, CycleDefinition& cycle, int cycle_index,
                        std::string_view directory,
                        const std::function<bool(LevelLoadContext&, int)>& read_payload,
                        std::string_view bitmap_directory)
try {
    LevelLoadContext info;
    unsigned char version;
    bool success;

    assert(!mon_name.empty());

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

    info.hFile = handle.get();
    info.bitmap_folder = wiz8::asset_path(bitmap_path);
    success = true;

    if ((handle->read(&version, 1).bytes == static_cast<std::size_t>(1)) == 0 || version != 1 ||
        !readCyclePrefix(&info, cycle, cycle_index) || !read_payload(info, cycle_index)) {
        success = false;
    }

    if (handle) handle->close();

    handle.reset();
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

CyclePlayer::CyclePlayer(const CycleDefinition& definition, World* owner) : CyclePlayer(owner)
{
    m_pAI = definition.path;
    if (m_pAI) m_pAI->timed = false;
    sound_events = definition.sound_events;
    shake_events = definition.shake_events;
    particles.reserve(definition.particles.size());
    for (const auto& source : definition.particles) {
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
    assert(method >= 1 && method <= 4);
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

        assert(behaviour >= BEHAVIOUR_FIRST && behaviour <= BEHAVIOUR_LAST);
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
    AnimationMesh* mesh = active_animation->mesh(m_bLOD, 0);

    assert(mesh != 0);
    *arg_radius = mesh->radius;
    return 1;
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

    if (animation->positioned_parts) {
        const auto count = animation->parts[m_bLOD].size();
        unsigned int index;

        for (index = 0; index < count; ++index) {
            KeyframeTrack* path = animation->partPath(m_bLOD, static_cast<signed char>(index));
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
    if (animation->positioned_parts) {
        const auto count = std::min<std::size_t>(animation->parts[m_bLOD].size(), models.size());
        for (std::size_t index = 0; index < count; ++index)
            models[index]->highlight_colour = colour;
    } else if (auto* model = GetCurrentModelInstance()) {
        model->highlight_colour = colour;
    }
}

/* Give a shown model this player's appearance. */
static void ApplyAppearance(const CyclePlayer& player, MeshInstance& model)
{
    model.highlight_colour = player.highlight_colour;
    model.skin = player.skin;
    model.damage_stage = player.damage_stage;
    model.light_scale = glm::vec3(player.light_scale);
    if (player.apply_instance_scale) {
        model.diffuse_scale_enabled = player.instance_scale != 1.0f;
        if (model.diffuse_scale_enabled) model.diffuse_scale = player.instance_scale;
    }
    model.emissive_override_enabled = player.emissive_override.has_value();
    model.emissive_override = player.emissive_override.value_or(0.0f);
}

void CyclePlayer::UpdateRepresentation(World* pWorld, const Navigator& nav)
{
    Animation* animation;
    glm::mat3 rot;
    glm::dvec3 loc;
    glm::vec3 vecPos;
    int index;

    assert(pWorld != 0);
    if (!rep_active) {
        return;
    }
    animation = active_animation;
    if (animation->positioned_parts) {
        int count = static_cast<int>(animation->parts[m_bLOD].size());

        for (index = 0; index < count; ++index) {
            const auto frame = animation->frame(m_bLOD, 0, static_cast<signed char>(index));
            KeyframeTrack* path;
            glm::mat3 current;

            assert(frame);
            MeshInstance* psrMesh = &model(index);
            ShowFrame(*psrMesh, frame);
            ApplyAppearance(*this, *psrMesh);
            psrMesh->visible = true;
            psrMesh->setScene((pWorld->scene.get()));
            path = animation->partPath(m_bLOD, static_cast<signed char>(index));
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
        hideModelsFrom(static_cast<std::size_t>(count));
    } else {
        MeshInstance* psrMesh = SelectCycleFrameLod(subcycle, m_bLOD);

        assert(psrMesh != 0);
        ApplyAppearance(*this, *psrMesh);
        if (model_scale) psrMesh->transform.setScale(glm::dvec3(*model_scale));
        hideModelsFrom(1);
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

        m_ground_shadow->setWorld(pWorld);
    }
    if (m_plsLights != 0) {
        int count = static_cast<int>(m_plsLights->size());
        glm::vec3 origin;

        if (animation->positioned_parts) {
            origin = GetLocation();
        } else {
            vecPos = nav.movement.position;
            origin = vecPos;
            origin.y += nav.movement.vertical_offset;
        }
        rot = GetRotation();
        for (index = 0; index < count; ++index) {
            Light* light = (*m_plsLights)[index].get();
            if (!animation->positioned_parts && current_model_instance) {
                // The light is a child of the visual. VSG composes it for
                // rendering; the bake query reads that same parentage.
                light->transform.setLocation(glm::dvec3(light->m_position));
                if (light->scene)
                    light->attachTo(*current_model_instance, {}, Light::Attachment::CycleOrigin);
            } else {
                const auto placed = rot * light->m_position + origin;
                light->transform.setLocation(glm::dvec3(placed));
            }
        }
    }
}

void CyclePlayer::hideModelsFrom(std::size_t first)
{
    for (std::size_t index = first; index < models.size(); ++index) {
        models[index]->visible = false;
        models[index]->setScene(nullptr);
    }
}

void CyclePlayer::DetachRepresentation(World* world)
{
    Animation* animation = active_animation;

    if (animation->positioned_parts) {
        hideModelsFrom(0);
    } else if (current_model_instance != 0) {
        assert(world != 0);
        current_model_instance->draw_parts = false;
        hideModelsFrom(0);
        current_model_instance = 0;
    }

    if (m_ground_shadow != 0) {

        m_ground_shadow->setWorld(nullptr);
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
        if (!active_animation) return;
        psrMesh = GetCurrentModelInstance();
    }
    if (!psrMesh || !psrMesh->getModel()) return;
    const auto matrix = psrMesh->worldMatrix();
    const auto rotation = psrMesh->worldRotation();
    for (auto& attachment : particles) {
        auto* particle = attachment.m_pstParticles.get();
        if (!particle->emitting) {
            continue;
        }
        auto offset = attachment.position;
        if (const auto mapped = findMappedVertex(psrMesh->getModel(), particle->attachment_key)) {
            const auto vertex =
                mapped->index < mapped->mesh->vertex_location_count ? mapped->index : 0;
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
    bool has_lod_2 = static_cast<bool>(frameModel(0, 2));
    bool has_lod_1 = static_cast<bool>(frameModel(0, 1));
    bool has_lod_0 = static_cast<bool>(frameModel(0, 0));

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

/* The model the representation draws: the one in the scene, or else the model
   showing the current frame (a path-list animation's first part). */
MeshInstance* CyclePlayer::GetCurrentModelInstance()
{
    if (current_model_instance != 0) {
        return current_model_instance;
    }

    Animation* animation = active_animation;
    if (animation->positioned_parts) {
        const auto frame = animation->frame(m_bLOD, 0, 0);
        if (!frame) return nullptr;
        ShowFrame(model(0), frame);
        return &model(0);
    }

    return SelectCycleFrameLod(subcycle, m_bLOD);
}

/* Show `frame` at `lod` on the model and return it. A missing frame leaves the
   model as it was and returns null. */
MeshInstance* CyclePlayer::SelectCycleFrameLod(signed char frame, signed char lod)
{
    assert(lod >= 0 && lod < 3);
    assert(frame >= 0);
    if (m_pAI.has_value()) {
        PathAIPosition(&*m_pAI, &parent_location);
        location = parent_location;
    }
    const auto shown = frameModel(frame, lod);
    if (!shown) return nullptr;
    ShowFrame(model(0), shown);
    return &model(0);
}

MeshInstance& CyclePlayer::model(std::size_t index)
{
    while (models.size() <= index) models.push_back(std::make_unique<MeshInstance>());
    return *models[index];
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
    assert(bBehaviour >= BEHAVIOUR_FIRST && bBehaviour <= BEHAVIOUR_LAST);
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

void CyclePlayer::CreateGroundShadow(float width, float depth)
{
    m_ground_shadow = std::make_unique<GroundShadow>();
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
    return active_animation ? static_cast<signed char>(active_animation->frameCount(m_bLOD)) : 0;
}
AnimationFrame CyclePlayer::frameModel(signed char frame, signed char lod) const
{
    if (!active_animation) return {};
    return active_animation->frame(lod, frame);
}

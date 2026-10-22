#include "wiz8/render/particle_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/PipelineLayout.h>

#include "effect_geometry.h"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "wiz8/chunk.h"
#include "wiz8/entities/monster.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/math.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/world.h"

constexpr float kParticleFlutterAngleRandomScale = 0.00019174758926965296f;
constexpr float kParticleFlutterDisplacementScale = -0.0010000000474974513f;
constexpr float kParticleFlutterVelocityFloor = -1000.0f;
constexpr float kParticleRandomUnitScale = 3.0517578125e-05f;

struct ParticleRenderData {
    struct Draw {
        vsg::ref_ptr<render::DrawNode> node;
        vsg::ref_ptr<vsg::ImageInfo> image;
        std::uint32_t shader = ~std::uint32_t{0};
    };
    vsg::ref_ptr<vsg::PipelineLayout> layout;
    vsg::ref_ptr<vsg::Group> particles = vsg::Group::create();
    vsg::ref_ptr<vsg::CullNode> cull;
    render::EffectGeometry geometry;
    std::vector<Draw> draws;
    std::uint64_t frame_number = ~std::uint64_t{0};

    ParticleRenderData(render::RenderContext& context, const ParticleSystem& source)
        : layout(context.layout),
          geometry(context, static_cast<unsigned int>(source.particles.size()) * 4, source.material,
                   render::HasAlpha | render::HasTexcoord0 | render::ParticleBillboard, false, 0)
    {
        cull = vsg::CullNode::create(vsg::dsphere(), particles);
        static constexpr std::array<glm::vec2, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (std::uint32_t particle = 0; particle < source.particles.size(); ++particle) {
            const auto first = particle * 4;
            for (std::uint32_t corner = 0; corner < 4; ++corner)
                geometry.attributes[0]->at(first + corner) =
                    vsg::vec4(uv[corner].x, uv[corner].y, 0, 0);
            auto indices = vsg::uintArray::create(6);
            const std::array values{first, first + 1, first + 2, first + 2, first + 3, first};
            std::ranges::copy(values, indices->begin());
            auto draw = geometry.draw(indices, source.requires_sorted_renderer != 0);
            draw->draw->indexCount = 0;
            particles->addChild(draw);
            draws.push_back({std::move(draw), {}, ~std::uint32_t{0}});
        }
    }
};

void ParticleSystem::SetRenderFlags(DrawShader flags)
{
    render_flags = flags;
}

void ParticleSystem::SetParticleScale(float scale)
{
    float inverse_scale = 1.0f / size_scale;
    // Retail uses the extent as its offset, not the midpoint of the bounds.
    const auto offset = lifetime_maximum - lifetime_minimum;
    lifetime_minimum = (lifetime_minimum - offset) * inverse_scale + offset;
    lifetime_maximum = (lifetime_maximum - offset) * inverse_scale + offset;
    size_scale = scale;
    lifetime_minimum = (lifetime_minimum - offset) * scale + offset;
    lifetime_maximum = (lifetime_maximum - offset) * scale + offset;
}

void SaveParticleStates(wiz8::File& handle)
{
    unsigned char version = 1;
    int count = 0;

    handle.write(&version, sizeof(version));

    if (GetWorld() != 0) {
        for (const auto& particle : GetWorld()->particles) {
            count += particle->persisted;
        }
    }
    handle.write(&count, sizeof(count));

    if (GetWorld() != 0) {
        for (const auto& particle : GetWorld()->particles) {
            if (particle->persisted) {
                handle.write_padded(particle->getName(), 0x80);
                handle.write(&particle->emitting, sizeof(particle->emitting));
            }
        }
    }
}

std::vector<W8ParticleState> ReadParticleStates(W8Chunk& file)
{
    unsigned char version;
    int count;
    file.ReadBytes(&version, sizeof(version));
    file.ReadBytes(&count, sizeof(count));
    if (count < 0 || count > file.RemainingBytes() / 129)
        throw std::runtime_error("invalid Particle state count");
    std::vector<W8ParticleState> records;
    records.reserve(count);
    for (int index = 0; index < count; ++index) {
        char name[128];
        unsigned char active;
        file.ReadBytes(name, sizeof(name));
        file.ReadBytes(&active, sizeof(active));
        const auto end = std::find(name, name + sizeof(name), '\0');
        if (end == name + sizeof(name)) throw std::runtime_error("unterminated Particle name");
        records.push_back({std::string(name, end), active});
    }
    return records;
}

void ApplyParticleStates(World& world, std::span<const W8ParticleState> states)
{
    for (const auto& state : states) {
        const auto found =
            std::find_if(world.particles.rbegin(), world.particles.rend(),
                         [&](const auto& particle) { return particle->name == state.name; });
        if (found != world.particles.rend()) (*found)->SetActive(state.active);
    }
}

ParticleSystem::~ParticleSystem()
{
    setScene(nullptr);
}

void ParticleSystem::CancelAnimationCallback()
{
    callback.reset();
}

ParticleSystem::ParticleSystem(WorldScene* scene, int count)
{
    setScene(scene);
    if (count == 0) {
        return;
    }
    assert(count >= 0 && count < 10000);
    particles.resize(count);
    last_integration_tick = GameClockMilliseconds();
    last_emission_tick = last_integration_tick;
}

/* A copy shares material and static texture, but starts with no live
   particles, fresh animation frames and fresh timing. */
ParticleSystem::ParticleSystem(const ParticleSystem& other)
{
    persisted = other.persisted;
    start_frame = other.start_frame;
    end_frame = other.end_frame;
    emission_gap = other.emission_gap;
    size_scale = other.size_scale;
    if (other.particles.empty()) {
        return;
    }

    setScene(nullptr);
    transform = other.transform;
    name = other.name;
    visible = other.visible;
    setName(other.isNamed() ? other.getName() : std::string{});
    particles.resize(other.particles.size());
    requires_sorted_renderer = other.requires_sorted_renderer;
    particle_size = other.particle_size;
    material = other.material;
    render_flags = other.render_flags;
    SetTexture(other.texture);
    emission_limit = other.emission_limit;
    release_when_done = other.release_when_done;
    replace_when_full = other.replace_when_full;
    emitting = other.emitting;
    bounds_mode = other.bounds_mode;
    has_acceleration = other.has_acceleration;
    expiry_mode = other.expiry_mode;
    emission_mode = other.emission_mode;
    los_check_enabled = other.los_check_enabled;
    direction_mode = other.direction_mode;
    speed_mode = other.speed_mode;
    camera_relative = other.camera_relative;
    flutter_amplitude = other.flutter_amplitude;
    flutter_period = other.flutter_period;
    SetFlutter(other.flutter_mode);
    emission_interval = other.emission_interval;
    lifetime_ms = other.lifetime_ms;
    emission_minimum = other.emission_minimum;
    emission_maximum = other.emission_maximum;
    direction = other.direction;
    acceleration = other.acceleration;
    cone_yaw = other.cone_yaw;
    cone_pitch = other.cone_pitch;
    initial_speed = other.initial_speed;
    speed_min = other.speed_min;
    speed_max = other.speed_max;
    lifetime_minimum = other.lifetime_minimum;
    lifetime_maximum = other.lifetime_maximum;
    bounds_origin = other.bounds_origin;
    bounds_radius = other.bounds_radius;
    last_integration_tick = GameClockMilliseconds();
    last_emission_tick = last_integration_tick;
    attachment_key = other.attachment_key;

    this->transform.setLocation(other.transform.getLocation());
    auto rotation = other.transform.getRotation<glm::mat3>();
    this->transform.setRotation(rotation);

    auto source_world = other.transform.matrix<glm::mat4>();
    const glm::dmat4 world(source_world);
    this->transform.setMatrix(world);
}

std::optional<unsigned int> ParticleSystem::activateParticle(bool replaceWhenFull)
{
    if (particles.empty() || (emission_limit != 0 && emission_count >= emission_limit)) {
        return std::nullopt;
    }

    auto slot = std::ranges::find(particles, false, &Particle::active);
    if (slot == particles.end()) {
        if (!replaceWhenFull) return std::nullopt;
        // Keep the retail comparison of absolute unsigned birth ticks and first tie.
        slot = std::ranges::min_element(particles, {}, &Particle::birth_tick);
    }
    const auto index = static_cast<unsigned int>(slot - particles.begin());

    slot->active = true;
    slot->birth_tick = GameClockMilliseconds();

    float magnitude = 0.0f;
    if (speed_mode == W8_PARTICLE_SPEED_FIXED) {
        magnitude = initial_speed;
    } else if (speed_mode == W8_PARTICLE_SPEED_RANDOM) {
        magnitude = (speed_max - speed_min) * PresentationRng().next() * kParticleRandomUnitScale +
                    speed_min;
    }
    magnitude *= size_scale;

    glm::vec3& velocity = slot->velocity;
    switch (direction_mode) {
    case W8_PARTICLE_DIRECTION_NODE_FORWARD: {
        velocity = glm::vec3(this->transform.forward() * double(magnitude));
        break;
    }

    case W8_PARTICLE_DIRECTION_FIXED:
        velocity = this->direction * magnitude;
        break;

    case W8_PARTICLE_DIRECTION_CONE: {
        glm::vec3 direction(0, 0, magnitude);

        double angle = (PresentationRng().next() * kParticleRandomUnitScale - 0.5f) * cone_pitch;
        direction = glm::vec3(glm::rotateX(glm::dvec3(direction), angle));

        angle = (PresentationRng().next() * kParticleRandomUnitScale - 0.5f) * cone_yaw;
        direction = glm::vec3(glm::rotateY(glm::dvec3(direction), angle));

        const auto rotation = this->transform.orientation<glm::mat3>();
        velocity = rotation * direction;
        break;
    }

    case W8_PARTICLE_DIRECTION_RANDOM: {
        glm::vec3 direction;
        direction.x = PresentationRng().next() * kParticleRandomUnitScale - 0.5f;
        direction.y = PresentationRng().next() * kParticleRandomUnitScale - 0.5f;
        direction.z = PresentationRng().next() * kParticleRandomUnitScale - 0.5f;

        direction = normalizedOrZero(direction);

        velocity = direction * magnitude;
        break;
    }

    default:
        velocity = glm::vec3(0);
        break;
    }

    slot->position = glm::vec3(this->transform.getLocation());

    if (flutter_mode != W8_PARTICLE_FLUTTER_NONE) {
        slot->flutter_angle = PresentationRng().next() * kParticleFlutterAngleRandomScale;
    }
    if (slot->animation) {
        // A pool slot may have been inactive for a long time. Restart its
        // selected animation chain at this birth, including nested IFL frames.
        const auto now = GameInProgress() ? CurrentGame().time : std::chrono::milliseconds{};
        auto* animation = slot->animation.get();
        while (animation && animation->playback) {
            animation->SetFrame(0);
            auto& playback = animation->playback.value();
            playback.last_update = now;
            animation = playback.textures.empty() ? nullptr : playback.textures.front().get();
        }
    }

    slot->alpha = 1.0f;
    ++emission_count;
    return index;
}

bool ParticleSystem::hasActiveParticles() const
{
    return std::ranges::any_of(particles, &Particle::active);
}

void ParticleSystem::Update()
{
    unsigned int now = GameClockMilliseconds();
    if (now - last_emitted_at < emission_gap) {
        return;
    }

    last_emitted_at = now;
    if (hasActiveParticles()) {
        unsigned int elapsed_ticks = now - last_integration_tick;

        glm::mat3 rotation;
        rotation = this->transform.getRotation<glm::mat3>();

        glm::mat4 transform(rotation);
        transform = glm::inverse(transform);

        const auto node_location = glm::vec3(this->transform.getLocation());

        double elapsed = static_cast<double>(elapsed_ticks);
        const auto acceleration_step = glm::vec3(glm::dvec3(acceleration) * (elapsed * 0.001));

        for (auto& particle : particles) {
            if (!particle.active) {
                continue;
            }

            if (expiry_mode == W8_PARTICLE_EXPIRY_TIMED) {
                unsigned int expires_at = particle.birth_tick + lifetime_ms;
                if (expires_at < now) {
                    particle.active = false;
                    continue;
                }
                if (expires_at - 500 < now) {
                    float alpha = (expires_at - now) * 0.002f;
                    particle.alpha = alpha;
                }
            } else if (expiry_mode == W8_PARTICLE_EXPIRY_TEXTURE) {
                if (!particle.animation) {
                    expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
                    if (lifetime_ms == 0) {
                        lifetime_ms = 1000;
                    }
                } else {
                    auto* animation = particle.animation.get();
                    if (animation->IsFinished() != 0) {
                        particle.active = false;
                        continue;
                    }
                }
            }

            if (has_acceleration) {
                particle.velocity += acceleration_step;
            }

            const auto movement = glm::vec3(glm::dvec3(particle.velocity) * (elapsed * 0.001));
            auto candidate = particle.position + movement;

            if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
                const auto center = rotation * bounds_origin + node_location;
                if (size_scale * bounds_radius < glm::length(candidate - center)) {
                    particle.active = false;
                    continue;
                }
            } else if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
                const auto local = glm::vec3(transform * glm::vec4(candidate - node_location, 1));
                if (!BoundingBox{lifetime_minimum, lifetime_maximum}.contains(local)) {
                    particle.active = false;
                    continue;
                }
            }

            if (los_check_enabled) {
                // The octree trace still accepts packed endpoint records.
                const glm::vec3 from(particle.position);
                glm::vec3 to(candidate);
                if (GetWorld()->compiledIndex() == nullptr ||
                    !GetWorld()->compiledIndex()->hasLineOfSight(from, to)) {
                    particle.active = false;
                    continue;
                }
                candidate = to;
            }

            particle.position = candidate;
        }
    }

    last_integration_tick = now;

    if (!emitting || emission_mode == W8_PARTICLE_EMISSION_NONE) {
        return;
    }

    unsigned int emission_elapsed = now - last_emission_tick;
    if (emission_elapsed < emission_interval) {
        return;
    }

    if (emission_mode == W8_PARTICLE_EMISSION_SINGLE) {
        activateParticle(replace_when_full);
        last_emission_tick = now;
        return;
    }
    if (emission_mode != W8_PARTICLE_EMISSION_CATCH_UP || emission_elapsed <= emission_interval) {
        return;
    }

    for (;;) {
        unsigned int lag = now - emission_interval - last_emission_tick;
        const auto activated = activateParticle(replace_when_full);
        if (!activated) {
            last_emission_tick = now;
            return;
        }

        const auto particle_index = *activated;
        if (has_acceleration) {
            const auto scaled = glm::vec3(glm::dvec3(acceleration) * double(lag));
            particles[particle_index].velocity += glm::vec3(glm::dvec3(scaled) / 1000.0);
        }
        const auto scaled = glm::vec3(glm::dvec3(particles[particle_index].velocity) * double(lag));
        const auto displacement = glm::vec3(glm::dvec3(scaled) / 1000.0);
        particles[particle_index].position = emissionPosition() + displacement;

        last_emission_tick += emission_interval;
        if (now - last_emission_tick <= emission_interval) {
            return;
        }
    }
}

glm::vec3 ParticleSystem::emissionPosition()
{
    // Braced initialization preserves the x, y, z random draw order.
    const glm::vec3 random{float(PresentationRng().next()), float(PresentationRng().next()),
                           float(PresentationRng().next())};
    const auto offset = ((emission_maximum - emission_minimum) * random * kParticleRandomUnitScale +
                         emission_minimum) *
                        size_scale;
    return this->transform.getRotation<glm::mat3>() * offset +
           glm::vec3(this->transform.getLocation());
}

/* Traversal is gated separately from particle activity.  Starting a new
   enabled interval resets the update timestamp; repeated enables do not. */
void ParticleSystem::SetTraversalEnabled(bool enabled)
{
    if (enabled && !traversal_enabled) {
        last_emission_tick = GameClockMilliseconds();
    }
    traversal_enabled = enabled;
}

/* Keep the old activation gate and emission/integration/completion order in
   the game update. Frustum visibility only affects drawing: an enabled system
   still expires and restores its owner's animation when it is offscreen. */
void ParticleSystem::AdvanceSimulation(std::chrono::milliseconds game_time)
{
    if (finished || !visible || !traversal_enabled || (!emitting && !hasActiveParticles())) {
        return;
    }
    if (camera_relative) {
        transform.setLocation(glm::dvec3(cameraLocation() + camera_offset));
    }

    // An IFL frame can itself be animated. Resolve the selected chain here
    // so every view later consumes the same complete playback snapshot.
    for (auto& particle : particles)
        if (particle.active && particle.animation)
            particle.animation->UpdateFrame(game_time, TextureClock::Game);
    Update();

    if (emission_limit != 0 && emission_count >= emission_limit) {
        emitting = false;
    }

    if (!emitting && !hasActiveParticles()) {
        if (auto completed = std::move(callback)) {
            completed->RestoreAnimation();
        }
        finished = release_when_done;
        return;
    }
}

vsg::ref_ptr<vsg::Node> ParticleSystem::renderNode() const
{
    return root;
}

void ParticleSystem::updateRenderNode(render::RenderContext& context, const SceneFrame& frame,
                                      std::uint64_t frame_number)
{
    // A new context rebuilds the resources under the same scene member.
    if (!render_data || render_data->layout != context.layout) {
        render_data = std::make_unique<ParticleRenderData>(context, *this);
        root->children.clear();
        root->addChild(render_data->cull);
    }
    auto& data = *render_data;
    if (data.frame_number == frame_number) return;
    data.frame_number = frame_number;

    const auto position = glm::vec3(transform.getLocation());
    const auto rotation = transform.getRotation<glm::mat3>();
    glm::vec3 center = position;
    float radius = 0;
    if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
        center += rotation * bounds_origin;
        radius = size_scale * bounds_radius;
    } else if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
        glm::mat4 world(rotation);
        world[3] = glm::vec4(position, 1);
        const auto bounds = BoundingBox{lifetime_minimum, lifetime_maximum}.transformed(world);
        center = bounds.center();
        radius = glm::length(bounds.maximum - center);
    } else {
        for (unsigned index = 0; index < particles.size(); ++index)
            if (particles[index].active)
                radius = std::max(radius, glm::length(particles[index].position - center));
        radius += float(particle_size) * size_scale;
    }
    data.cull->bound = vsg::dsphere(center.x, center.y, center.z, radius);
    render::InstanceBlock instance;
    if (scene) instance.ambient = glm::vec4(scene->ambient_light, 0);
    instance.bounds = glm::vec4(center, radius);
    render::store(*data.geometry.instance, instance);
    render::store(*data.geometry.pass, render::packPass(std::array{material},
                                                        render::HasAlpha | render::HasTexcoord0 |
                                                            render::ParticleBillboard,
                                                        false, frame.time));

    float phase = 0;
    if (flutter_mode != W8_PARTICLE_FLUTTER_NONE && flutter_period != 0)
        phase = float(GameClockMilliseconds() % flutter_period) / int(flutter_period) *
                kCameraAnglePeriod;
    const float flutter = float(std::sin(phase)) * flutter_amplitude * size_scale;
    const bool enabled = visible && traversal_enabled && !finished;
    bool positions_changed = false;
    bool metadata_changed = false;
    for (unsigned index = 0; index < particles.size(); ++index) {
        auto& state = data.draws[index];
        auto& draw = *state.node;
        draw.draw->indexCount = enabled && particles[index].active ? 6 : 0;
        draw.sorted = requires_sorted_renderer != 0;
        if (!draw.draw->indexCount) continue;
        auto center = particles[index].position;
        if (flutter_mode != W8_PARTICLE_FLUTTER_NONE && particles[index].velocity.y < 0) {
            float amount = flutter;
            if (flutter_mode == W8_PARTICLE_FLUTTER_VELOCITY_SCALED)
                amount *= std::max(kParticleFlutterVelocityFloor, particles[index].velocity.y) *
                          kParticleFlutterDisplacementScale;
            center += glm::vec3(
                glm::rotateY(glm::dvec3(amount, 0, 0), double(particles[index].flutter_angle)));
        }
        for (unsigned corner = 0; corner < 4; ++corner) {
            const auto vertex = index * 4 + corner;
            // All four centers have the billboard's actual eye-space depth,
            // which is also the key used by global triangle sorting.
            auto& position = data.geometry.positions->at(vertex);
            const vsg::vec3 next_position(center.x, center.y, center.z);
            if (position != next_position) {
                position = next_position;
                positions_changed = true;
            }
            auto& metadata = data.geometry.attributes[4]->at(vertex);
            const vsg::vec4 next_metadata(0, 1, particles[index].alpha,
                                          float(particle_size) * size_scale);
            if (metadata != next_metadata) {
                metadata = next_metadata;
                metadata_changed = true;
            }
        }
        auto* image = particles[index].animation ? particles[index].animation.get() : texture.get();
        if (image) image = image->currentFrame();
        auto current_image = context.texture(image, frame.texture_reduction);
        if (state.image != current_image || state.shader != render_flags.value) {
            state.image = current_image;
            state.shader = render_flags.value;
            draw.stateCommands = {context.pipeline(render_flags.value, FaceCull::None),
                                  data.geometry.instance_bind, data.geometry.pass_bind,
                                  context.batchSet(render_flags.value, current_image)};
        }
    }
    if (positions_changed) data.geometry.positions->dirty();
    if (metadata_changed) data.geometry.attributes[4]->dirty();
}

void ParticleSystem::SetTexture(std::shared_ptr<Texture> texture)
{
    for (auto& particle : particles) particle.animation.reset();
    if (texture && texture->playback) {
        for (auto& particle : particles) {
            particle.animation = std::make_shared<Texture>(*texture);
            particle.animation->setAnimationClock(TextureClock::Game);
        }
    }
    this->texture = std::move(texture);
}

// FUNCTION: WIZ8 0x0049acd0
void ParticleSystem::SetActive(bool active)
{
    if (active && !emitting) {
        unsigned int now = GameClockMilliseconds();
        last_integration_tick = now;
        last_emission_tick = now;
    }
    emitting = active;
}

bool ParticleSystem::ReplaceTexture(std::string_view old_name, std::shared_ptr<Texture> replacement)
{
    if (texture && (!texture->source_path.empty() || texture->playback) &&
        CompareTextIgnoreAsciiCase(texture->getName(), old_name) == 0) {
        SetTexture(std::move(replacement));
        return true;
    }
    return false;
}

void ParticleSystem::SetMaterial(std::shared_ptr<Material> new_material)
{
    material = std::move(new_material);
}

void ParticleSystem::SetFlutter(W8ParticleFlutterMode mode)
{
    if (flutter_mode == W8_PARTICLE_FLUTTER_NONE && mode != W8_PARTICLE_FLUTTER_NONE)
        for (auto& particle : particles) particle.flutter_angle = 0;
    flutter_mode = mode;
}

// A particle still emitting when its cycle dies finishes under its world's care.
void RetireParticle(World& world, std::unique_ptr<ParticleSystem> particle)
{
    particle->release_when_done = true;
    world.retired_particles.push_back(std::move(particle));
}

void AdvanceWorldParticles(World& world, std::chrono::milliseconds game_time)
{
    // Completion can change a monster's animation. Newly attached systems wait
    // for the next step, just as they did in the scene's submission snapshot.
    const auto particles = world.scene->particles;
    for (auto* particle : particles) particle->AdvanceSimulation(game_time);
}

void SweepFinishedParticles(World* world)
{
    std::erase_if(world->retired_particles,
                  [](const auto& particle) { return particle->finished; });
}

#include "wiz8/render/particle_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>

#include "compat/compiler.h"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "wiz8/checked_integer.h"
#include "wiz8/chunk.h"
#include "wiz8/entities/monster.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/math.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_submission.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/world.h"

constexpr float kParticleFlutterAngleRandomScale = 0.00019174758926965296f;
constexpr float kParticleFlutterDisplacementScale = -0.0010000000474974513f;
constexpr float kParticleFlutterVelocityFloor = -1000.0f;
constexpr float kParticleRandomUnitScale = 3.0517578125e-05f;

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

void ParticleSystem::AllocateParticles(unsigned int count)
{
    if (count >= 10000) {
        Fail("cnt < 10000");
    }
    particle_count = count;
    particle_positions.assign(count, glm::vec3(0));
    velocities.assign(count, glm::vec3(0));
    birth_ticks.resize(count);
    particle_active.assign(count, false);
    active_triangles.resize(count * 2);
    vertex_positions.resize(count * 4);
    texcoords.resize(count * 4);
    alphas.assign(count * 4, 1.0f);
    triangles.resize(count * 2);
    for (unsigned int index = 0; index < count; ++index) {
        int vertex = static_cast<int>(index * 4);
        triangles[index * 2] = glm::ivec3{vertex, vertex + 1, vertex + 2};
        triangles[index * 2 + 1] = glm::ivec3{vertex + 2, vertex + 3, vertex};
        texcoords[vertex] = glm::vec2(0.0f, 0.0f);
        texcoords[vertex + 1] = glm::vec2(1.0f, 0.0f);
        texcoords[vertex + 2] = glm::vec2(1.0f, 1.0f);
        texcoords[vertex + 3] = glm::vec2(0.0f, 1.0f);
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
    AllocateParticles(count);
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
    if (other.particle_count == 0) {
        return;
    }

    setScene(nullptr);
    transform = other.transform;
    name = other.name;
    visible = other.visible;
    setName(other.isNamed() ? other.getName() : std::string{});
    AllocateParticles(other.particle_count);
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
    update_flags = W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
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
    if (particle_count == 0 || (emission_limit != 0 && emission_count >= emission_limit)) {
        return std::nullopt;
    }

    unsigned int index;
    for (index = 0; index < particle_count; ++index) {
        if (particle_active[index] == 0) {
            break;
        }
    }

    if (index == particle_count) {
        if (!replaceWhenFull) {
            return std::nullopt;
        }

        unsigned int oldest = 0;
        unsigned int candidate;
        for (candidate = 0; candidate < particle_count; ++candidate) {
            if (birth_ticks[candidate] < birth_ticks[oldest] && particle_active[candidate] != 0) {
                oldest = candidate;
            }
        }
        DeactivateParticle(oldest);
        index = oldest;
    }

    particle_active[index] = 1;
    birth_ticks[index] = GameClockMilliseconds();

    float magnitude = 0.0f;
    if (speed_mode == W8_PARTICLE_SPEED_FIXED) {
        magnitude = initial_speed;
    } else if (speed_mode == W8_PARTICLE_SPEED_RANDOM) {
        magnitude = (speed_max - speed_min) * Random15() * kParticleRandomUnitScale + speed_min;
    }
    magnitude *= size_scale;

    glm::vec3& velocity = velocities[index];
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

        double angle = (Random15() * kParticleRandomUnitScale - 0.5f) * cone_pitch;
        direction = glm::vec3(glm::rotateX(glm::dvec3(direction), angle));

        angle = (Random15() * kParticleRandomUnitScale - 0.5f) * cone_yaw;
        direction = glm::vec3(glm::rotateY(glm::dvec3(direction), angle));

        const auto rotation = this->transform.orientation<glm::mat3>();
        velocity = rotation * direction;
        break;
    }

    case W8_PARTICLE_DIRECTION_RANDOM: {
        glm::vec3 direction;
        direction.x = Random15() * kParticleRandomUnitScale - 0.5f;
        direction.y = Random15() * kParticleRandomUnitScale - 0.5f;
        direction.z = Random15() * kParticleRandomUnitScale - 0.5f;

        direction = normalizedOrZero(direction);

        velocity = direction * magnitude;
        break;
    }

    default:
        velocity = glm::vec3(0);
        break;
    }

    particle_positions[index] = glm::vec3(this->transform.getLocation());

    if (!flutter_angles.empty()) {
        flutter_angles[index] = Random15() * kParticleFlutterAngleRandomScale;
    }
    if (!texture_frames.empty()) {
        static_cast<Texture*>(texture_frames[index * 2].get())->SetFrame(0);
    }

    std::fill_n(alphas.begin() + index * 4, 4, 1.0f);

    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
    ++emission_count;
    ++active_particle_count;
    return index;
}

void ParticleSystem::DeactivateParticle(unsigned int index)
{
    if (particle_active[index]) {
        particle_active[index] = false;
        update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
        --active_particle_count;
    }
}

void ParticleSystem::Update()
{
    unsigned int now = GameClockMilliseconds();
    if (now - last_emitted_at < emission_gap) {
        return;
    }

    last_emitted_at = now;
    if (active_particle_count != 0) {
        unsigned int elapsed_ticks = now - last_integration_tick;

        glm::mat3 rotation;
        rotation = this->transform.getRotation<glm::mat3>();

        glm::mat4 transform(rotation);
        transform = glm::inverse(transform);

        const auto node_location = glm::vec3(this->transform.getLocation());

        double elapsed = static_cast<double>(elapsed_ticks);
        const auto acceleration_step = glm::vec3(glm::dvec3(acceleration) * (elapsed * 0.001));

        unsigned int index;
        for (index = 0; index < particle_count; ++index) {
            if (particle_active[index] == 0) {
                continue;
            }

            unsigned int vertex = index * 4;
            if (expiry_mode == W8_PARTICLE_EXPIRY_TIMED) {
                unsigned int expires_at = birth_ticks[index] + lifetime_ms;
                if (expires_at < now) {
                    DeactivateParticle(index);
                    continue;
                }
                if (expires_at - 500 < now) {
                    float alpha = (expires_at - now) * 0.002f;
                    std::fill_n(alphas.begin() + vertex, 4, alpha);
                }
            } else if (expiry_mode == W8_PARTICLE_EXPIRY_TEXTURE) {
                if (texture_frames.empty()) {
                    expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
                    if (lifetime_ms == 0) {
                        lifetime_ms = 1000;
                    }
                } else {
                    auto* animation = static_cast<Texture*>(texture_frames[index * 2].get());
                    animation->UpdateFrame();
                    if (animation->IsFinished() != 0) {
                        DeactivateParticle(index);
                        continue;
                    }
                }
            }

            if (has_acceleration) {
                velocities[index] += acceleration_step;
            }

            const auto movement = glm::vec3(glm::dvec3(velocities[index]) * (elapsed * 0.001));
            auto candidate = particle_positions[index] + movement;

            if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
                const auto center = rotation * bounds_origin + node_location;
                if (size_scale * bounds_radius < glm::length(candidate - center)) {
                    DeactivateParticle(index);
                    continue;
                }
            } else if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
                const auto local = glm::vec3(transform * glm::vec4(candidate - node_location, 1));
                if (!BoundingBox{lifetime_minimum, lifetime_maximum}.contains(local)) {
                    DeactivateParticle(index);
                    continue;
                }
            }

            if (los_check_enabled) {
                // The octree trace still accepts packed endpoint records.
                const glm::vec3 from(particle_positions[index]);
                glm::vec3 to(candidate);
                if (GetWorld()->compiledIndex() == nullptr ||
                    !GetWorld()->compiledIndex()->HasLineOfSight(&from, &to, true)) {
                    DeactivateParticle(index);
                    continue;
                }
                candidate = to;
            }

            particle_positions[index] = candidate;
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
            velocities[particle_index] += glm::vec3(glm::dvec3(scaled) / 1000.0);
        }
        const auto scaled = glm::vec3(glm::dvec3(velocities[particle_index]) * double(lag));
        const auto displacement = glm::vec3(glm::dvec3(scaled) / 1000.0);
        particle_positions[particle_index] = emissionPosition() + displacement;

        last_emission_tick += emission_interval;
        if (now - last_emission_tick <= emission_interval) {
            return;
        }
    }
}

glm::vec3 ParticleSystem::emissionPosition()
{
    // Braced initialization preserves the x, y, z random draw order.
    const glm::vec3 random{float(Random15()), float(Random15()), float(Random15())};
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

/* Build the four camera-facing offsets once per call, then expand every
   particle center into a quad. */
void ParticleSystem::PrepareRenderer(const glm::mat4& view)
{
    static const std::array<glm::vec3, 4> corners{
        {{-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}, {-0.5f, -0.5f, 0}}};
    auto basis = glm::mat3(view);
    const float normalization = float(1.0 / glm::length(glm::vec3(glm::row(view, 0))));
    basis *= normalization;
    const float scale = float(particle_size) * size_scale;
    std::array<glm::vec3, 4> offsets;
    for (unsigned corner = 0; corner < corners.size(); ++corner)
        offsets[corner] = (basis * corners[corner]) * scale;

    float phase = 0;
    if (flutter_mode != W8_PARTICLE_FLUTTER_NONE && flutter_period != 0)
        phase = float(GameClockMilliseconds() % flutter_period) / int(flutter_period) *
                kCameraAnglePeriod;
    const float flutter = float(std::sin(phase)) * flutter_amplitude * size_scale;
    for (unsigned index = 0; index < particle_count; ++index) {
        auto position = particle_positions[index];
        if (flutter_mode != W8_PARTICLE_FLUTTER_NONE && velocities[index].y < 0) {
            float amount = flutter;
            if (flutter_mode == W8_PARTICLE_FLUTTER_VELOCITY_SCALED)
                amount *= std::max(kParticleFlutterVelocityFloor, velocities[index].y) *
                          kParticleFlutterDisplacementScale;
            position +=
                glm::vec3(glm::rotateY(glm::dvec3(amount, 0, 0), double(flutter_angles[index])));
        }
        for (unsigned corner = 0; corner < corners.size(); ++corner)
            vertex_positions[index * 4 + corner] = position + offsets[corner];
    }
}

/* One particle system's complete submission. The system is placed (either
   relative to the camera or at its own node location), aged, stopped after
   its configured total activation count, culled against the renderer, and
   finally handed to the shared triangle-mesh pipeline as a single slot.

   The retired path is the only one that can drop the system: a particle whose
   activity has run out notifies its shake callback and, when release_when_done marks
   it as self-owned, marks itself finished for its owner to destroy. */
void ParticleSystem::SubmitToRenderer(GpuRenderer* renderer)
{
    glm::vec3 position;

    if (finished) {
        return;
    }
    if (camera_relative) {
        position = cameraLocation() + camera_offset;

        this->transform.setLocation(glm::dvec3(position));
    } else {
        position = glm::vec3(this->transform.getLocation());
    }

    Update();

    if (emission_limit != 0 && emission_count >= emission_limit) {
        emitting = false;
    }

    if (!emitting && active_particle_count == 0) {
        if (auto completed = std::move(callback)) {
            completed->RestoreAnimation();
        }
        finished = release_when_done;
        return;
    }

    glm::mat3 rotation;

    if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
        GpuRenderer::e_visibility visibility;

        const auto& extent = bounds_origin;

        if (extent.x == 0.0f && extent.y == 0.0f && extent.z == 0.0f) {
            visibility = renderer->testBoundingSphere(position, size_scale * bounds_radius);
        } else {
            rotation = this->transform.getRotation<glm::mat3>();
            glm::vec3 center = rotation * extent + position;
            visibility = renderer->testBoundingSphere(center, size_scale * bounds_radius);
        }
        if (visibility == GpuRenderer::VISIBILITY_OUTSIDE) {
            return;
        }
    }

    if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
        rotation = this->transform.getRotation<glm::mat3>();
        glm::mat4 world(rotation);
        world[3] = glm::vec4(position, 1);
        const auto bounds = BoundingBox{lifetime_minimum, lifetime_maximum}.transformed(world);
        const auto visibility = renderer->testBoundingBox(bounds.minimum, bounds.maximum);
        if (visibility == GpuRenderer::VISIBILITY_OUTSIDE) {
            return;
        }
    }

    /* Two indices per surviving particle, rebuilt only after a deactivation
       has marked the pairs stale. */
    if ((update_flags & W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY) != 0) {
        unsigned int written = 0;
        for (unsigned int index = 0; index < particle_count; ++index) {
            if (particle_active[index] != 0) {
                active_triangles[written++] = index * 2;
                active_triangles[written++] = index * 2 + 1;
            }
        }
        update_flags &= ~static_cast<unsigned int>(W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY);
    }

    const meshSubmission::EnableScope options(*renderer, requires_sorted_renderer != 0);
    renderer->matrixMode(GpuRenderer::MATRIX_MODELVIEW);

    auto view = glm::inverse(renderer->getMatrix(GpuRenderer::MATRIX_MODELVIEW));
    PrepareRenderer(view);

    if (requires_sorted_renderer != 0 &&
        !renderer->isEnabled(GpuRenderer::ENABLE_SORTED_RENDERING)) {
        renderer->toggle(GpuRenderer::ENABLE_SORTED_RENDERING);
    }
    renderer->setCullMode(GpuRenderer::CULL_NONE);
    renderer->setPickKey(0);

    Mesh::TriMesh mesh;
    mesh.active_polygons = std::span(active_triangles).first(active_particle_count * 2);
    mesh.poly_vertices = triangles.data();
    mesh.polygon_count = wiz8::checked_integer<w8_long>(triangles.size());
    mesh.positions = vertex_positions.data();
    mesh.vertex_count = wiz8::checked_integer<w8_long>(vertex_positions.size());
    mesh.pass_count = 1;
    mesh.control_flags =
        (1u << Mesh::CONTROL_SKIP_AUTO_BOX) | (1u << Mesh::CONTROL_SKIP_AUTO_SPHERE);
    mesh.vertex_alpha = alphas;
    mesh.materials[0][0] = material;
    mesh.shaders[0] = render_flags;
    mesh.texcoords[0][0] = texcoords.data();
    if (!texture_frames.empty())
        mesh.poly_textures[0][0] = &texture_frames;
    else
        mesh.textures[0][0] = texture;
    {
        const meshSubmission::PickScope picking(*renderer, true);
        renderer->submitMesh(mesh);
    }
}

void ParticleSystem::SetTexture(std::shared_ptr<Texture> texture)
{
    texture_frames = {};
    if (auto* animation = ((texture.get() && texture.get()->playback) ? texture.get() : nullptr)) {
        texture_frames.resize(particle_count * 2);
        for (unsigned int i = 0; i < texture_frames.size(); i += 2) {
            auto frame = std::make_shared<Texture>(*animation);
            texture_frames.set(i, frame);
            texture_frames.set(i + 1, std::move(frame));
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

unsigned char ParticleSystem::ReplaceTexture(std::string_view old_name,
                                             std::shared_ptr<Texture> replacement)
{
    if (texture && (!texture->source_path.empty() || texture->playback) &&
        CompareTextIgnoreAsciiCase(texture->getName(), old_name) == 0) {
        SetTexture(std::move(replacement));
        return 1;
    }
    return 0;
}

void ParticleSystem::SetMaterial(std::shared_ptr<Material> new_material)
{
    material = std::move(new_material);
}

void ParticleSystem::SetFlutter(W8ParticleFlutterMode mode)
{
    flutter_mode = mode;
    if (mode == W8_PARTICLE_FLUTTER_NONE) {
        flutter_angles.clear();
    } else if (flutter_angles.empty()) {
        flutter_angles.assign(particle_count, 0.0f);
    }
}

// A particle still emitting when its cycle dies finishes under its world's care.
void RetireParticle(World& world, std::unique_ptr<ParticleSystem> particle)
{
    particle->release_when_done = true;
    world.retired_particles.push_back(std::move(particle));
}

void SweepFinishedParticles(World* world)
{
    std::erase_if(world->retired_particles,
                  [](const auto& particle) { return particle->finished; });
}

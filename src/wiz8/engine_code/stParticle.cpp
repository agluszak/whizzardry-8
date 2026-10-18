#include "wiz8/engine_code/stParticle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/gtc/matrix_access.hpp>

#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/matrix.hpp"
#include "layouts/world.h"
#include "particle_modes.h"
#include "random.h"
#include "surrender/mesh_submission.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVertexPipe.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/unicode.h"

constexpr float kParticleFlutterAngleRandomScale = 0.00019174758926965296f;
constexpr float kParticleFlutterDisplacementScale = -0.0010000000474974513f;
constexpr float kParticleFlutterVelocityFloor = -1000.0f;
constexpr float kParticleRandomUnitScale = 3.0517578125e-05f;

void stParticle::SetRenderFlags(srShader flags)
{
    render_flags = flags;
}

void stParticle::SetParticleScale(float scale)
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

void ApplyParticleStates(W8World& world, std::span<const W8ParticleState> states)
{
    for (const auto& state : states) {
        if (auto* particle = world.static_scene->findChild<stParticle>(state.name))
            particle->SetActive(state.active);
    }
}

void stParticle::AllocateParticles(unsigned int count)
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

stParticle::~stParticle() = default;

void stParticle::CancelAnimationCallback()
{
    callback.reset();
}

stParticle::stParticle(srNode* parent, int count) : srNode(static_cast<srNode*>(0))
{
    setParent(parent, 1);
    if (count == 0) {
        return;
    }
    AllocateParticles(count);
    last_integration_tick = GameClockMilliseconds();
    last_emission_tick = last_integration_tick;
}

/* A copy shares material and static texture, but starts with no live
   particles, fresh animation frames and fresh timing. */
stParticle::stParticle(const stParticle& other) : srNode(static_cast<srNode*>(0))
{
    persisted = other.persisted;
    start_frame = other.start_frame;
    end_frame = other.end_frame;
    emission_gap = other.emission_gap;
    size_scale = other.size_scale;
    if (other.particle_count == 0) {
        return;
    }

    setParent(other.getParent(), 1);
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

    setLocation(other.getLocation());
    auto rotation = other.getRotation<glm::mat3>();
    setRotation(rotation);

    auto source_world = other.getWorldSpaceMatrix<glm::mat4>();
    const glm::dmat4 world(source_world);
    setWorldSpaceMatrix(world);
}

std::optional<unsigned int> stParticle::activateParticle(bool replaceWhenFull)
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
        velocity = glm::vec3(getWorldSpaceDOF() * double(magnitude));
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

        const auto rotation = getWorldSpaceRotation<glm::mat3>();
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

    particle_positions[index] = glm::vec3(getLocation());

    if (!flutter_angles.empty()) {
        flutter_angles[index] = Random15() * kParticleFlutterAngleRandomScale;
    }
    if (!texture_frames.empty()) {
        static_cast<stTextureAnim*>(texture_frames[index * 2].get())->SetFrame(0);
    }

    std::fill_n(alphas.begin() + index * 4, 4, 1.0f);

    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
    ++emission_count;
    ++active_particle_count;
    return index;
}

void stParticle::DeactivateParticle(unsigned int index)
{
    if (particle_active[index]) {
        particle_active[index] = false;
        update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
        --active_particle_count;
    }
}

void stParticle::Update()
{
    unsigned int now = GameClockMilliseconds();
    if (now - last_emitted_at < emission_gap) {
        return;
    }

    last_emitted_at = now;
    if (active_particle_count != 0) {
        unsigned int elapsed_ticks = now - last_integration_tick;

        glm::mat3 rotation;
        rotation = getRotation<glm::mat3>();

        glm::mat4 transform(rotation);
        transform = glm::inverse(transform);

        const auto node_location = glm::vec3(getLocation());

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
                    auto* animation = static_cast<stTextureAnim*>(texture_frames[index * 2].get());
                    animation->UpdateFrame();
                    if (animation->IsFinished() != 0) {
                        DeactivateParticle(index);
                        continue;
                    }
                }
            }

            if (has_acceleration == 1) {
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
                if (!srBoundingBox{lifetime_minimum, lifetime_maximum}.contains(local)) {
                    DeactivateParticle(index);
                    continue;
                }
            }

            if (los_check_enabled == 1) {
                // The octree trace still accepts packed endpoint records.
                const glm::vec3 from(particle_positions[index]);
                glm::vec3 to(candidate);
                if (GetWorld()->octree == nullptr ||
                    !GetWorld()->octree->HasLineOfSight(&from, &to, true)) {
                    DeactivateParticle(index);
                    continue;
                }
                candidate = to;
            }

            particle_positions[index] = candidate;
        }
    }

    last_integration_tick = now;

    if (emitting == 0 || emission_mode == W8_PARTICLE_EMISSION_NONE) {
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
        if (has_acceleration == 1) {
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

glm::vec3 stParticle::emissionPosition()
{
    // Braced initialization preserves the x, y, z random draw order.
    const glm::vec3 random{float(Random15()), float(Random15()), float(Random15())};
    const auto offset = ((emission_maximum - emission_minimum) * random * kParticleRandomUnitScale +
                         emission_minimum) *
                        size_scale;
    return getRotation<glm::mat3>() * offset + glm::vec3(getLocation());
}

void stParticle::traverse(srNode::TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }

    if (!testFlag(FLAG_DISABLE)) {
        if ((emitting != 0 || active_particle_count != 0) && traversal_enabled) {
            info.entries.push_back({this, 0});
        }
    }

    if (!testFlag(FLAG_TERMINATE) && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

/* Traversal is gated separately from particle activity.  Starting a new
   enabled interval resets the update timestamp; repeated enables do not. */
void stParticle::SetTraversalEnabled(bool enabled)
{
    if (enabled && !traversal_enabled) {
        last_emission_tick = GameClockMilliseconds();
    }
    traversal_enabled = enabled;
}

void stParticle::process(const ProcessInfo& info, e_processType)
{
    info.renderer->pushMatrix();
    SubmitToRenderer(info.renderer);
    info.renderer->popMatrix();
}

/* Build the four camera-facing offsets once per call, then expand every
   particle center into a quad. */
void stParticle::PrepareRenderer(const glm::mat4& view)
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
void stParticle::SubmitToRenderer(srGERD* renderer)
{
    glm::vec3 position;

    if (finished) {
        return;
    }
    if (camera_relative == 1) {
        position = cameraLocation() + camera_offset;

        setLocation(glm::dvec3(position));
    } else {
        position = glm::vec3(getLocation());
    }

    Update();

    if (emission_limit != 0 && emission_count >= emission_limit) {
        emitting = 0;
    }

    if (emitting == 0 && active_particle_count == 0) {
        if (auto completed = std::move(callback)) {
            completed->RestoreAnimation();
        }
        finished = release_when_done;
        return;
    }

    glm::mat3 rotation;

    if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
        srGERD::e_visibility visibility;

        const auto& extent = bounds_origin;

        if (extent.x == 0.0f && extent.y == 0.0f && extent.z == 0.0f) {
            visibility = renderer->testBoundingSphere(position, size_scale * bounds_radius);
        } else {
            rotation = getRotation<glm::mat3>();
            glm::vec3 center = rotation * extent + position;
            visibility = renderer->testBoundingSphere(center, size_scale * bounds_radius);
        }
        if (visibility == srGERD::VISIBILITY_OUTSIDE) {
            return;
        }
    }

    if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
        rotation = getRotation<glm::mat3>();
        glm::mat4 world(rotation);
        world[3] = glm::vec4(position, 1);
        const auto bounds = srBoundingBox{lifetime_minimum, lifetime_maximum}.transformed(world);
        const auto visibility = renderer->testBoundingBox(bounds.minimum, bounds.maximum);
        if (visibility == srGERD::VISIBILITY_OUTSIDE) {
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

    renderer->pushEnable();
    renderer->matrixMode(srGERD::MATRIX_MODELVIEW);

    auto view = glm::inverse(renderer->getMatrix(srGERD::MATRIX_MODELVIEW));
    PrepareRenderer(view);

    if (requires_sorted_renderer != 0 && !renderer->isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
        renderer->toggle(srGERD::ENABLE_SORTED_RENDERING);
    }
    renderer->setCullMode(srGERD::CULL_NONE);
    renderer->setPickKey(0);

    srTriMeshPipeline* pipeline = &renderer->meshPipeline();

    pipeline->active_triangles = std::span(active_triangles).first(active_particle_count * 2);
    pipeline->triangles = triangles.data();
    pipeline->triangle_count = triangles.size();
    pipeline->positions = vertex_positions.data();
    pipeline->vertex_count = vertex_positions.size();

    pipeline->current_record->flags = 0;
    pipeline->current_pass->shaders = 0;
    pipeline->current_pass->texture_tables[0] = 0;
    pipeline->current_pass->texture_tables[1] = 0;

    pipeline->current_record->alpha_source = alphas.data();
    pipeline->current_record->flags |= srVertexPipe::Record::HAS_ALPHA;

    /* The particle material reaches
       both the pipeline and the record it is about to submit. */
    pipeline->material = material.get();
    pipeline->current_record->material = material.get();

    pipeline->SetFlags(render_flags);

    pipeline->current_record->st_source[0] = texcoords.data();
    pipeline->current_record->flags |= srVertexPipe::Record::HAS_TEXCOORD0;

    if (!texture_frames.empty()) {
        pipeline->current_pass->texture_tables[0] = &texture_frames;
    } else if (texture != 0) {
        pipeline->texture0 = texture.get();
        pipeline->current_pass->textures[0] = texture.get();
    }

    ++pipeline->slot_count;
    pipeline->PrepareSlot();

    {
        const srMeshSubmission::PickScope picking(*renderer, true);
        pipeline->Flush();
    }
    renderer->popEnable();
}

void stParticle::SetTexture(std::shared_ptr<srTexture> texture)
{
    texture_frames = {};
    if (auto* animation = dynamic_cast<stTextureAnim*>(texture.get())) {
        texture_frames.resize(particle_count * 2);
        for (unsigned int i = 0; i < texture_frames.size(); i += 2) {
            auto frame = std::make_shared<stTextureAnim>(*animation);
            texture_frames.set(i, frame);
            texture_frames.set(i + 1, std::move(frame));
        }
    }
    this->texture = std::move(texture);
}

void stParticle::SetActive(unsigned char active)
{
    if (active != 0 && emitting == 0) {
        unsigned int now = GameClockMilliseconds();
        last_integration_tick = now;
        last_emission_tick = now;
    }
    emitting = active;
}

unsigned char stParticle::ReplaceTexture(std::string_view old_name,
                                         std::shared_ptr<srTexture> replacement)
{
    if ((dynamic_cast<stTextureFile*>(texture.get()) ||
         dynamic_cast<stTextureAnim*>(texture.get())) &&
        CompareTextIgnoreAsciiCase(texture->getName(), old_name) == 0) {
        SetTexture(std::move(replacement));
        return 1;
    }
    return 0;
}

void stParticle::SetMaterial(std::shared_ptr<srMaterial> new_material)
{
    material = std::move(new_material);
}

void stParticle::SetFlutter(W8ParticleFlutterMode mode)
{
    flutter_mode = mode;
    if (mode == W8_PARTICLE_FLUTTER_NONE) {
        flutter_angles.clear();
    } else if (flutter_angles.empty()) {
        flutter_angles.assign(particle_count, 0.0f);
    }
}

// A particle still emitting when its cycle dies finishes under its world's care.
void RetireParticle(W8World& world, std::unique_ptr<stParticle> particle)
{
    particle->release_when_done = true;
    world.retired_particles.push_back(std::move(particle));
}

void SweepFinishedParticles(W8World* world)
{
    std::erase_if(world->retired_particles,
                  [](const auto& particle) { return particle->finished; });
}

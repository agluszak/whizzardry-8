#include <glm/gtc/matrix_access.hpp>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/srMath.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srTypeRegistry.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/float_constants.h"
#include "wiz8/sr_api.h"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srTriMeshPipeline.h"
#include "wiz8/filesystem.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/PolyPick.h"
#include "compat/native.h"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/world.h"
#include "particle_modes.h"
#include "surrender/srClock.h"
#include "surrender/srVertexPipe.h"

// GLOBAL: WIZ8 0x005ebc60
const float g_float_one_five_hundredth = 0.0020000000949949026f;
// GLOBAL: WIZ8 0x005ec438
const float g_particle_random_unit_scale = 3.0517578125e-05f;
// GLOBAL: WIZ8 0x005ec8d0
const double g_double_one_thousandth = 0.001;
// GLOBAL: WIZ8 0x005ecc38
const float g_particle_flutter_displacement_scale = -0.0010000000474974513f;
// GLOBAL: WIZ8 0x005ecc3c
const float g_particle_flutter_velocity_floor = -1000.0f;
// GLOBAL: WIZ8 0x005ecc40
const float g_particle_flutter_angle_random_scale = 0.00019174758926965296f;

// STRING: WIZ8 0x0060BF6C
#define ST_PARTICLE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\stParticle.cpp"

// VTABLE: WIZ8 0x005ECBD0
// class stParticle

// VTABLE: WIZ8 0x005ECC04
// class srClassSupport<stParticle,srNode,65545>

/* Return the renderer flags as a value. VC6 lowers the four-byte class return
   through its hidden result pointer. */
// FUNCTION: WIZ8 0x00498A10
srShader stParticle::GetRenderFlags() const
{
    return render_flags;
}

// FUNCTION: WIZ8 0x004925A0
void stParticle::SetRenderFlags(srShader flags)
{
    render_flags = flags;
}

// FUNCTION: WIZ8 0x0049AE90
void stParticle::SetParticleScale(float scale)
{
    float inverse_scale = 1.0f / size_scale;
    // Retail uses the extent as its offset, not the midpoint of the bounds.
    srVector3T<float> offset = lifetime_maximum - lifetime_minimum;
    lifetime_minimum = (lifetime_minimum - offset) * inverse_scale + offset;
    lifetime_maximum = (lifetime_maximum - offset) * inverse_scale + offset;
    size_scale = scale;
    lifetime_minimum = (lifetime_minimum - offset) * scale + offset;
    lifetime_maximum = (lifetime_maximum - offset) * scale + offset;
}

// FUNCTION: WIZ8 0x0049B150
void SaveParticleStates(wiz8::File* handle)
{
    unsigned char version = 1;
    char name[0x80] = "";
    int count = 0;

    handle->write(&version, sizeof(version));

    if (g_world != 0) {
        for (stParticle* particle : g_world->particles) {
            count += particle->persisted;
        }
    }
    handle->write(&count, sizeof(count));

    if (g_world != 0) {
        for (stParticle* particle : g_world->particles) {
            if (particle->persisted) {
                strcpy(name, particle->getName().c_str());
                handle->write(name, sizeof(name));
                handle->write(&particle->emitting, sizeof(particle->emitting));
            }
        }
    }
}

// FUNCTION: WIZ8 0x0049B3B0
void LoadParticleStates(wiz8::File* handle)
{
    unsigned char version;
    int count = 0;
    char name[0x80] = "";

    handle->read_exact(&version, sizeof(version));
    handle->read_exact(&count, sizeof(count));

    for (int index = 0; index < count; ++index) {
        unsigned char active;
        handle->read_exact(name, sizeof(name));
        handle->read_exact(&active, sizeof(active));

        stParticle* particle = FindParticleByName(g_world, name);
        if (particle != 0) {
            particle->SetActive(active);
        }
    }
}

void stParticle::AllocateParticles(unsigned int count)
{
    if (count >= 10000) {
        srAssertFail("cnt < 10000", ST_PARTICLE_CPP, 0x41, 0);
    }
    particle_count = count;
    particle_positions.assign(count, srVector3T<float>(0.0f, 0.0f, 0.0f));
    velocities.resize(count);
    birth_ticks.resize(count);
    particle_active.assign(count, false);
    active_triangles.resize(count * 2);
    vertex_positions.resize(count * 4);
    texcoords.resize(count * 4);
    alphas.assign(count * 4, 1.0f);
    triangles.resize(count * 2);
    for (unsigned int index = 0; index < count; ++index) {
        int vertex = static_cast<int>(index * 4);
        triangles[index * 2] = srVector3i{vertex, vertex + 1, vertex + 2};
        triangles[index * 2 + 1] = srVector3i{vertex + 2, vertex + 3, vertex};
        texcoords[vertex] = glm::vec2(0.0f, 0.0f);
        texcoords[vertex + 1] = glm::vec2(1.0f, 0.0f);
        texcoords[vertex + 2] = glm::vec2(1.0f, 1.0f);
        texcoords[vertex + 3] = glm::vec2(0.0f, 1.0f);
    }
}

// FUNCTION: WIZ8 0x00497AF0
stParticle::stParticle(srNode* parent, int count)
    : srClassSupport<stParticle, srNode, 0x10009>(static_cast<srNode*>(0))
{
    setParent(parent, 1);
    if (count == 0) {
        return;
    }
    AllocateParticles(count);
    last_integration_tick = g_shared_timer_base->milliseconds();
    last_emission_tick = last_integration_tick;
}

/* A copy shares material and static texture, but starts with no live
   particles, fresh animation frames and fresh timing. */
// FUNCTION: WIZ8 0x00498180
stParticle::stParticle(const stParticle& other)
    : srClassSupport<stParticle, srNode, 0x10009>(static_cast<srNode*>(0))
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
    last_integration_tick = g_shared_timer_base->milliseconds();
    last_emission_tick = last_integration_tick;
    attachment_key = other.attachment_key;

    setLocation(other.getLocation());
    glm::mat3 rotation;
    other.getRotation(rotation);
    setRotation(rotation);

    glm::mat4 source_world;
    other.getWorldSpaceMatrix(source_world);
    const glm::dmat4 world(source_world);
    setWorldSpaceMatrix(world);
}

// FUNCTION: WIZ8 0x00499A50
unsigned char stParticle::ActivateParticle(unsigned int* out_index, bool replace_when_full)
{
    if (emission_limit != 0 && emission_count >= emission_limit) {
        return 0;
    }

    unsigned int index;
    for (index = 0; index < particle_count; ++index) {
        if (particle_active[index] == 0) {
            break;
        }
    }

    if (index == particle_count) {
        if (!replace_when_full) {
            return 0;
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

    *out_index = index;
    particle_active[index] = 1;
    birth_ticks[index] = g_shared_timer_base->milliseconds();

    float magnitude = 0.0f;
    if (speed_mode == W8_PARTICLE_SPEED_FIXED) {
        magnitude = initial_speed;
    } else if (speed_mode == W8_PARTICLE_SPEED_RANDOM) {
        magnitude =
            (speed_max - speed_min) * (rand() & 0x7fff) * g_particle_random_unit_scale + speed_min;
    }
    magnitude *= size_scale;

    srVector3T<float>& velocity = velocities[index];
    switch (direction_mode) {
    case W8_PARTICLE_DIRECTION_NODE_FORWARD: {
        srVector3T<double> direction = getWorldSpaceDOF();
        velocity = direction * magnitude;
        break;
    }

    case W8_PARTICLE_DIRECTION_FIXED:
        velocity = this->direction * magnitude;
        break;

    case W8_PARTICLE_DIRECTION_CONE: {
        srVector3T<float> direction;
        direction.Set(g_float_zero, g_float_zero, magnitude);

        double angle =
            ((rand() & 0x7fff) * g_particle_random_unit_scale - g_float_half) * cone_pitch;
        direction.RotateAboutX(sin(angle), cos(angle));

        angle = ((rand() & 0x7fff) * g_particle_random_unit_scale - g_float_half) * cone_yaw;
        direction.RotateAboutY(sin(angle), cos(angle));

            glm::mat3 rotation;
        getWorldSpaceRotation(rotation);
        velocity = rotation * direction.glmValue();
        break;
    }

    case W8_PARTICLE_DIRECTION_RANDOM: {
        srVector3T<float> direction;
        direction.x = (rand() & 0x7fff) * g_particle_random_unit_scale - g_float_half;
        direction.y = (rand() & 0x7fff) * g_particle_random_unit_scale - g_float_half;
        direction.z = (rand() & 0x7fff) * g_particle_random_unit_scale - g_float_half;

        direction.Normalize();

        velocity = direction * magnitude;
        break;
    }

    default:
        velocity = 0.0f;
        break;
    }

    srVector3T<double> location = getLocation();
    particle_positions[index] = location;

    if (!flutter_angles.empty()) {
        flutter_angles[index] = (rand() & 0x7fff) * g_particle_flutter_angle_random_scale;
    }
    if (!texture_frames.empty()) {
        static_cast<stTextureAnim*>(texture_frames[index * 2].get())->SetFrame(0);
    }

    unsigned int vertex = index * 4;
    unsigned int end = vertex + 4;
    for (; vertex < end; ++vertex) {
        alphas[vertex] = 1.0f;
    }

    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
    ++emission_count;
    ++active_particle_count;
    return 1;
}

// FUNCTION: WIZ8 0x00499F70
void stParticle::DeactivateParticle(unsigned int index)
{
    if (particle_active[index]) {
        particle_active[index] = false;
        update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
        --active_particle_count;
    }
}

// FUNCTION: WIZ8 0x00499FA0
void stParticle::Update()
{
    unsigned int now = g_shared_timer_base->milliseconds();
    if (now - last_emitted_at < emission_gap) {
        return;
    }

    last_emitted_at = now;
    if (active_particle_count != 0) {
        unsigned int elapsed_ticks = now - last_integration_tick;

        glm::mat3 rotation;
        getRotation(rotation);

        glm::mat4 transform(rotation);
        transform = glm::inverse(transform);

        srVector3T<float> node_location;
        getLocation(node_location);

        double elapsed = static_cast<double>(elapsed_ticks);
        srVector3T<float> acceleration_step =
            this->acceleration * (elapsed * g_double_one_thousandth);

        unsigned int index;
        for (index = 0; index < particle_count; ++index) {
            if (particle_active[index] == 0) {
                continue;
            }

            unsigned int vertex = index * 4;
            if (expiry_mode == W8_PARTICLE_EXPIRY_TIMED) {
                unsigned int expires_at = birth_ticks[index] + lifetime_ms;
                if (expires_at < now) {
                    particle_active[index] = 0;
                    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
                    --active_particle_count;
                    continue;
                }
                if (expires_at - 500 < now) {
                    float alpha = (expires_at - now) * g_float_one_five_hundredth;
                    unsigned int alpha_end = vertex + 4;
                    unsigned int alpha_index;
                    for (alpha_index = vertex; alpha_index < alpha_end; ++alpha_index) {
                        alphas[alpha_index] = alpha;
                    }
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
                        particle_active[index] = 0;
                        update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
                        --active_particle_count;
                        continue;
                    }
                }
            }

            if (has_acceleration == 1) {
                velocities[index] += acceleration_step;
            }

            srVector3T<float> movement = velocities[index] * (elapsed * g_double_one_thousandth);

            srVector3T<float> candidate;
            candidate = particle_positions[index] + movement;

            if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
                double distance;
                if (bounds_origin.x == g_float_zero && bounds_origin.y == g_float_zero &&
                    bounds_origin.z == g_float_zero) {
                    distance = (candidate - node_location).Length();
                } else {
                    srVector3T<float> center =
                        rotation * bounds_origin.glmValue() + node_location.glmValue();
                    srVector3T<float> difference = candidate - center;
                    distance = difference.Length();
                }

                if (size_scale * bounds_radius < distance) {
                    particle_active[index] = 0;
                    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
                    --active_particle_count;
                    continue;
                }
            } else if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
                srVector3T<float> local = candidate - node_location;
                glm::vec4 transformed = glm::vec4{transform * glm::vec4(local.glmValue(), 1)};
                srVector3T<float> local_point;
                local_point = glm::vec3(transformed);
                if (!PointInsideBounds(&local_point, &lifetime_minimum, &lifetime_maximum)) {
                    particle_active[index] = 0;
                    update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
                    --active_particle_count;
                    continue;
                }
            }

            if (los_check_enabled == 1 &&
                (g_world->octree == 0 ||
                 !g_world->octree->HasLineOfSight(&particle_positions[index], &candidate, true))) {
                particle_active[index] = 0;
                update_flags |= W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY;
                --active_particle_count;
                continue;
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
        unsigned int particle_index;
        ActivateParticle(&particle_index, replace_when_full);
        last_emission_tick = now;
        return;
    }
    if (emission_mode != W8_PARTICLE_EMISSION_CATCH_UP || emission_elapsed <= emission_interval) {
        return;
    }

    for (;;) {
        unsigned int lag = now - emission_interval - last_emission_tick;
        unsigned int particle_index;
        if (ActivateParticle(&particle_index, replace_when_full) == 0) {
            last_emission_tick = now;
            return;
        }

        if (has_acceleration == 1) {
            srVector3T<float> acceleration =
                (this->acceleration * static_cast<double>(lag)) / 1000.0;
            velocities[particle_index] += acceleration;
        }

        srVector3T<float> displacement = velocities[particle_index];
        displacement *= static_cast<double>(lag);
        displacement /= 1000.0;
        InitializeParticlePosition(&particle_positions[particle_index]);
        particle_positions[particle_index] += displacement;

        last_emission_tick += emission_interval;
        if (now - last_emission_tick <= emission_interval) {
            return;
        }
    }
}

// FUNCTION: WIZ8 0x0049A990
void stParticle::InitializeParticlePosition(srVector3T<float>* output)
{
    output->x = (emission_maximum.x - emission_minimum.x) * (rand() & 0x7fff) *
                    g_particle_random_unit_scale +
                emission_minimum.x;
    output->y = (emission_maximum.y - emission_minimum.y) * (rand() & 0x7fff) *
                    g_particle_random_unit_scale +
                emission_minimum.y;
    output->z = (emission_maximum.z - emission_minimum.z) * (rand() & 0x7fff) *
                    g_particle_random_unit_scale +
                emission_minimum.z;

    *output *= size_scale;

    glm::mat3 rotation;
    getRotation(rotation);
    (*output) = rotation * (*output).glmValue();

    srVector3T<double> location = getLocation();
    output->x += static_cast<float>(location.x);
    output->y += static_cast<float>(location.y);
    output->z += static_cast<float>(location.z);
}

// FUNCTION: WIZ8 0x00498C40
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
// FUNCTION: WIZ8 0x00498D90
void stParticle::SetTraversalEnabled(bool enabled)
{
    if (enabled && !traversal_enabled) {
        last_emission_tick = g_shared_timer_base->milliseconds();
    }
    traversal_enabled = enabled;
}

// FUNCTION: WIZ8 0x00498D60
void stParticle::process(const ProcessInfo& info, e_processType)
{
    info.renderer->pushMatrix();
    SubmitToRenderer(info.renderer);
    info.renderer->popMatrix();
}

/* Build the four camera-facing offsets once per call, then expand every
   particle center into a quad. */
// FUNCTION: WIZ8 0x00498DD0
void stParticle::PrepareRenderer(glm::mat4& view)
{
    static srVector3T<float> corners[4] = {
        srVector3T<float>(-0.5f, 0.5f, 0.0f), srVector3T<float>(0.5f, 0.5f, 0.0f),
        srVector3T<float>(0.5f, -0.5f, 0.0f), srVector3T<float>(-0.5f, -0.5f, 0.0f)};
    static srVector3T<float> offsets[4];

    view[3][0] = 0.0f;
    view[3][1] = 0.0f;
    view[3][2] = 0.0f;

    float normalization = static_cast<float>(g_double_one / glm::length(glm::row(view, 0)));
    view = glm::row(view, 0, glm::row(view, 0) * (normalization));
    view = glm::row(view, 1, glm::row(view, 1) * (normalization));
    view = glm::row(view, 2, glm::row(view, 2) * (normalization));

    for (unsigned int index = 0; index < 4; ++index) {
        glm::vec4 transformed = glm::vec4{view * glm::vec4(corners[index].glmValue(), 1)};

        float scale = static_cast<float>(particle_size) * size_scale;
        offsets[index] = srVector3T<float>(transformed.x, transformed.y, transformed.z) *
                         static_cast<double>(scale);
    }

    if (flutter_mode == W8_PARTICLE_FLUTTER_NONE) {
        for (unsigned int direct_index = 0; direct_index < particle_count; ++direct_index) {
            unsigned int vertex = direct_index * 4;
            const srVector3T<float>& position = particle_positions[direct_index];
            vertex_positions[vertex] = position + offsets[0];
            vertex_positions[vertex + 1] = position + offsets[1];
            vertex_positions[vertex + 2] = position + offsets[2];
            vertex_positions[vertex + 3] = position + offsets[3];
        }
        return;
    }

    float phase = g_float_zero;
    if (flutter_period != 0) {
        phase = static_cast<float>(g_shared_timer_base->milliseconds() %
                                   flutter_period) /
                static_cast<int>(flutter_period) * g_camera_angle_period;
    }
    float flutter = static_cast<float>(sin(phase)) * flutter_amplitude * size_scale;

    for (unsigned int particle_index = 0; particle_index < particle_count; ++particle_index) {
        srVector3T<float> position;

        if (velocities[particle_index].y >= g_float_zero) {
            position = particle_positions[particle_index];
        } else {
            position.Set(flutter, 0.0f, 0.0f);

            if (flutter_mode == W8_PARTICLE_FLUTTER_VELOCITY_SCALED) {
                float scale = g_particle_flutter_velocity_floor;
                if (g_particle_flutter_velocity_floor < velocities[particle_index].y) {
                    scale = velocities[particle_index].y;
                }
                position.x = scale * g_particle_flutter_displacement_scale * flutter;
            }

            double angle = flutter_angles[particle_index];
            position.RotateAboutY(sin(angle), cos(angle));
            position += particle_positions[particle_index];
        }

        unsigned int vertex = particle_index * 4;
        vertex_positions[vertex] = position + offsets[0];
        vertex_positions[vertex + 1] = position + offsets[1];
        vertex_positions[vertex + 2] = position + offsets[2];
        vertex_positions[vertex + 3] = position + offsets[3];
    }
}

/* One particle system's complete submission. The system is placed (either
   relative to the camera or at its own node location), aged, stopped after
   its configured total activation count, culled against the renderer, and
   finally handed to the shared triangle-mesh pipeline as a single slot.

   The retired path is the only one that can drop the system: a particle whose
   activity has run out notifies its shake callback and, when release_when_done marks
   it as self-owned, releases itself. */
// FUNCTION: WIZ8 0x004994D0
void stParticle::SubmitToRenderer(srGERD* renderer)
{
    glm::vec3 position;

    if (camera_relative == 1) {
        srVector3T<float> camera_position;
        GetCameraPosition(&camera_position);
        position = camera_position.glmValue() + camera_offset.glmValue();

        setLocation(srVector3T<double>(glm::dvec3(position)));
    } else {
        position = glm::vec3(getLocation());
    }

    Update();

    if (emission_limit != 0 && emission_count >= emission_limit) {
        emitting = 0;
    }

    if (emitting == 0 && active_particle_count == 0) {
        if (callback != 0) {
            callback->RestoreAnimation();
        }
        if (release_when_done) {
            delete this;
        }
        return;
    }

    glm::mat3 rotation;

    if (bounds_mode == W8_PARTICLE_BOUNDS_SPHERE) {
        srGERD::e_visibility visibility;

        const srVector3T<float>& extent = bounds_origin;

        if (extent.x == g_float_zero && extent.y == g_float_zero && extent.z == g_float_zero) {
            visibility = renderer->testBoundingSphere(position, size_scale * bounds_radius);
        } else {
            getRotation(rotation);
            glm::vec3 center = rotation * extent.glmValue() + position;
            visibility = renderer->testBoundingSphere(center, size_scale * bounds_radius);
        }
        if (visibility == srGERD::VISIBILITY_OUTSIDE) {
            return;
        }
    }

    if (bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
        getRotation(rotation);
        glm::mat4 world(rotation);
        world[3] = glm::vec4(position, 1);
        const auto bounds =
            srBoundingBox{lifetime_minimum.glmValue(), lifetime_maximum.glmValue()}.transformed(
                world);
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

    glm::mat4 view;
    renderer->getMatrix(srGERD::MATRIX_MODELVIEW, view);
    view = glm::inverse(view);
    PrepareRenderer(view);

    if (requires_sorted_renderer != 0 && !renderer->isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
        renderer->toggle(srGERD::ENABLE_SORTED_RENDERING);
    }
    renderer->setCullMode(srGERD::CULL_NONE);
    renderer->setPickKey(0);

    srTriMeshPipeline* pipeline = srTriMeshPipeline::Get(renderer);

    /* Three array/count pairs: the index pairs rebuilt above, the polygon
       index list, and the transformed vertex positions. */
    pipeline->active_triangles = active_triangles.data();
    pipeline->active_triangle_count = active_particle_count * 2;
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
        pipeline->current_pass->texture_tables[0] = texture_frames.data();
    } else if (texture != 0) {
        pipeline->texture0 = texture.get();
        pipeline->current_pass->textures[0] = texture.get();
    }

    ++pipeline->slot_count;
    pipeline->PrepareSlot();

    if (!renderer->isPickStackEmpty()) {
        srGERD::Pick pick;

        renderer->popPick(pick);
        pipeline->FlushIfCurrent();
        renderer->pushPick(pick);
    } else {
        pipeline->FlushIfCurrent();
    }
    renderer->popEnable();
}

// FUNCTION: WIZ8 0x0049AB00
void stParticle::SetTexture(std::shared_ptr<srTextureIFace> texture)
{
    texture_frames.clear();
    if (auto* animation = sr_cast<stTextureAnim>(texture)) {
        texture_frames.resize(particle_count * 2);
        for (unsigned int i = 0; i < texture_frames.size(); i += 2) {
            texture_frames[i] = texture_frames[i + 1] = std::make_shared<stTextureAnim>(*animation);
        }
    }
    this->texture = std::move(texture);
}

// FUNCTION: WIZ8 0x0049acd0
void stParticle::SetActive(unsigned char active)
{
    if (active != 0 && emitting == 0) {
        unsigned int now = g_shared_timer_base->milliseconds();
        last_integration_tick = now;
        last_emission_tick = now;
    }
    emitting = active;
}

// FUNCTION: WIZ8 0x0049ac30
unsigned char stParticle::ReplaceTexture(const char* old_name,
                                         std::shared_ptr<srTextureIFace> replacement)
{
    if ((sr_cast<stTextureFile>(texture) || sr_cast<stTextureAnim>(texture)) &&
        _stricmp(texture->getName().c_str(), old_name) == 0) {
        SetTexture(std::move(replacement));
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x0049ACA0
void stParticle::SetMaterial(std::shared_ptr<srMaterialIFace> new_material)
{
    material = std::move(new_material);
}

// FUNCTION: WIZ8 0x0049AD10
void stParticle::SetFlutter(W8ParticleFlutterMode mode)
{
    flutter_mode = mode;
    if (mode == W8_PARTICLE_FLUTTER_NONE) {
        flutter_angles.clear();
    } else if (flutter_angles.empty()) {
        flutter_angles.assign(particle_count, 0.0f);
    }
}

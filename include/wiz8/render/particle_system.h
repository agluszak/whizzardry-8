#include <chrono>
#pragma once
struct W8Chunk;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/transform.h"

struct W8MonsterShakeCallback;
namespace render {
class RenderContext;
}
struct ParticleRenderData;
class WorldScene;
namespace wiz8 {
class File;
}

class ParticleSystem {
public:
    ParticleSystem(WorldScene* scene, int count); /* 0x00497AF0 */
    ParticleSystem(const ParticleSystem& other); /* 0x00498180 */
    ~ParticleSystem();
    void CancelAnimationCallback();
    void SetActive(bool active);
    void SetTraversalEnabled(bool enabled);
    void DeactivateParticle(unsigned int index);
    std::optional<unsigned int> activateParticle(bool replaceWhenFull);
    glm::vec3 emissionPosition();
    void SetTexture(std::shared_ptr<Texture> texture);
    void SetMaterial(std::shared_ptr<Material> material);
    void SetRenderFlags(DrawShader flags);
    void SetFlutter(W8ParticleFlutterMode mode);
    void SetParticleScale(float scale);
    // Called once by the game update, independent of the number of views.
    void AdvanceSimulation(std::chrono::milliseconds game_time);
    // The effect's scene member; the scene inserts it on attach and removes it on detach.
    vsg::ref_ptr<vsg::Node> renderNode() const;
    void updateRenderNode(render::RenderContext& context, const SceneFrame& frame,
                          std::uint64_t frame_number);
    bool ReplaceTexture(std::string_view old_name, std::shared_ptr<Texture> replacement);

    ParticleSystem& operator=(const ParticleSystem&) = delete;

    unsigned int requires_sorted_renderer = 0;
    double particle_size = 1.0; /* billboard quad scale */
    std::shared_ptr<Material> material;
    DrawShader render_flags;
    std::shared_ptr<Texture> texture;
    unsigned int particle_count = 0;
    /* Per-particle state. */
    std::vector<glm::vec3> particle_positions;
    std::vector<glm::vec3> velocities;
    /* Unsigned millisecond birth ticks. */
    std::vector<unsigned int> birth_ticks;
    std::vector<bool> particle_active;
    /* Flutter phase per particle; empty without flutter. */
    std::vector<float> flutter_angles;
    std::vector<float> alphas;
    // Each particle has independent animated-texture playback.
    std::vector<std::shared_ptr<Texture>> texture_frames;
    /* Both unsigned: 0x004994D0 gates the particle off with the unsigned
       `emission_limit != 0 && emission_limit <= emission_count` pair. */
    unsigned int emission_limit = 0;
    unsigned int emission_count = 0;
    unsigned int active_particle_count = 0;
    /* Once it stops emitting with no live particles, the particle marks itself
       finished; its owner (the world's retired list) destroys it outside rendering. */
    bool release_when_done = false;
    bool finished = false;
    bool replace_when_full = false;
    bool persisted = false;
    /* The save codec writes this as the existing one-byte active selector. */
    bool emitting = true;
    bool traversal_enabled = true;
    W8ParticleBoundsMode bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
    bool has_acceleration = false;
    W8ParticleExpiryMode expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
    W8ParticleEmissionMode emission_mode = W8_PARTICLE_EMISSION_NONE;
    bool los_check_enabled = false;
    W8ParticleDirectionMode direction_mode = W8_PARTICLE_DIRECTION_CONE;
    W8ParticleSpeedMode speed_mode = W8_PARTICLE_SPEED_RANDOM;
    W8ParticleFlutterMode flutter_mode = W8_PARTICLE_FLUTTER_NONE;
    bool camera_relative = false;
    /* Emission interval; elapsed comparisons use unsigned subtraction. */
    unsigned int emission_interval = 50;
    /* Lifetime added to each absolute unsigned birth tick. */
    unsigned int lifetime_ms = 1500;
    glm::vec3 emission_minimum{-250.0f, -250.0f, -250.0f};
    glm::vec3 emission_maximum{250.0f, 250.0f, 250.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 acceleration{0.0f, -4905.0f, 0.0f};
    float flutter_amplitude = 0.0f;
    /* 0x00498DD0 uses this as an unsigned modulus period. */
    unsigned int flutter_period = 0;
    float cone_yaw = 0.39269906f;
    float cone_pitch = 0.39269906f;
    float initial_speed = 500.0f;
    float speed_min = 1000.0f;
    float speed_max = 4000.0f;
    glm::vec3 lifetime_minimum{-1000.0f, -1000.0f, -1000.0f};
    glm::vec3 lifetime_maximum{1000.0f, 1000.0f, 1000.0f};
    glm::vec3 bounds_origin{0.0f, 0.0f, 0.0f};
    float bounds_radius = 2000.0f;
    /* Added to the camera position when camera_relative selects camera-relative
       placement. */
    glm::vec3 camera_offset{0.0f, 0.0f, 0.0f};
    /* Last accepted particle-integration tick. */
    unsigned int last_integration_tick = 0;
    /* Emission schedule tick. */
    unsigned int last_emission_tick = 0;
    short attachment_key = -1;
    int start_frame = -1;
    int end_frame = -1;
    std::unique_ptr<W8MonsterShakeCallback> callback;
    unsigned int emission_gap = 25;
    unsigned int last_emitted_at = 0;
    float size_scale = 1.0f;

private:
    void AllocateParticles(unsigned int count);
    void Update(); /* 0x00499FA0 */
    vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
    std::unique_ptr<ParticleRenderData> render_data;

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
    WorldScene* scene = nullptr; // Borrowed typed render membership.
    void setScene(WorldScene* value);
};

struct World;
// Advance attached particles in their scene order, after entity placement.
void AdvanceWorldParticles(World& world, std::chrono::milliseconds game_time);
// A still-emitting particle outlives its cycle in the world's retired list;
// the sweep destroys it once it has finished, never during rendering.
void RetireParticle(World& world, std::unique_ptr<ParticleSystem> particle);
void SweepFinishedParticles(World* world);

void SaveParticleStates(wiz8::File& handle);
struct W8ParticleState {
    std::string name;
    unsigned char active;
};
std::vector<W8ParticleState> ReadParticleStates(W8Chunk& file);
void ApplyParticleStates(World& world, std::span<const W8ParticleState> states);

#pragma once
struct W8Chunk;

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "wiz8/engine_code/particle_modes.h"

struct W8MonsterShakeCallback;
class srGERD;
namespace wiz8 {
class File;
}

class stParticle : public srNode {
public:
    stParticle(srNode* parent, int count); /* 0x00497AF0 */
    stParticle(const stParticle& other); /* 0x00498180 */
    ~stParticle() override;
    void CancelAnimationCallback();
    void SetActive(bool active);
    void SetTraversalEnabled(bool enabled);
    void DeactivateParticle(unsigned int index);
    std::optional<unsigned int> activateParticle(bool replaceWhenFull);
    glm::vec3 emissionPosition();
    void SetTexture(std::shared_ptr<srTexture> texture);
    void SetMaterial(std::shared_ptr<srMaterial> material);
    void SetRenderFlags(srShader flags);
    void SetFlutter(W8ParticleFlutterMode mode);
    void SetParticleScale(float scale);
    void SubmitToRenderer(srGERD* renderer);
    /* The per-particle age/cull/move step and billboard-corner expansion used
       by the submitted batch. Their retail names remain unavailable. */
    void Update(); /* 0x00499FA0 */
    void PrepareRenderer(const glm::mat4& view); /* 0x00498DD0 */
    unsigned char ReplaceTexture(const char* old_name, std::shared_ptr<srTexture> replacement);
    virtual void traverse(srNode::TraverseInfo& info) override; /* 0x00498C40 */
    virtual void process(const srNode::ProcessInfo& info,
                         srNode::e_processType type) override; /* 0x00498D60 */

    stParticle& operator=(const stParticle&) = delete;

    unsigned int requires_sorted_renderer = 0;
    double particle_size = 1.0; /* billboard quad scale */
    std::shared_ptr<srMaterial> material;
    srShader render_flags;
    std::shared_ptr<srTexture> texture;
    unsigned int particle_count = 0;
    /* Per-particle state. */
    std::vector<glm::vec3> particle_positions;
    std::vector<glm::vec3> velocities;
    /* Unsigned millisecond birth ticks. */
    std::vector<unsigned int> birth_ticks;
    std::vector<bool> particle_active;
    /* Flutter phase per particle; empty without flutter. */
    std::vector<float> flutter_angles;
    /* Billboard geometry: four vertices and two triangles per particle. */
    std::vector<glm::vec3> vertex_positions;
    std::vector<glm::vec2> texcoords;
    std::vector<float> alphas;
    std::vector<glm::ivec3> triangles;
    /* Per-triangle animated texture copies; both triangles of a particle share
       one. Empty unless the texture is animated. */
    MeshResources<srTexture> texture_frames;
    /* Two triangle indices per still-active particle, rebuilt whenever
       update_flags carries W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY. */
    std::vector<w8_ulong> active_triangles;
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
    unsigned int update_flags = 0;
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
};

struct W8World;
// A still-emitting particle outlives its cycle in the world's retired list;
// the sweep destroys it once it has finished, never during rendering.
void RetireParticle(W8World& world, std::unique_ptr<stParticle> particle);
void SweepFinishedParticles(W8World* world);

void SaveParticleStates(wiz8::File& handle);
struct W8ParticleState {
    std::string name;
    unsigned char active;
};
std::vector<W8ParticleState> ReadParticleStates(W8Chunk& file);
void ApplyParticleStates(W8World& world, std::span<const W8ParticleState> states);

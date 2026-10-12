#pragma once

#include <cstdint>
#include "glm/ext/vector_int3.hpp"

#include <memory>
#include <optional>
#include "glm/ext/vector_float3.hpp"
#include <vector>

#include "wiz8/engine_code/particle_modes.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"

class W8MonsterShakeCallback;
class srGERD;
namespace wiz8 { class File; }

class stParticle : public srClassSupport<stParticle, srNode, 0x10009> {
public:
    static const char* sGetClassName()
    {
        return "stParticle";
    }
    stParticle(srNode* parent, std::int32_t count); /* 0x00497AF0 */
    stParticle(const stParticle& other);   /* 0x00498180 */
    void SetActive(bool active);
    void SetTraversalEnabled(bool enabled);
    void DeactivateParticle(std::uint32_t index);
    std::optional<std::uint32_t> activateParticle(bool replaceWhenFull);
    glm::vec3 emissionPosition();
    void SetTexture(std::shared_ptr<srTextureIFace> texture);
    void SetMaterial(std::shared_ptr<srMaterialIFace> material);
    void SetRenderFlags(srShader flags);
    void SetFlutter(W8ParticleFlutterMode mode);
    void SetParticleScale(float scale);
    void SubmitToRenderer(srGERD* renderer);
    /* The per-particle age/cull/move step and billboard-corner expansion used
       by the submitted batch. Their retail names remain unavailable. */
    void Update();                                 /* 0x00499FA0 */
    void PrepareRenderer(const glm::mat4& view); /* 0x00498DD0 */
    srShader GetRenderFlags() const;
    bool ReplaceTexture(const char* old_name, std::shared_ptr<srTextureIFace> replacement);
    virtual void traverse(srNode::TraverseInfo& info) override; /* 0x00498C40 */
    virtual void process(const srNode::ProcessInfo& info,
                         srNode::e_processType type) override; /* 0x00498D60 */

    stParticle& operator=(const stParticle&) = default;

    std::uint32_t requires_sorted_renderer = 0;
    double particle_size = 1.0; /* billboard quad scale */
    std::shared_ptr<srMaterialIFace> material;
    srShader render_flags;
    std::shared_ptr<srTextureIFace> texture;
    std::uint32_t particle_count = 0;
    /* Per-particle state. */
    std::vector<glm::vec3> particle_positions;
    std::vector<glm::vec3> velocities;
    /* Unsigned millisecond birth ticks. */
    std::vector<std::uint32_t> birth_ticks;
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
    std::vector<std::shared_ptr<srTextureIFace>> texture_frames;
    /* Two triangle indices per still-active particle, rebuilt whenever
       update_flags carries W8_PARTICLE_ACTIVE_TRIANGLES_DIRTY. */
    std::vector<std::uint32_t> active_triangles;
    /* Both unsigned: 0x004994D0 gates the particle off with the unsigned
       `emission_limit != 0 && emission_limit <= emission_count` pair. */
    std::uint32_t emission_limit = 0;
    std::uint32_t emission_count = 0;
    std::uint32_t active_particle_count = 0;
    /* The particle deletes itself once it stops emitting and has no live particles. */
    bool release_when_done = false;
    bool replace_when_full = false;
    bool persisted = false;
    std::uint8_t emitting = 1;
    bool traversal_enabled = true;
    W8ParticleBoundsMode bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
    std::int32_t has_acceleration = 0;
    W8ParticleExpiryMode expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
    W8ParticleEmissionMode emission_mode = W8_PARTICLE_EMISSION_NONE;
    std::int32_t los_check_enabled = 0;
    W8ParticleDirectionMode direction_mode = W8_PARTICLE_DIRECTION_CONE;
    W8ParticleSpeedMode speed_mode = W8_PARTICLE_SPEED_RANDOM;
    W8ParticleFlutterMode flutter_mode = W8_PARTICLE_FLUTTER_NONE;
    std::int32_t camera_relative = 0;
    /* Emission interval; elapsed comparisons use unsigned subtraction. */
    std::uint32_t emission_interval = 50;
    /* Lifetime added to each absolute unsigned birth tick. */
    std::uint32_t lifetime_ms = 1500;
    glm::vec3 emission_minimum{-250.0f, -250.0f, -250.0f};
    glm::vec3 emission_maximum{250.0f, 250.0f, 250.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 acceleration{0.0f, -4905.0f, 0.0f};
    float flutter_amplitude = 0.0f;
    /* 0x00498DD0 uses this as an unsigned modulus period. */
    std::uint32_t flutter_period = 0;
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
    std::uint32_t update_flags = 0;
    /* Last accepted particle-integration tick. */
    std::uint32_t last_integration_tick = 0;
    /* Emission schedule tick. */
    std::uint32_t last_emission_tick = 0;
    std::int16_t attachment_key = -1;
    std::int32_t start_frame = -1;
    std::int32_t end_frame = -1;
    W8MonsterShakeCallback* callback = nullptr;
    std::uint32_t emission_gap = 25;
    std::uint32_t last_emitted_at = 0;
    float size_scale = 1.0f;

private:
    void AllocateParticles(std::uint32_t count);
};

void SaveParticleStates(wiz8::File& handle);
void LoadParticleStates(wiz8::File& handle);

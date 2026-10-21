#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/spatial/bounding_box.h"

// Shared animation playback and navigation for monsters, missiles, and spell visuals.

#include <string>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/entities/animation_types.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/spatial/navigator.h"

class MeshInstance;
struct World;
struct LevelLoadContext;
#include "wiz8/audio/SoundEvent.h"

class Light;
class Mesh;
class ParticleSystem;
class GroundShadow;
struct Animation;
struct AnimationMesh;
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/render/camera_shake.h"

class ParticleAttachment {
public:
    void SelectCycle(signed char selected_cycle);

    int cycle;
    signed char subcycle;
    std::unique_ptr<ParticleSystem> m_pstParticles;
    glm::vec3 position;
    /* 0x004A7E50 composes this into the model instance's own rotation with
       MultiplyBy, which is what makes it a matrix rather than 0x24
       opaque bytes. */
    glm::mat3 rotation;
};

class CyclePlayer {
public:
    World* const owner_world;
    explicit CyclePlayer(World* owner = nullptr);
    // Borrowed lifetime: the world destroys its cycles before its scene.
    CyclePlayer(const CyclePlayer& other) : CyclePlayer(other, other.owner_world) {}
    CyclePlayer(const CyclePlayer& other, World* owner); /* 0x004A5F20 */
    CyclePlayer& operator=(const CyclePlayer&) = delete;
    ~CyclePlayer();

    W8SoundEvent& AddSoundEvent(W8SoundEvent event);
    void SetLocation(const glm::vec3* new_location);
    glm::vec3 GetLocation() const;
    glm::vec3 GetLocalLocation() const;
    void SetRotation(const glm::mat3* new_rotation);
    glm::mat3 GetRotation() const;
    void SetFrameMethod(W8AnimationFrameMethod method);

    // Gameplay despawning lets active particles finish; level destruction does not.
    void RetireParticles(World& world);
    // Take this player's lights and particles out of the scene for good, as a
    // never-drawn definition needs; its resources stay loaded for copying.
    void WithdrawFromScene();
    void TickAnimation(float rate, const glm::vec3& position, bool reset_frame_bounds,
                       const std::function<bool(unsigned char)>& frame_event); /* 0x004A6E20 */
    void finishCycleSelection(const glm::vec3& position); /* 0x004A6FC0 */
    void SetPresentationColour(glm::vec4 colour);
    void UpdateRepresentation(World* world, const Navigator& nav); /* 0x004A7470 */

    std::optional<BoundingBox> GetAnimationBounds();
    unsigned char GetAnimationRadius(float* radius);

    void advanceFrame();

    void SetSubCycle(unsigned char subcycle);
    void SetBehaviour(W8AnimationBehaviour bBehaviour);
    void SetLights(std::vector<std::unique_ptr<Light>>* lights);
    void DetachCycleLights(std::vector<std::unique_ptr<Light>>* lights);
    void AttachCycleLights(std::vector<std::unique_ptr<Light>>* lights);
    void AddShakeEffect(ShakeDefinition effect);
    void CreateGroundShadow(float width, float depth);
    void SetGroundShadowVisible(bool visible);
    void ResetRepresentation();
    void DetachRepresentation(World* world);
    /* Runs at the end of every representation update; its own body is the
       shake/particle event walk. */
    void UpdateParticleAttachments();
    void SelectLOD(const glm::vec3* position); /* 0x004A7BE0 */
    void UpdateLights();
    MeshInstance* SelectCycleFrameLod(signed char frame, signed char lod);
    MeshInstance* frameModel(signed char frame, signed char lod) const;
    signed char frameCount() const;
    Animation* active_animation = nullptr;
    MeshInstance* GetCurrentModelInstance();
    void SubmitTargetValue();

public:
    std::optional<KeyframeTrack> m_pAI;
    std::vector<W8SoundEvent> sound_events;
    glm::vec3 location{0.0f};
    glm::vec3 local_location{0.0f};
    glm::vec3 parent_location{0.0f};
    glm::mat3 rotation{1.0f};
    glm::vec4 highlight_colour{0.0f};
    // The actor's appearance, applied to whichever model instance is active.
    std::string skin;
    int damage_stage = -1;
    float light_scale = 1.0f;
    float instance_scale = 1.0f;
    bool apply_instance_scale = false;
    unsigned char subcycle = 0;
    unsigned short pending_subcycle = 0xffff;
    unsigned int timer = 0;
    bool rep_active = false;
    unsigned char animation_playing = 0;
    W8AnimationDirection frame_direction = W8_ANIMATION_DIRECTION_NONE;
    W8AnimationFrameMethod frame_method = W8_ANIMATION_FRAME_METHOD_NONE;
    W8AnimationBehaviour animation_behaviour = W8_ANIMATION_BEHAVIOUR_NONE;
    W8AnimationBehaviour pending_behaviour = W8_ANIMATION_NO_PENDING_BEHAVIOUR;
    unsigned char first_frame = 0xff;
    unsigned char last_frame = 0xff;
    signed char m_bLOD = 0;
    float lod_near = 3500.0f;
    float lod_far = 8500.0f;
    signed char current_cycle = 0;
    signed char current_subcycle = 0;
    signed char forced_subcycle = -1;
    signed char pending_cycle = -1;
    float animation_radius = 0.0f;

    MeshInstance* current_model_instance;
    std::vector<std::unique_ptr<Light>>* m_plsLights; /* 0x1ac */
    std::deque<ShakeDefinition> shake_events;
    /* 0x1b5: the subcycle the last update pass left on the representation. */
    unsigned char last_subcycle;
    std::vector<ParticleAttachment> particles;
    /* 0x1bc: set when the frame walk wrapped to first_frame; suppresses the
       per-subcycle light reset. */
    bool wrapped;
    bool enabled;
    /* 0x1be: mirror the model on X (the left-handed strike pick). */
    bool mirror_x;
    /* 0x1bf: m_axis holds an aim point; mode-3 particles orient along it. */
    bool aim_set;
    /* The axis 0x004A7E50 aims a mode-three particle along. */
    glm::vec3 m_axis;
    float opacity;
    std::unique_ptr<GroundShadow> m_ground_shadow;
    /* Fractional frame progress after TickAnimation consumes whole frames.
       Monster interpolation and light definition time use the same fraction. */
    float frame_fraction;
}; /* 0x1d8 */

bool loadAnimationCycle(std::string_view name, CyclePlayer& player, int cycle_index,
                        std::string_view directory,
                        const std::function<bool(LevelLoadContext&, int)>& read_payload,
                        std::string_view bitmap_directory = {});

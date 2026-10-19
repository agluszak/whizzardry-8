#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"

// Shared animation playback and navigation for monsters, missiles, and spell visuals.

#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/platform/game_timer.h"

class srModelInstance;
struct W8World;
struct W8ReadLevelInfo;
class W8SoundEvent;

class stLight;
class stMeshModel;
class stParticle;
class stGroundShadow;
struct W8AnimObj;
struct W8AniMesh;
struct W8PathAI;

// Reusable animation event data. Starting a shake copies this definition;
// neither the definition nor its caller owns a running instance.
struct ShakeDefinition {
    ShakeDefinition(float duration, bool preset, float intensity, float distance_cap,
                    const glm::vec3* position);
    float duration;
    float intensity;
    float distance_cap;
    glm::vec3 position{0, 0, 0};
    bool limit_distance = false;
    bool quadratic_falloff = false;
    bool fade_out = false;
    bool fade_in = false;
    int cycle = 0;
    int frame = 0;
    int subcycle = 0;
    std::function<void()> completion_callback;
};

class ShakeSystem {
public:
    // A key identifies a reusable definition that can be restarted/stopped.
    // Unkeyed instances run once. The system exclusively owns every instance.
    void Start(const ShakeDefinition&, const ShakeDefinition* key = nullptr);
    void Stop(const ShakeDefinition& key);
    bool IsActive(const ShakeDefinition& key) const;
    float Update(const glm::vec3& camera);

private:
    struct Instance {
        explicit Instance(const ShakeDefinition& definition, const ShakeDefinition* key);
        ShakeDefinition definition;
        W8GameTimer timer;
        const ShakeDefinition* key;
        bool Evaluate(const glm::vec3& camera, float& amount);
    };
    std::vector<std::unique_ptr<Instance>> active;
};

ShakeSystem& CameraShakes();
void UpdateShakeEffects();

class W8GrCycleParticleAttachment {
public:
    void SelectCycle(signed char selected_cycle);

    int cycle;
    signed char subcycle;
    std::unique_ptr<stParticle> m_pstParticles;
    glm::vec3 position;
    /* 0x004A7E50 composes this into the model instance's own rotation with
       MultiplyBy, which is what makes it a matrix rather than 0x24
       opaque bytes. */
    glm::mat3 rotation;
};

class W8GrCycle : public W8Navigator {
public:
    explicit W8GrCycle(W8World* owner = nullptr);
    // Borrowed lifetime: the world destroys its cycles before its scene.
    W8GrCycle(const W8GrCycle& other) : W8GrCycle(other, other.owner_world) {}
    W8GrCycle(const W8GrCycle& other, W8World* owner); /* 0x004A5F20 */
    W8GrCycle& operator=(const W8GrCycle&) = delete;
    virtual ~W8GrCycle() override;

    void AddSoundEvent(std::unique_ptr<W8SoundEvent> event);
    void SetLocation(const glm::vec3* new_location);
    glm::vec3 GetLocation() const;
    glm::vec3 GetLocalLocation() const;
    void SetRotation(const glm::mat3* new_rotation);
    glm::mat3 GetRotation() const;
    void SetFrameMethod(W8AnimationFrameMethod method);

    virtual srModelInstance* SetCycleFrameLod(signed char cycle, signed char frame,
                                              signed char lod) = 0;
    virtual unsigned int ApplyEmitterSetting(signed char emitter) = 0;
    virtual W8AniMesh* GetEmitterAniMesh(signed char emitter) = 0;

    // Gameplay despawning lets active particles finish; level destruction does not.
    void RetireParticles(W8World& world);
    virtual unsigned char CanEnterCycle(signed char) { return 1; }
    virtual void TickAnimation(float scale);           /* 0x004A6E20 */
    virtual unsigned char ApplyPendingCycle();         /* 0x004A6FC0 */
    void SetPresentationColour(glm::vec4 colour);
    virtual void UpdateRepresentation(W8World* world); /* 0x004A7470 */
    virtual signed char GetNumSubCycles() = 0;
    virtual bool IsCycleSupported(signed char cycle) = 0;
    virtual signed char GetTotalAnimationCount() = 0;
    virtual float GetCurrentAnimationScale() = 0;
    void SetCyclePosition(glm::vec3* position);
    /* Registry-wide lookups answered from this cycle's identity. */
    std::string_view GetRegisteredName() const;
    bool IsSoleRegisteredCycleForName() const; /* 0x004A8700 */
    virtual std::optional<srBoundingBox> GetAnimationBounds();
    virtual unsigned char GetAnimationRadius(float* radius);
    virtual void SetCycle(signed char cycle) = 0;
    virtual W8AnimObj* GetCurrentAnimation() = 0;
    virtual void AdvanceAnimationFrame(int value, int flags);
    virtual W8AniMesh* GetCurrentAniMesh() = 0;

    void SetSubCycle(unsigned char subcycle);
    void SetBehaviour(W8AnimationBehaviour bBehaviour);
    void SetLights(std::vector<std::unique_ptr<stLight>>* lights);
    void DetachCycleLights(std::vector<std::unique_ptr<stLight>>* lights);
    void AttachCycleLights(std::vector<std::unique_ptr<stLight>>* lights);
    void AddShakeEffect(ShakeDefinition effect);
    void CreateGroundShadow(float width, float depth);
    void SetGroundShadowVisible(bool visible);
    void ResetRepresentation();
    void DetachRepresentation(W8World* world);
    /* Runs at the end of every representation update; its own body is the
       shake/particle event walk. */
    void UpdateParticleAttachments();
    void SelectLOD(const glm::vec3* position); /* 0x004A7BE0 */
    void UpdateLights();
    srModelInstance* SelectCycleFrameLod(signed char cycle, signed char frame, signed char lod);
    srModelInstance* GetCurrentModelInstance();
    bool UsesCurrentModelInstance();
    unsigned char ReplacePath(W8PathAI* path);
    void SubmitTargetValue();

public:
    std::unique_ptr<W8PathAI> m_pAI;
    std::vector<std::unique_ptr<W8SoundEvent>> sound_events;
    glm::vec3 location{0.0f};
    glm::vec3 local_location{0.0f};
    glm::vec3 parent_location{0.0f};
    glm::mat3 rotation{1.0f};
    glm::vec4 highlight_colour{0.0f};
    float instance_scale = 1.0f;
    bool apply_instance_scale = false;
    unsigned char subcycle = 0;
    unsigned short pending_subcycle = 0xffff;
    unsigned int timer = 0;
    unsigned char rep_active = 0;
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

    srModelInstance* current_model_instance;
    std::vector<std::unique_ptr<stLight>>* m_plsLights;                  /* 0x1ac */
    std::deque<ShakeDefinition> shake_events;
    /* 0x1b5: the subcycle the last update pass left on the representation. */
    unsigned char last_subcycle;
    std::vector<W8GrCycleParticleAttachment> particles;
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
    std::unique_ptr<stGroundShadow> m_ground_shadow;
    /* Fractional frame progress after TickAnimation consumes whole frames.
       Monster interpolation and light definition time use the same fraction. */
    float frame_fraction;
}; /* 0x1d8 */

W8GrCycle* FindFirstGrCycleByName(std::string_view name);
unsigned char UnregisterGrCycle(W8GrCycle* cycle);
void RegisterGrCycle(std::string_view name, W8GrCycle* cycle);
bool LoadGrCycle(std::string_view mon_name, W8GrCycle* cycle, int cycle_index, int value,
                 std::string_view directory, unsigned char object_type,
                 std::string_view bitmap_directory = {});
unsigned char ReadGrCycleData(W8ReadLevelInfo* info, W8GrCycle* cycle, int cycle_index, int value,
                              unsigned char object_type);
int FindMappedIndexInMeshChain(stMeshModel** mesh, int key); /* 0x004A8D10 */

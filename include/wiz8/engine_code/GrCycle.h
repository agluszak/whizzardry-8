#pragma once

#include <cstdint>
#include <optional>
#include "surrender/srBoundingBox.h"
#include <string_view>

/* Engine Code\GrCycle.cpp owns these declarations. The two bases keep their
   original relative order: W8GrObject is
   W8GrCycle's first base and W8Navigator its second. */

#include <vector>

#include "surrender/srMath.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/Navigator.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"

class srModelInstance;
struct W8World;
struct W8ReadLevelInfo;
class W8EmitterHost;

/* Callers build this trio before loading a .mon/.mls resource. The loader
   carries world into its four-field read record and never reads the other
   two, but callers still store into them: monster callers null
   bitmap_directory and set directory to "Data\\Monsters", while the
   spell factory points directory at the spell-bitmap tree. */
struct W8GrCycleLoadContext {
    W8World* world;
    const char* bitmap_directory;
    const char* directory;
};

class stLight;
class stMeshModel;
class stParticle;
class stGroundShadow;
struct W8AnimObj;
struct W8AniMesh;
struct W8PathAI;

void DestroyLightVector(std::vector<stLight*>* vector); /* 0x004A8C50 */

/* Live-list membership and ownership are independent: Trigger retains its
   effect and clears LIST_OWNS before the live updater can release it. */
class W8CameraShakeEffect {
public:
    struct Flags {
        bool active : 1;
        bool list_owns : 1;
        bool limit_distance : 1;
        bool quadratic_falloff : 1;
        bool fade_out : 1;
        bool fade_in : 1;
        std::uint8_t reserved : 2;
        std::uint8_t reserved_bytes[3];
    };

    W8CameraShakeEffect(float duration, bool preset, float intensity, float distance_cap,
                        const srVector3T<float>* position); /* 0x004ADED0 */
    W8CameraShakeEffect(const W8CameraShakeEffect& other);  /* 0x004AE000 */
    /* Per-frame evaluation: answers whether the effect is still active and
       reports how much it contributes this frame. */
    std::uint8_t Evaluate(const srVector3T<float>* position, float* out_amount);

    Flags flags;                /* 0x00: runtime effect and list state */
    float intensity;            /* 0x04 */
    float distance_cap;         /* 0x08: radius for LIMIT_DISTANCE effects */
    srVector3T<float> position; /* 0x0c */
    W8GameTimer timer;          /* 0x18 */
    /* The key 0x004AE170 matches an animation event against. */
    std::int32_t cycle;                         /* 0x3c */
    std::int32_t frame;                         /* 0x40 */
    std::int32_t subcycle;                      /* 0x44 */
    void (*completion_callback)(void); /* 0x48 */
};

/* The live list every active effect is on, and the timer the first effect
   creates alongside it. Both are built lazily by the constructor. */
extern std::vector<W8CameraShakeEffect*>* g_shake_effects;
extern W8GameTimer* g_shake_timer;
extern const float g_float_two_hundred_fifty;

W8CameraShakeEffect* CreateCameraShakeEffect(float duration, bool preset, float intensity,
                                             float distance_cap, const srVector3T<float>* position);
/* Fire every effect in one cycle's vector whose key matches, moving it onto the
   live list and restarting its timer. */
void TriggerShakeEffects(std::vector<W8CameraShakeEffect*>* effects, std::int32_t cycle, std::uint32_t frame,
                         std::int32_t subcycle, const srVector3T<float>* position);
/* Take every active effect in one cycle's vector back off the live list, and
   release the ones that list owned. */
void StopShakeEffects(std::vector<W8CameraShakeEffect*>* effects);
/* The per-frame shake update: retire finished live effects and turn the
   accumulated intensity into Trigger's action camera offset. */
void UpdateShakeEffects();

/* 0x004A5F20 allocates 0x3c for each of these and copies them field by field:
   a leading dword, the byte after it, an owned stParticle rebuilt through
   0x00498180, a vector at +0x0c, and the 0x24-byte tail wholesale. */
class W8GrCycleParticleAttachment {
public:
    void SelectCycle(std::int8_t selected_cycle);

    std::int32_t cycle;
    std::int8_t subcycle;
    std::uint8_t padding_05[3];
    stParticle* m_pstParticles;
    srVector3T<float> position;
    /* 0x004A7E50 composes this into the model instance's own rotation with
       MultiplyBy, which is what makes it a matrix rather than 0x24
       opaque bytes. */
    glm::mat3 rotation;
};

class W8GrCycle : public W8GrObject, public W8Navigator {
public:
    W8GrCycle();
    W8GrCycle(const W8GrCycle& other); /* 0x004A5F20 */
    virtual ~W8GrCycle() override;
    // FUNCTION: WIZ8 0x004a7140
    virtual std::uint8_t CanEnterCycle(std::int8_t)
    {
        return 1;
    }
    virtual void TickAnimation(float scale);           /* 0x004A6E20 */
    virtual std::uint8_t ApplyPendingCycle();         /* 0x004A6FC0 */
    virtual void UpdateRepresentation(W8World* world); /* 0x004A7470 */
    virtual std::int8_t GetNumSubCycles() = 0;
    virtual bool IsCycleSupported(std::int8_t cycle) = 0;
    virtual std::int8_t GetTotalAnimationCount() = 0;
    virtual float GetCurrentAnimationScale() = 0;
    /* The concrete cycle owns the representation placed in GrObject::m_pRep.
       This typed view supplies animation, particles, lights, and model state;
       neither base constructs or deletes that pointer. */
    virtual W8EmitterHost* GetRepresentation() = 0;
    void SetCyclePosition(srVector3T<float>* position);
    /* Registry-wide lookups answered from this cycle's identity. */
    const char* GetRegisteredName() const;     /* 0x004A8650 */
    bool IsSoleRegisteredCycleForName() const; /* 0x004A8700 */
    virtual std::optional<srBoundingBox> GetAnimationBounds();
    virtual std::uint8_t GetAnimationRadius(float* radius);
    virtual void SetCycle(std::int8_t cycle) = 0;
    virtual W8AnimObj* GetCurrentAnimation() = 0;
    virtual void AdvanceAnimationFrame(std::int32_t value, std::int32_t flags);
    virtual W8AniMesh* GetCurrentAniMesh() = 0;

    void SetSubCycle(std::uint8_t subcycle);
    void SetBehaviour(std::int8_t bBehaviour);
    void SetLights(std::vector<stLight*>* lights);
    void DetachCycleLights(std::vector<stLight*>* lights);
    void AttachCycleLights(std::vector<stLight*>* lights);
    void AddShakeEffect(W8CameraShakeEffect* effect);
    void CreateGroundShadow(float width, float depth);
    void SetGroundShadowVisible(bool visible);
    void ResetRepresentation();
    void DetachRepresentation(W8World* world);
    /* Runs at the end of every representation update; its own body is the
       shake/particle event walk. */
    void UpdateParticleAttachments();
    void SelectLOD(const srVector3T<float>* position); /* 0x004A7BE0 */
    void UpdateLights();
    srModelInstance* SelectCycleFrameLod(std::int8_t cycle, std::int8_t frame, std::int8_t lod);
    srModelInstance* GetCurrentModelInstance();
    std::uint8_t ReplacePath(W8PathAI* path);
    void SubmitTargetValue();

public:
    srModelInstance* current_model_instance;
    std::vector<stLight*>* m_plsLights;                  /* 0x1ac */
    std::vector<W8CameraShakeEffect*>* m_plsShakeEvents; /* 0x1b0 */
    bool m_fDeleteLights;                             /* 0x1b4: named by GrCycle.cpp:1656 */
    /* 0x1b5: the subcycle the last update pass left on the representation. */
    std::uint8_t last_subcycle;
    std::uint8_t padding_1b6[2];
    std::vector<W8GrCycleParticleAttachment*>* m_plsParticles; /* 0x1b8 */
    /* 0x1bc: set when the frame walk wrapped to first_frame; suppresses the
       per-subcycle light reset. */
    bool wrapped;
    bool enabled;
    /* 0x1be: mirror the model on X (the left-handed strike pick). */
    bool mirror_x;
    /* 0x1bf: m_axis holds an aim point; mode-3 particles orient along it. */
    bool aim_set;
    /* The axis 0x004A7E50 aims a mode-three particle along. */
    srVector3T<float> m_axis;
    float scale;
    stGroundShadow* m_ground_shadow; /* 0x1d0: typed runtime class stGroundShadow */
    /* Fractional frame progress after TickAnimation consumes whole frames.
       Monster interpolation and light definition time use the same fraction. */
    float frame_fraction;
}; /* 0x1d8 */

/* Secondary vftable 0x005eceb8 places the W8Navigator subobject at +0x18. */

/* 0x005EC128: hundredth-second scale shared by the monster and trigger
   durations. */
extern const float g_float_one_thousandth;

W8GrCycle* FindFirstGrCycleByName(std::string_view name);
std::uint8_t UnregisterGrCycle(W8GrCycle* cycle);
void RegisterGrCycle(const char* name, W8GrCycle* cycle);
bool LoadGrCycle(const W8GrCycleLoadContext* context, const char* mon_name, W8GrCycle** cycle,
                 std::int32_t cycle_index, std::int32_t value, const char* directory, std::uint8_t object_type,
                 const char* bitmap_directory = 0);
std::uint8_t ReadGrCycleData(W8ReadLevelInfo* info, W8GrCycle** cycle, std::int32_t cycle_index, std::int32_t value,
                              std::uint8_t object_type);
std::int32_t FindMappedIndexInMeshChain(stMeshModel** mesh, std::int32_t key); /* 0x004A8D10 */

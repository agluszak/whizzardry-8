#pragma once

#include <deque>
#include <optional>
#include <memory>
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

// Reusable animation event data. Starting a shake copies this definition;
// neither the definition nor its caller owns a running instance.
struct ShakeDefinition {
    ShakeDefinition(float duration, bool preset, float intensity, float distance_cap,
                    const srVector3T<float>* position);
    float duration;
    float intensity;
    float distance_cap;
    srVector3T<float> position{0, 0, 0};
    bool limit_distance = false;
    bool quadratic_falloff = false;
    bool fade_out = false;
    bool fade_in = false;
    int cycle = 0;
    int frame = 0;
    int subcycle = 0;
    void (*completion_callback)() = nullptr;
};

class ShakeSystem {
public:
    // A key identifies a reusable definition that can be restarted/stopped.
    // Unkeyed instances run once. The system exclusively owns every instance.
    void Start(const ShakeDefinition&, const ShakeDefinition* key = nullptr);
    void Stop(const ShakeDefinition& key);
    bool IsActive(const ShakeDefinition& key) const;
    float Update(const srVector3T<float>& camera);
private:
    struct Instance {
        explicit Instance(const ShakeDefinition& definition, const ShakeDefinition* key);
        ShakeDefinition definition;
        W8GameTimer timer;
        const ShakeDefinition* key;
        bool Evaluate(const srVector3T<float>& camera, float& amount);
    };
    std::vector<std::unique_ptr<Instance>> active;
};

ShakeSystem& CameraShakes();
extern const float g_float_two_hundred_fifty;
void UpdateShakeEffects();

/* 0x004A5F20 allocates 0x3c for each of these and copies them field by field:
   a leading dword, the byte after it, an owned stParticle rebuilt through
   0x00498180, a vector at +0x0c, and the 0x24-byte tail wholesale. */
class W8GrCycleParticleAttachment {
public:
    void SelectCycle(signed char selected_cycle);

    int cycle;
    signed char subcycle;
    unsigned char padding_05[3];
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
    virtual unsigned char CanEnterCycle(signed char)
    {
        return 1;
    }
    virtual void TickAnimation(float scale);           /* 0x004A6E20 */
    virtual unsigned char ApplyPendingCycle();         /* 0x004A6FC0 */
    virtual void UpdateRepresentation(W8World* world); /* 0x004A7470 */
    virtual signed char GetNumSubCycles() = 0;
    virtual bool IsCycleSupported(signed char cycle) = 0;
    virtual signed char GetTotalAnimationCount() = 0;
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
    virtual unsigned char GetAnimationRadius(float* radius);
    virtual void SetCycle(signed char cycle) = 0;
    virtual W8AnimObj* GetCurrentAnimation() = 0;
    virtual void AdvanceAnimationFrame(int value, int flags);
    virtual W8AniMesh* GetCurrentAniMesh() = 0;

    void SetSubCycle(unsigned char subcycle);
    void SetBehaviour(signed char bBehaviour);
    void SetLights(std::vector<stLight*>* lights);
    void DetachCycleLights(std::vector<stLight*>* lights);
    void AttachCycleLights(std::vector<stLight*>* lights);
    void AddShakeEffect(ShakeDefinition effect);
    void CreateGroundShadow(float width, float depth);
    void SetGroundShadowVisible(bool visible);
    void ResetRepresentation();
    void DetachRepresentation(W8World* world);
    /* Runs at the end of every representation update; its own body is the
       shake/particle event walk. */
    void UpdateParticleAttachments();
    void SelectLOD(const srVector3T<float>* position); /* 0x004A7BE0 */
    void UpdateLights();
    srModelInstance* SelectCycleFrameLod(signed char cycle, signed char frame, signed char lod);
    srModelInstance* GetCurrentModelInstance();
    unsigned char ReplacePath(W8PathAI* path);
    void SubmitTargetValue();

public:
    srModelInstance* current_model_instance;
    std::vector<stLight*>* m_plsLights;                  /* 0x1ac */
    std::deque<ShakeDefinition> shake_events;
    bool m_fDeleteLights;                             /* 0x1b4: named by GrCycle.cpp:1656 */
    /* 0x1b5: the subcycle the last update pass left on the representation. */
    unsigned char last_subcycle;
    unsigned char padding_1b6[2];
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
unsigned char UnregisterGrCycle(W8GrCycle* cycle);
void RegisterGrCycle(const char* name, W8GrCycle* cycle);
bool LoadGrCycle(const W8GrCycleLoadContext* context, const char* mon_name, W8GrCycle** cycle,
                 int cycle_index, int value, const char* directory, unsigned char object_type,
                 const char* bitmap_directory = 0);
unsigned char ReadGrCycleData(W8ReadLevelInfo* info, W8GrCycle** cycle, int cycle_index, int value,
                              unsigned char object_type);
int FindMappedIndexInMeshChain(stMeshModel** mesh, int key); /* 0x004A8D10 */

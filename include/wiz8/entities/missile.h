#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/cycle_resources.h"


#include "compat/compiler.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/rules/targeting.h"

struct LevelLoadContext;
struct World;
class Light;
class Missile;
struct Animation;

enum W8MissileAnimationStateQuery {
    W8_MISSILE_ANIMATION_EMITTER_SETTING = 0,
    W8_MISSILE_ANIMATION_TOTAL_COUNT = 1,
    W8_MISSILE_ANIMATION_AT_FINAL_SUBCYCLE = 2,
    W8_MISSILE_ANIMATION_AT_INITIAL_SUBCYCLE = 3,
    W8_MISSILE_ANIMATION_SUBCYCLE_INDEX = 4,
    W8_MISSILE_ANIMATION_HAS_EMITTER = 5,
    W8_MISSILE_ANIMATION_CYCLE_INDEX = 6,
    W8_MISSILE_ANIMATION_FINISHED = 7
};

struct MissileFlight {
    MissileFlight() = default;

    /* Gravity latch copied from the missile's gravity: when set, the
       vertical fall rate decays each step. */
    bool gravity{};
    /* Per-step advance scale (speed units per step tick). */
    float speed_per_step{};
    /* Current vertical fall rate, seeded from launch pitch and decayed by
       gravity. */
    float fall_speed{};
    Missile* missile{};
    /* Half-tick baseline (milliseconds()>>1) the delta against the current
       half-tick is clamped to 0xfa. */
    int last_half_tick{};
    float elapsed{};
    float limit{};
    bool unknown_1c{};
};

unsigned char UpdateMissileAI(MissileFlight* record);
float AdvanceMissileAI(MissileFlight* record, glm::vec3* out, unsigned int steps);

/* The copy path allocates 0x328 bytes and invokes CyclePlayer's copy constructor
   on the same receiver before installing the missile's primary and navigator
   tables. The source assertion for its AI names the object `pMissile`. */
class Missile {
public:
    World* const owner_world;
    Navigator nav;
    CyclePlayer visual;
    std::string asset_name;
    explicit Missile(World* owner = nullptr);
    void TickAnimation(float scale);
    bool ApplyPendingCycle();
    void SetCyclePosition(glm::vec3* position);
    Missile(const Missile& other) : Missile(other, other.owner_world) {}
    Missile(const Missile& other, World* owner); /* 0x004A3E50 */
    Missile& operator=(const Missile&) = delete;
    ~Missile();

    signed char GetNumSubCycles();
    bool IsCycleSupported(signed char cycle);
    signed char GetTotalAnimationCount();
    float GetCurrentAnimationScale();
    void SetCycle(signed char cycle);
    Animation* GetCurrentAnimation();

    AnimationMesh* GetCurrentAniMesh();
    MeshInstance* SetCycleFrameLod(signed char emitter, signed char frame, signed char lod);
    unsigned int ApplyEmitterSetting(signed char emitter);
    AnimationMesh* GetEmitterAniMesh(signed char emitter);
    unsigned char ReadCycleData(LevelLoadContext* info, int cycle_index, int);
    void StartIfHostActive(); /* 0x004A4050 */
    /* Decide what the missile struck - the party or a live monster - record it
       in combat_slot, roll the target's missile deflection and either hand
       the hit to Combat Attack.cpp or leave it for the combat engine. */
    bool OnCollision(Navigator* other); /* 0x004A4720 */

    w8_ulong GetAnimationState(W8MissileAnimationStateQuery query);
    void DetonateMissileSpell();
    void AnnounceCollisionTarget(); /* 0x004A4AC0 */
    /* Switch the representation to its impact cycle, or end the flight when the
       missile has no such cycle. */
    void EnterImpactCycle(); /* 0x004A4C20 */

    void SetEffectDefinition(const W8SpellEffectDefinition* definition); /* 0x004A5410 */
    /* 0x004A5790: true while this in-flight missile still blocks ending combat. */
    bool BlocksEndingCombat();

public:
    int missile_table_index;
    EmitterResources<2> resources;
    std::optional<MissileFlight> flight_ai;
    /* Flight exhausted its duration; the world updater destroys the missile
       once block_released is also set. */
    bool flight_done;
    /* Set by EnterImpactCycle while the impact animation plays. */
    bool impacting;
    /* Set once the missile no longer blocks combat end (BlocksEndingCombat
       returned 0 or combat already resolved). */
    unsigned char block_released;
    bool gravity;
    bool align_camera;
    bool explode_ground;
    bool align_explosion;
    bool flag;
    float lifetime;
    int flags0;
    float duration;
    W8SpellEffectDefinition definition;
    W8TargetSource m_Source;
    W8CombatSlot combat_slot;
    /* 0x280: the damage and condition results this missile has accumulated,
       folded into the owning spell effect by 0x00500460. */
    W8SpellEffectResult result;
    bool retargeted; /* the missile struck something other than its intended target */
};

/* Secondary vftable 0x005ecdf4 keeps the Navigator subobject at +0x18. */

Missile* FireMissile(unsigned int missile_table_index, glm::vec3* source, glm::vec3* target,
                     float flight_speed, unsigned int trace_mask, unsigned int block_released,
                     float duration);
/* The world position `character_index`'s current hand fires a missile from:
   the camera's launch point swung to the wielding side. */
void GetCharacterProjectilePosition(unsigned int character_index, glm::vec3* position);



Missile* NextMissile(bool restart);

Missile* AllocateMissile(int missile_table_index);
std::unique_ptr<Missile> LoadMissileCycle(World* world, std::string_view name);

Missile* CreateMissile(unsigned int missile_table_index, glm::vec3* source, float heading,
                       float pitch, float flight_speed, unsigned int trace_mask,
                       unsigned char block_released, float duration);
void UpdateWorldMissiles(World* world);

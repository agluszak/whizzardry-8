#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::uint8_t LoadMissileDatabase(void);
void ReleaseMissileDatabase(void);

#include "wiz8/attack_modes.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/local_code/SpellEffect.h"
#include "Emitter.h"
#include "compat/compiler.h"
#include "layouts/targeting.h"

struct W8ReadLevelInfo;
struct W8World;
class stLight;
class W8Missile;
struct W8AnimObj;
template <class T> class srVector3T;

/* The kind-3 record of the tagged AI family W8GrObject holds at +0x0c (the
   path record is kind 0). UpdateMissileAI ticks it: +0x04 is the
   per-step distance scale, +0x08 the remaining turn budget, +0x0c the missile
   it steers (runtime clones start detached), +0x10 the
   half-tick baseline, +0x14 the elapsed flight clock, +0x18 the early-impact
   limit and +0x1c a trailing flag. */
struct W8AIMissile : W8AIRecord {
    W8AIMissile() : W8AIRecord(W8_AI_RECORD_MISSILE) {}
    std::unique_ptr<W8AIRecord> Clone() const override;

    /* Gravity latch copied from the missile's gravity: when set, the
       vertical fall rate decays each step. */
    bool gravity{};
    /* Per-step advance scale (speed units per step tick). */
    float speed_per_step{};
    /* Current vertical fall rate, seeded from launch pitch and decayed by
       gravity. */
    float fall_speed{};
    W8Missile* missile{};
    /* Half-tick baseline (milliseconds()>>1) the delta against the current
       half-tick is clamped to 0xfa. */
    std::int32_t last_half_tick{};
    float elapsed{};
    float limit{};
    bool unknown_1c{};
    std::uint8_t padding_1d[3];
};

bool UpdateMissileAI(W8AIMissile* record);
float AdvanceMissileAI(W8AIMissile* record, srVector3T<float>* out, std::uint32_t steps);

/* The missile constructor allocates this complete 0x108-byte representation,
   invokes W8EmitterHost on the same receiver, constructs the two light-list
   vectors, and installs vtable 0x005ECDE0. Its copy constructor and destructor
   at 0x004A2DB0 and 0x004A3230 own the same storage. */
class W8MissileRep : public W8EmitterHost {
public:
    W8MissileRep();
    W8MissileRep(const W8MissileRep& other);
    virtual ~W8MissileRep() override;
    virtual W8AnimRepBase* Clone() override;
    virtual srModelInstance* SetCycleFrameLod(std::int8_t cycle, std::int8_t frame,
                                              std::int8_t lod) override;
    virtual std::uint32_t ApplyEmitterSetting(std::int8_t emitter) override;
    virtual W8AniMesh* GetEmitterAniMesh(std::int8_t emitter) override;
    std::uint8_t ReadCycleData(W8ReadLevelInfo* info, W8Missile* missile, std::int32_t cycle_index, std::int32_t);

    std::uint32_t value_0ac;
    std::uint32_t value_0b0;
    std::uint8_t unknown_0b4[0x24];
    W8AnimObj* emitters[2];
    float emitter_playback_scales[2];
    std::vector<std::vector<stLight*>*> light_lists[2];
};

/* The copy path allocates 0x328 bytes and invokes W8GrCycle's copy constructor
   on the same receiver before installing the missile's primary and navigator
   tables. The source assertion for its AI names the object `pMissile`. */
class W8Missile : public W8GrCycle {
public:
    W8Missile();
    W8Missile(const W8Missile& other); /* 0x004A3E50 */
    virtual ~W8Missile() override;

    virtual void UpdateRepresentation(W8World* world) override;
    virtual std::int8_t GetNumSubCycles() override;
    virtual bool IsCycleSupported(std::int8_t cycle) override;
    virtual std::int8_t GetTotalAnimationCount() override;
    virtual float GetCurrentAnimationScale() override;
    virtual W8EmitterHost* GetRepresentation() override;
    virtual void SetCycle(std::int8_t cycle) override;
    virtual W8AnimObj* GetCurrentAnimation() override;
    virtual void AdvanceAnimationFrame(std::int32_t value, std::int32_t flags) override;
    virtual W8AniMesh* GetCurrentAniMesh() override;
    virtual void StartIfHostActive(); /* 0x004A4050 */
    /* Decide what the missile struck - the party or a live monster - record it
       in combat_slot, roll the target's missile deflection and either hand
       the hit to Combat Attack.cpp or leave it for the combat engine. */
    virtual bool OnCollision(W8Navigator* other) override; /* 0x004A4720 */

    std::uint32_t GetAnimationState(std::int32_t mode);
    void DetonateMissileSpell();
    void DestroyMissile();          /* 0x004A4180 */
    void AnnounceCollisionTarget(); /* 0x004A4AC0 */
    /* Switch the representation to its impact cycle, or end the flight when the
       missile has no such cycle. */
    void EnterImpactCycle(); /* 0x004A4C20 */

    void SetEffectDefinition(const W8SpellEffectDefinition* definition); /* 0x004A5410 */
    /* 0x004A5790: true while this in-flight missile still blocks ending combat. */
    bool BlocksEndingCombat();

public:
    std::int32_t missile_table_index;
    /* Assertion-backed original spelling. Distinct from GrObject::m_pRep at
       +0x14; GetRepresentation() returns this GrCycle-tail slot at +0x1dc. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow-field"
#endif
    W8MissileRep* m_pRep;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    /* Flight exhausted its duration; the world updater destroys the missile
       once block_released is also set. */
    bool flight_done;
    /* Set by EnterImpactCycle while the impact animation plays. */
    bool impacting;
    /* Set once the missile no longer blocks combat end (BlocksEndingCombat
       returned 0 or combat already resolved). */
    std::uint8_t block_released;
    bool gravity;
    bool align_camera;
    bool explode_ground;
    bool align_explosion;
    bool flag;
    void* value_1e8;
    void* value_1ec;
    float lifetime;
    std::int32_t flags0;
    float duration;
    W8SpellEffectDefinition definition;
    W8TargetSource m_Source;
    W8CombatSlot combat_slot;
    /* 0x280: the damage and condition results this missile has accumulated,
       folded into the owning spell effect by 0x00500460. */
    W8SpellEffectResult result;
    bool retargeted; /* the missile struck something other than its intended target */
    std::uint8_t padding_323[5];
};

/* Secondary vftable 0x005ecdf4 keeps the W8Navigator subobject at +0x18. */

W8Missile* FireMissile(std::uint32_t missile_table_index, srVector3T<float>* source,
                       srVector3T<float>* target, float flight_speed, std::uint32_t trace_mask,
                       std::uint32_t block_released, float duration);
/* The world position `character_index`'s current hand fires a missile from:
   the camera's launch point swung to the wielding side. */
void GetCharacterProjectilePosition(std::uint32_t character_index, srVector3T<float>* position);
void DestroyAllMissiles(W8World* world); /* 0x004A4210 */

extern std::uint32_t g_missile_table_count;

/* One 0x1e5-byte MissileTables.dbs runtime row. Only the fields reached by
   recovered consumers are named. */
struct W8MissileTableRecord {
    /* The retained database row starts with a UTF-16 name, independently
       visible in all 36 canonical MissileTables.dbs rows. */
    std::string display_name;
    /* Retail offset 0x100: the GrCycle resource name the launcher loads through the
       "Data\\Missiles" script path. */
    std::string cycle_name;
    float radius;             /* Retail offset 0x140: replaces the launched effect's radius */
    W8AttackMode attack_mode; /* Retail offset 0x144: the attack mode the hit is resolved with */
    std::uint8_t unknown_148[8];
    /* Retail offset 0x150: copied into the launched effect block's magnitude_base. */
    std::int32_t magnitude_base;
    /* Retail offset 0x154: nonzero marks a spell missile - its hits resolve through
       ResolveSpellMissileHit instead of the physical hit/deflect path. */
    bool spell_missile;
    /* Retail offset 0x155: the percentage chances the missile's hit effect assigns each
       condition; CastSpellFromSource copies them into its effect block. */
    std::uint8_t condition_chances[W8_ATTACK_EFFECT_COUNT];
    /* Retail offset 0x165: the missile weapon's impact sound class; MakePCHitSound bounds it
       against the 28 material-impact rows. */
    std::int32_t weapon_sound_class;
    std::uint8_t unknown_169[0x7c];
};

extern W8MissileTableRecord* g_missile_table;

W8Missile* NextMissile(bool restart);

W8Missile* AllocateMissile(std::int32_t missile_table_index);
std::uint8_t LoadMissileCycle(W8GrCycleLoadContext* context, const char* name,
                               W8Missile** ppMissile, std::int32_t);

W8Missile* CreateMissile(std::uint32_t missile_table_index, srVector3T<float>* source, float heading,
                         float pitch, float flight_speed, std::uint32_t trace_mask,
                         std::uint8_t block_released, float duration);
void UpdateWorldMissiles(W8World* world);

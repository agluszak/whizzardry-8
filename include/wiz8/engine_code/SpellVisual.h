#pragma once

#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/SpellEmitterHost.h"

/* The four spell-visual modes double as the four seven-cycle groups of
   g_spell_cycle_names: the value selects which .mls rows a visual loads and
   how UpdateRepresentation anchors it. FLASH visuals stay on the camera,
   EXPLOSION visuals are caller-positioned billboards, TARGET visuals anchor
   to a monster and scale to its animation bounds, and CONE visuals follow
   the monster or camera unless a fixed caller transform was supplied. */
enum W8SpellVisualMode {
    W8_SPELL_VISUAL_NONE = -1,
    W8_SPELL_VISUAL_FLASH = 0,
    W8_SPELL_VISUAL_EXPLOSION = 1,
    W8_SPELL_VISUAL_TARGET = 2,
    W8_SPELL_VISUAL_CONE = 3
};

/* The 28 named bitmap cycles a spell visual carries, spelled by
   Spells.cpp's own assertions: four groups of SPELL_CYCLES_PER_GROUP rows
   indexed by g_spell_cycle_names. The cast's power level picks the row
   within the group. */
enum {
    SPELL_CYCLE_FIRST = 0,
    SPELL_CYCLE_LAST = 27,
    SPELL_NUM_CYCLES = 28,
    SPELL_CYCLES_PER_GROUP = 7
};

/* Engine Code\Spells.cpp's 0x1f8-byte GrCycle specialization.  Construction
   installs the primary 0x005ECF40 table and the ordinary W8Navigator secondary
   table at +0x18, then owns one 0x37c-byte emitter host at +0x1e0. */
class W8SpellVisual : public W8GrCycle {
public:
    explicit W8SpellVisual(W8World* owner = nullptr);
    /* Cloned from a registered shared visual: keeps the cycle type, the saved
       flags and the idle pacing, restarts the transient state, and gets its
       own emitter host and identity counter. Inlined at the 0x004AB580 clone
       site; no out-of-line copy constructor exists in the image. */
    W8SpellVisual(const W8SpellVisual& other) : W8SpellVisual(other, other.owner_world) {}
    W8SpellVisual(const W8SpellVisual& other, W8World* owner);
    W8SpellVisual& operator=(const W8SpellVisual&) = delete;
    virtual ~W8SpellVisual() override;

    virtual void UpdateRepresentation(W8World* world) override;
    virtual signed char GetNumSubCycles() override;
    virtual bool IsCycleSupported(signed char cycle) override;
    virtual signed char GetTotalAnimationCount() override;
    virtual float GetCurrentAnimationScale() override;
    virtual W8EmitterHost* GetRepresentation() override;
    virtual void SetCycle(signed char cycle) override;
    virtual W8AnimObj* GetCurrentAnimation() override;
    virtual void AdvanceAnimationFrame(int value, int flags) override;
    virtual W8AniMesh* GetCurrentAniMesh() override;

    virtual void StartIfHostActive(); /* 0x004ABDC0 */
    /* Search backward from a subcycle for the first cycle this visual
       supports; returns -1 when none does. */
    virtual int FindSupportedCycle(int group, int subcycle);
    /* Kind-indexed query over the emitter host's current state; only ever
       called with kind 7 by StartIfHostActive. */
    int QueryHostStateByKind(int kind);

    /* Selects the update mode and the cycle group the resource loader
       matches shared visuals by. */
    W8SpellVisualMode mode;
    int value_1dc;
    std::unique_ptr<W8SpellEmitterHost> host = std::make_unique<W8SpellEmitterHost>();
    /* Set once the host's animation no longer needs ticking, or forced when
       the owning effect releases it; the world updater deletes a finished
       visual as soon as auto_release also permits it. */
    bool finished; /* 0x1e4 */
    bool flag;
    /* Held at 0 while a spell effect owns the visual; the releasing pass in
       Local Code\Magic.cpp sets it back to 1, which lets
       UpdateWorldSpellVisuals delete a finished visual. */
    bool auto_release;
    /* Set when the caller supplied the position and rotation directly;
       mode-3 updates then skip the monster/camera follow logic. */
    bool fixed_transform;
    float scale0;
    int location_id;
    int effect_value; /* 0x1f0: spawn `value` payload */
    int flags0;       /* 0x1f4 */
};

/* Secondary vftable 0x005ecf2c keeps the W8Navigator subobject at +0x18. */

inline W8SpellVisual::W8SpellVisual(const W8SpellVisual& other, W8World* owner)
    : W8GrCycle(other, owner)
{
    mode = other.mode;
    finished = false;
    flag = false;
    auto_release = other.auto_release;
    fixed_transform = other.fixed_transform;
    scale0 = other.scale0;
    location_id = 0;
    host = std::make_unique<W8SpellEmitterHost>(*other.host);
    id = AllocateGrObjectId();
}

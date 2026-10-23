#pragma once

#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"

/* The four spell-visual modes double as the four seven-cycle groups of
   g_spell_cycle_names: the value selects which .mls rows a visual loads and
   how UpdateRepresentation anchors it. FLASH visuals stay on the camera,
   EXPLOSION visuals are caller-positioned billboards, TARGET visuals anchor
   to a monster and scale to its animation bounds, and CONE visuals follow
   the monster or camera unless a fixed caller transform was supplied. */
enum W8SpellVisualMode : int {
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

enum class W8SpellVisualHostStateQuery : unsigned char {
    EmitterSetting,
    AnimationCount,
    AtLastSubcycle,
    IsFirstSubcycle,
    Subcycle,
    HasEmitterSetting,
    CurrentCycle,
    AnimationStopped,
};

/* What a spell visual's MLS and .mon files author for one cycle group, loaded
   once per world: the group's cycles with their lights outside any scene and
   the cycle events. */
struct SpellVisualDefinition {
    std::string asset_name;
    W8SpellVisualMode mode = W8_SPELL_VISUAL_NONE;
    CycleDefinition events;
    EmitterResources<28> resources;
};

/* Engine Code\Spells.cpp's 0x1f8-byte GrCycle specialization.  Construction
   installs the primary 0x005ECF40 table and the ordinary Navigator secondary
   table at +0x18, then owns one 0x37c-byte emitter host at +0x1e0. */
class SpellVisual {
public:
    World* const owner_world;
    Navigator nav;
    CyclePlayer visual;
    std::string asset_name;
    explicit SpellVisual(World* owner = nullptr);
    void TickAnimation(float scale);
    bool ApplyPendingCycle();
    void SetCyclePosition(glm::vec3* position);
    /* A new visual of a definition's cycle group, with its own cycles, set to
       play its next cycle once. */
    SpellVisual(const SpellVisualDefinition& definition, World* owner);
    SpellVisual(const SpellVisual&) = delete;
    SpellVisual& operator=(const SpellVisual&) = delete;
    ~SpellVisual();

    void UpdateRepresentation(World* world);
    signed char GetNumSubCycles();
    bool IsCycleSupported(signed char cycle);
    signed char GetTotalAnimationCount();
    float GetCurrentAnimationScale();
    void SetCycle(signed char cycle);
    Animation* GetCurrentAnimation();

    unsigned int ApplyEmitterSetting(signed char emitter);

    void StartIfHostActive(); /* 0x004ABDC0 */
    /* Search backward from a subcycle for the first cycle this visual
       supports; returns -1 when none does. */
    int FindSupportedCycle(int group, int subcycle);
    /* Typed query over the emitter host's current state. */
    int QueryHostState(W8SpellVisualHostStateQuery query);

    /* Selects the update mode and the cycle group the resource loader
       matches shared visuals by. */
    W8SpellVisualMode mode;
    EmitterResources<28> resources;
    /* Set for spawned visuals: UpdateRepresentation then adds the camera-facing yaw
       rotation on top of the mode's orientation. */
    bool billboard{};
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

/* Secondary vftable 0x005ecf2c keeps the Navigator subobject at +0x18. */

SpellVisual* CreateMonsterSpellEffect(std::string_view mls_name, int power_level, Monster* monster,
                                      int value, int flags); /* 0x004AD630 */
SpellVisual* CreateAttachedSpellEffect(std::string_view mls_name, int power_level, Monster* parent,
                                       int value, int flags); /* 0x004AD8A0 */
SpellVisual* CreateAimedSpellEffect(std::string_view mls_name, int power_level, glm::vec3* position,
                                    glm::mat3* rotation, int value, int flags); /* 0x004ADB20 */
void SetTargetConeEnabled(bool enabled);
SpellVisual* SpawnCameraSpellEffect(std::string_view name, int power_level, int value,
                                    int flags); /* 0x004AD080 */
/* Load one named visual from the spell bitmap directory; the out pointer is
   set only on success. */
std::unique_ptr<SpellVisual> LoadSpellVisualResource(World* world, std::string_view name,
                                                     W8SpellVisualMode group);
/* Create one spell visual from the spell's own record resource. Engine
   Code\Spells.cpp's factory, whose result the queued effect owns. */
SpellVisual* SpawnSpellEffect(const glm::vec3* position, std::string_view resource_name,
                              int power_level, int value, int flags); /* 0x004AD430 */

void UpdateWorldSpellVisuals(World* world);

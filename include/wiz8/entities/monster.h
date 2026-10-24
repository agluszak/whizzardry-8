#ifndef WIZ8_ENGINE_CODE_MONSTER_H
#define WIZ8_ENGINE_CODE_MONSTER_H

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>

#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "wiz8/audio_resources.h"
#include "wiz8/spatial/bounding_box.h"

struct KeyframeTrack;
struct World;
struct W8MonsterRecord;

#include <memory>
#include <string>
#include <vector>

#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/monster_spell_icons.h"
#include "wiz8/platform/game_timer.h"

struct Animation;
struct LevelLoadContext;
struct WorldItemEntity;
class Mesh;
class MeshInstance;
class Light;
class ParticleSystem;
class Script;
class Sound3D;
class Trigger;
class Monster;
struct MonsterDefinition;

enum W8MonsterFlag {
    W8_MONSTER_KEEP_FRAME_DIRECTION = 0x1,
    W8_MONSTER_TEXTURE_CHECKED = 0x2,
    W8_MONSTER_ANIMATED_TEXTURE = 0x4,
    W8_MONSTER_KEEP_SUBCYCLE = 0x10,
    W8_MONSTER_SCRIPT_WAIT = 0x20,
    W8_MONSTER_PARKED = 0x40,
    W8_MONSTER_REMOVE_AFTER_FADE = 0x100,
    W8_MONSTER_REMOVE_NOW = 0x200,
    W8_MONSTER_FADED_OUT = 0x400,
};

extern const float g_monster_rotation_offset;
extern const double g_monster_facing_tolerance;
extern const double g_monster_poster_max_distance;
extern float g_monster_light_scale;

/* Sixteen bytes the cycle runtime record carries at 0x04c, written as one block
   by the setter at 0x004C5AD0. That setter takes the block by value and VC6
   copies it with the interleaved two-register rotation it uses for a struct
   assignment, rather than the sequential load/store pairs four separate scalar
   parameters would emit - which is what makes this one object and not four.
   GrCycle copies the same 0x10 bytes onto a model instance at +0x164. */

/* One spell/condition icon attached to a monster: the icon id and the
   billboard item it owns (retail "psrBMO"). Removing an icon detaches the
   billboard from the scene before it is destroyed. */
struct W8MonsterSpellIcon {
    W8MonsterSpellIconId icon;
    std::unique_ptr<WorldItemEntity> item;
};

/* The constructor at 0x004BEA20 initializes through 0x624; the observed
   allocation is 0x628 bytes. These bound the modeled extent, not the roles
   of every byte or the complete set of callers. */

/* The GrCycle factory allocates 0x348 bytes and calls the constructor at
   0x004BFB00 for object type zero. Both constructors and the destructor install
   primary vtable 0x005ED22C and the Navigator secondary-base table at +0x18. */
class Monster {
public:
    World& owner_world;
    Navigator nav;
    CyclePlayer visual;
    std::string asset_name;
    typedef void (*CycleCallback)(Monster* monster);

    explicit Monster(World& owner, const W8MonsterRecord* species = nullptr);
    /* A new monster of a species: its own copies of the definition's animations,
       lights, emitters and light, a registered navigator, and appearance still
       to be rolled. */
    Monster(const MonsterDefinition& definition, World& owner,
            const W8MonsterRecord* species = nullptr);
    const W8MonsterRecord& species() const;
    void TickAnimation(float scale);
    bool ApplyPendingCycle();
    void SetCyclePosition(glm::vec3* position);
    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;
    ~Monster();
    // Stop exposing this runtime to the world, without reclaiming in-flight storage.
    void retire();

    bool CanEnterCycle(signed char cycle);
    void UpdateRepresentation();
    signed char GetNumSubCycles();
    bool IsCycleSupported(signed char cycle);
    signed char GetTotalAnimationCount();
    float GetCurrentAnimationScale();
    std::optional<BoundingBox> GetAnimationBounds();
    unsigned char GetAnimationRadius(float* radius);
    void SetCycle(signed char cycle);
    Animation* GetCurrentAnimation();
    void dispatchAnimationFrame(unsigned char previous_frame);
    void Update();
    void GetMappedPosition(glm::vec3* position);
    std::optional<glm::vec3> GetAnimationCenter();
    void SetPosition(const glm::vec3* position);

    // Representation state that used to live in a separate Monster.
    unsigned int ApplyEmitterSetting(signed char cycle); /* 0x004BF970 */
    AnimationMesh* GetEmitterAniMesh(signed char cycle);     /* 0x004BF920 */
    void CopyCycle(signed char cycle, signed char other_cycle);

    // Animation models must disappear before any cycle's registered lights.
    struct CycleSlot {
        EmitterLights lights;
        std::unique_ptr<Animation> animation;
        float playback_scale;
        CycleSlot(EmitterLights&& lights, std::unique_ptr<Animation> animation, float scale)
            : lights(std::move(lights)), animation(std::move(animation)), playback_scale(scale)
        {
        }
        CycleSlot(const CycleSlot& other)
            : lights(other.lights), animation(std::make_unique<Animation>(*other.animation)),
              playback_scale(other.playback_scale)
        {
        }
    };
    // Stable slots keep the light-vector addresses borrowed by CyclePlayer valid.
    std::array<std::deque<CycleSlot>, W8_MONSTER_CYCLE_COUNT> cycles;
    // Skin tables every model instance of this species carries, base included.
    int damage_stage_count = 0;
    /* 0x5bc: per-party-member highlight bitmask - bit N set while party
       member N has the monster highlighted/targeted. */
    std::string name;
    /* 0x5c4: number of populated party-icon entries in objects; the
       attachment layout read by UpdateAttachedObjects. */
    int icon_count = 0;
    std::unique_ptr<WorldItemEntity> objects[8];
    std::vector<W8MonsterSpellIcon> spell_icons;
    float standing_height = 0.0f;
    float scale = 1.0f;
    float minimum_scale = 0.0f;
    float maximum_scale = 0.0f;
    /* 0x5fc: death shrink factor multiplying scale on the death path. */
    float death_scale = 1.0f;
    /* 0x601: flies, swims or full-transitions - suppresses the grounded
       transition check at 0x004C0000. */
    bool special_movement = false;
    std::vector<std::unique_ptr<MeshInstance>> linked_runtime_objects;
    std::unique_ptr<class Light> monster_light;

    unsigned char GetNumSubsPerCycle(signed char bCycle);
    /* 0x004C4660. A method, not the free function an earlier reading assumed:
       it takes its receiver in ECX and IsDying calls it without reloading ECX
       at all, relying on `this` already being there. The query selector is
       bounded at nine by the body's own `ja` against the jump table. */
    int Query(W8MonsterQueryKind query);         /* 0x004C4660 */
    std::optional<BoundingBox> GetWorldAnimationBounds();
    glm::vec3 GetRepresentationLocation() const;
    glm::vec3 GetRepresentationLocalLocation() const;
    void SetFacing(float angle);
    void SetAdjustedPosition(const glm::vec3* position);
    unsigned short ApproachStartupNavigator(double separation);
    unsigned char LinkToStartupNavigator();
    unsigned short ConfigureMovementToPlayer(float separation, float maximum_distance,
                                             glm::vec3 position, int trace_mode,
                                             unsigned char* probe_result);
    unsigned short ConfigureMovementToMonster(Monster& target, float separation,
                                              float maximum_distance, glm::vec3 position,
                                              int trace_mode, unsigned char* probe_result);
    void AimAtMonster(const Monster& target, bool alternate);
    void SetActive(bool state);
    unsigned char SetAnimating(bool animating);
    unsigned char IsAnimating();
    bool HasPendingCycle();
    void SetPendingCycle(int cycle);
    void SetRuntimeBehaviour(W8AnimationBehaviour behaviour);
    void SetLocationId(int value);
    void ForwardReferencePosition(signed char alternate);
    void SpawnDamageNumber(unsigned int amount); /* 0x004C6C30 */
    bool IsDying();                              /* 0x004CA4C0 */
    // The death cycle is playing.
    bool IsPlayingDeath();
    /* The monster's animation or turning still holds a fight's turn: a pending or scaling
       cycle, an action short of its release point (halfway through a close swing, or until
       a ranged attack's missile leaves), any action of a monster whose own action the fight
       is not resolving, or a turn toward its target (when it can turn). Idle, spice, walk
       and death cycles hold nothing. */
    bool HoldsCombatTurn(bool resolving_own_action, bool missile_released, bool can_turn);
    bool IsCycleInterruptable(signed char cycle);
    void ApplyRemovalStateEffects();
    bool IsRenderable(bool alternate);
    void InitializeAnimatedTexture();
    void HandleAnimationFrame(unsigned char frame);
    void UpdateShakeEvents(unsigned char frame);
    void SetShakeEventVisibility(signed char cycle);
    void UpdateAttachedObjects(bool hovered = false);
    void BeginFadeIn(float duration);
    void BeginDelayedRemoval();
    void BeginFadeOutAndRemove(W8MonsterRemovalState state);
    void BeginFadeOut(float duration);
    void StartTalking(bool animate_mouth);
    void StopTalking();
    void SetCycleCallback(int cycle, CycleCallback callback);
    bool GetPatrolPoint(glm::vec3* point);
    void TrackSound(wiz8::sound::Playback playback);
    float GetDistanceToPlayer();
    float GetPointDistanceToPlayer(glm::vec3 point);
    float GetDistanceToMonster(const Monster& monster);
    float GetPointDistanceToMonster(Monster* monster, glm::vec3 point);
    bool SetScript(std::string script_name, bool reset_orders);
    void UseScript(std::shared_ptr<Script> loaded, bool reset_orders);
    void ProcessScript();
    bool ResolveScriptPosition(std::string_view name, glm::vec3* position);
    bool GetProjectilePosition(glm::vec3* position);
    bool GetSpellPosition(glm::vec3* position);
    bool GetCycleMappedPosition(signed char cycle, int mapped_index, glm::vec3* position);
    bool EvaluateScriptCondition(std::string_view expression);
    bool CanContinueScript();
    bool SetScriptLabel(std::string_view label);
    bool IsPendingFinalize() const;
    bool IsWithinWorldRange();
    bool CheckLineOfSightToPlayer();
    void GetPlayerSightFlags(bool* primary, bool* secondary);
    bool IsVisibleToPlayer(bool use_bounds);
    void GetPlayerToMonsterSightFlags(bool* primary, bool* secondary, const glm::vec3* source);
    bool HasLineOfSightToMonster(const Monster& monster);
    void GetMonsterSightFlags(const Monster& monster, bool* primary, bool* secondary);
    bool HasLineOfSightFromPoint(glm::vec3 point);
    bool IsFacingMonster(const Monster& monster);
    bool IsFacingPlayer();
    void ApplyRepresentationScale();
    glm::vec3 CycleModelScale() const;
    void RefreshStandingHeight();

public:
    unsigned int runtime_flags;
    /* 0x1e4: the monster's location id, stored by MonsterSetLocationId and
       used throughout for MonsterInfo lookups. */
    int location_id;
    /* Attack-animation frame that triggers the missile launch. */
    int missile_frame;
    /* Cycle-25 animation frame that triggers the attached spell effect. */
    int spell_frame;
    /* 0x1fc: talking state armed by StartTalking; cleared by StopTalking. */
    bool talking;
    /* 0x1fd: the StartTalking argument; mouth texture animation only runs
       while it is set. */
    bool animate_mouth;
    /* The owning game's millisecond stamp of the last random mouth-flicker frame
       while the gap track reports the mouth closed, and that frame. */
    std::uint32_t mouth_frame_clock;
    int mouth_frame;
    // When the current talk cycle began, on the world clock.
    std::uint32_t talk_start;
    unsigned int talk_duration;
    /* The mouth state the dialogue update copies out of the active
       W8MouthGapTrack; forces mouth frame 0 while open. */
    bool mouth_open;
    /* 0x215: set while the monster is deactivated (active cleared). */
    bool inactive;
    /* 0x216: raised at construction; cleared once world registration and the
       spawn bookkeeping finish - iteration skips monsters still pending. */
    bool pending_finalize;
    /* 0x217: suppresses rendering and radar/automap display. */
    bool disabled;
    bool nearest_to_party;
    W8MonsterRemovalState removal_state;
    CycleCallback cycle_callback;
    int callback_cycle;
    std::shared_ptr<Script> script;
    int script_line;
    W8MonsterScriptCommand script_wait;
    std::vector<unsigned char> script_conditions;
    W8GameTimer script_delay_timer;
    Trigger* trigger;
    glm::vec3 formation;
    bool defining_orders;
    bool orders_finished;
    W8MonsterOrderMode order_mode;
    bool deaf;
    bool face_party;
    bool stay_home;
    float patrol_distance;
    float patrol_variation;
    std::vector<glm::vec3> vector;
    signed char patrol_index;
    float direction_x;
    float direction_y;
    float direction_z;
    /* 0x2bc: the direction the real-time AI moves the monster along, added to
       its position to pick the aim point (mode 0xa). */
    glm::vec3 move_direction;
    int look_frequency;
    int look_duration;
    /* 0x2d0: sun-visibility state for the model light-scale lerp: -1
       uninitialized, 1 lit (scale toward 0.75), 0 shadowed (toward 0). */
    W8MonsterSunlightState sunlit_state;
    bool move_dirty;
    W8GameTimer light_scale_timer;
    float target_scale;
    float current_scale;
    /* 0x304: one-shot latch; the cycle-25 spell frame fires
       CreateAttachedSpellEffect once then clears it. */
    bool spell_effect_armed;
    /* The render mesh of the world sector this monster currently occupies,
       cached by LevelSpatialIndex::UpdateMonsterLocation (null while unplaced).
       IsWithinWorldRange answers from its disable flag when present.
       This is not the inherited navigator scene node. */
    MeshInstance* sector_mesh;
    W8GameTimer fade_timer;
    W8MonsterFadeState fade_state;
    /* 0x331: this monster is the highlighted target; exempt from the
       attachment distance-scale clamp. */
    /* 0x332: copied from the source monster; blocks hostility recompute in
       Targeting and Combat Hostility. */
    bool hostility_preserved;
    std::unique_ptr<Sound3D> sound;
    std::vector<wiz8::sound::Playback> sounds;

private:
    // Immutable gameplay definition outlives this body. Cursor-only bodies have none.
    const W8MonsterRecord* const species_definition_;
};

/* What a species' MLS and .mon files author, loaded once per world: the
   animations with their lights outside any scene, the cycle events and
   emitters, and the parameters each new monster starts from or rolls its
   appearance from. Nothing in it plays, lights or navigates. */
struct MonsterDefinition {
    std::string asset_name;
    std::array<std::deque<Monster::CycleSlot>, W8_MONSTER_CYCLE_COUNT> cycles;
    CycleDefinition events;
    std::string skin;
    int damage_stage = -1;
    int damage_stage_count = 0;
    std::shared_ptr<Script> script;

    float scale = 1.0f;
    float minimum_scale = 0.0f;
    float maximum_scale = 0.0f;
    float death_scale = 1.0f;
    /* The idle cycle's playback scale is rolled per monster in
       [random_idle_fps_min, random_idle_fps_max] on top of the idle animation's own. */
    bool random_idle = false;
    float idle_playback_scale = 10.0f;
    float random_idle_fps_min = 0.0f;
    float random_idle_fps_max = 0.0f;
    // Percent chance a monster is mirrored to strike left-handed.
    int left_handed = 0;
    bool special_movement = false;
    int missile_frame = 0;
    int spell_frame = 0;
    // Hover base and bob amplitude ranges, before kWorldScale.
    float hover_base_min = 0.0f;
    float hover_base_max = 0.0f;
    float bob_amplitude_min = 0.0f;
    float bob_amplitude_max = 0.0f;
    // A spice monster: hostility is never recomputed and it does not navigate.
    bool hostility_preserved = false;

    std::optional<std::pair<float, float>> lod_range;
    std::optional<float> opacity;
    std::optional<float> emissive_override;
    std::optional<std::pair<float, float>> ground_shadow; // width, depth
    std::unique_ptr<Light> light;                          // in no scene

    // Navigator settings; the defaults are a new navigator's.
    float collision_radius = 500.0f;
    float alternate_radius = 500.0f;
    float height_offset = 500.0f;
    float secondary_height_offset = 500.0f;
    BoundingBox bounds{glm::vec3(-500.0f), glm::vec3(500.0f)};
    float movement_scale = 0.0f;
    float turn_rate = 0.0f;
    W8NavigatorMode navigation_mode = W8_NAVIGATOR_MODE_DEFAULT;

    // Append one .mon animation to `cycle_index` (or the cycle it names, when NONE).
    bool ReadCycleData(LevelLoadContext& info, int cycle_index);
    // Every distinct mesh the animations draw: what load-time skin setup reaches.
    std::vector<Mesh*> CollectModelMeshes();
    bool ReplaceSkinTexture(int stage, std::string_view old_name, std::string_view new_name);
    int AddDamageStage(int stage);
};

W8MonsterCycle ParseMonsterCycleName(std::string_view name, signed char* subcycle = nullptr);
unsigned short ChooseDifferentMonsterDirection(unsigned short previous_direction);

std::unique_ptr<Monster> LoadMonsterCycle(World* world, std::string_view mon_name, int cycle);

/* Secondary vftable 0x005ed218 keeps the Navigator subobject at +0x18. */

// Owned by a particle while it temporarily controls the monster animation.
struct W8MonsterShakeCallback {
    void RestoreAnimation();
    Monster* m_pMonster = nullptr;
    ParticleSystem* m_pParticles = nullptr;
    W8AnimationBehaviour saved_behaviour{};
    W8AnimationFrameMethod saved_frame_method{};
};

void SetMonsterPartySlotMarker(int party_slot, int location_id, signed char on);

std::unique_ptr<WorldItemEntity> CreateMonsterIconItem(World& world, std::string_view path);
/* A Monster script by name, shared through the script cache; null when missing. */
std::shared_ptr<Script> LoadMonsterScript(std::string_view name);
/* One eight-byte row per animation cycle at 0x0060EA08. The parser at
   0x004C2010 reads prefix_length as a signed byte. Its optional atoi offset
   comes from the search index, which can differ from the returned cycle. */
struct W8CycleNameRow {
    const char* name;
    signed char prefix_length;
};
extern W8CycleNameRow g_cycle_names[];
// bool-byte-ok: automap save/restore (0x0057E660/0x0057FB40) copies it through a byte array
// unnormalized
extern unsigned char g_monster_shadow_updates_enabled;

#endif

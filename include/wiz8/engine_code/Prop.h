#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/game_timer.h"

class GDProp;
struct W8AnimObj;
struct W8ReadLevelInfo;
struct W8World;
class srModelInstance;
class stModelInstance;
class Trigger;
class W8Prop;
struct W8AIMissile;
class W8GameTimer;
namespace wiz8 {
class File;
}

extern bool g_animated_prop_present;

/* One two-byte animation-slot record: the frame the segment selects and the
   tag that names it.  LoadProp reads each as a serialized short and
   narrows to a byte; the segment walkers sign-extend the frame, and
   SelectAnimationSlot treats a negative frame as a slot with no animation. */
struct W8PropAnimationSegment {
    signed char frame;
    unsigned char tag;
};

// Optional animation data and segment values belong to the representation.
class W8PropRepresentation : public W8AnimRep {
public:
    /* Default construction is inlined at Prop::Prop. */
    W8PropRepresentation()
        : animation_speed(0.0f), frame_index(0), animation_running(0), random_play(0),
          play_chance(0.5f), saved_subcycle(0), frame_steps(0), slots(), footstep_surface(0xff),
          footstep_material(0xff)
    {
    }
    W8PropRepresentation(const W8PropRepresentation& other);
    virtual ~W8PropRepresentation() override;
    W8PropRepresentation& operator=(const W8PropRepresentation&) = delete;
    virtual W8AnimRepBase* Clone() override;

    srModelInstance* ToggleAnimation(int argument); /* 0x0044BA00 */
    unsigned char SelectAnimationSlot(unsigned char tag);
    int FindCurrentAnimationSlot(); /* 0x0044BAE0 */
    unsigned char AdvanceAnimationSegment();
    /* CreateAndLoadProp loads m_pRep into ECX, then passes (pInfo, pProp). */
    bool LoadProp(W8ReadLevelInfo* info, W8Prop* prop); /* 0x0044AEE0 */

    std::optional<W8AnimObj> animation;
    W8AnimObj* Animation() { return animation ? &*animation : nullptr; }
    float animation_speed; /* 0x9c */
    /* 0xa0: integer path position accumulator.  UpdatePropAnimation adds the elapsed
       frame count to it with a dword add, compares it against the animation's
       value_16, and FILD-converts it for PathAISetValue. */
    int frame_index;
    bool animation_running; /* 0xa4 */
    bool random_play;       /* 0xa5 */
    float play_chance;            /* 0xa8: constructed as 0.5 */
    unsigned char saved_subcycle; /* 0xac */
    unsigned char frame_steps;    /* 0xad */
    std::vector<W8PropAnimationSegment> slots; /* 0xb0 */
    unsigned char footstep_surface;          /* 0xc0 */
    unsigned char footstep_material;         /* 0xc1 */
}; /* 0xc4 */

/* Combined W8Prop::flags masks; these are distinct from representation flags. */
enum {
    W8_PROP_COLLIDABLE = 0x01u,
    W8_PROP_ACCUMULATE_PATH_FRAMES = 0x02u,
    W8_PROP_ANIMATION_GEOMETRY_DIRTY = 0x20u,
    W8_PROP_NO_DIRECT_SUN = 0x40u,
    W8_PROP_GD_TRIGGER_BOUND = 0x80u
};

class W8Prop : public W8GrObject {
    W8PropRepresentation representation;

public:
    W8Prop();                   /* 0x0044BC00 */
    virtual ~W8Prop() override; /* complete destructor 0x0044BEC0 */

    W8PropRepresentation* Rep() const { return static_cast<W8PropRepresentation*>(m_pRep); }

    void DetachAnimationInstances(W8World* world);
    W8Prop(const W8Prop&) = delete;
    W8Prop& operator=(const W8Prop&) = delete;
    void UpdatePropAnimation();
    /* Re-apply every animation path and roll the position snapshots forward.
       `world` is only used by the pWorld assertion. */
    void ApplyAnimationPaths(W8World* world);
    /* Advance the rep's animation value by `frames`, honouring the direction,
       bounce and wrap modes; clamps to the counter range and pushes the new
       value to every bound path. `total` is the animation's frame count. */
    void AdvanceAnimationValue(int frames, char total);
    /* The animation value one step ahead of the current one, without
       committing it - clamped for transitive animations, wrapping or bouncing
       for the looping kinds. */
    char NextAnimationValue();
    void ApplyAnimationFrame(); /* 0x0044C670 */
    /* Restore the rep's persisted animation state: five saved bytes plus one
       discarded byte, clamped to the loaded animation's frame count, with
       path values re-synced while a running animation is active. */
    struct SavedAnimationState {
        unsigned char subcycle, first_frame, last_frame, frame_direction, animation_playing, unused;
    };
    void ApplyAnimationState(const SavedAnimationState& state);        /* 0x0044DBD0 */
    int BuildOrRefreshPathingRepresentation(); /* 0x0044DEA0 */
    /* Current minus previous position when the animation advances exactly
       one frame; otherwise zero. */
    glm::vec3 GetDelta(); /* 0x0044E130 */
    /* The prop's position for external queries: animation_position while its
       animation runs, else the rep node's location. */
    glm::vec3 GetPosition() const; /* 0x0044E2C0 */
    /* Mirror of GetPosition: stores `position` in animation_position while the
       animation runs, else moves the rep node through SetLocation. */
    void SetPosition(glm::vec3* position); /* 0x0044E310 */
    bool TriggerHasActionMessage();                /* 0x0044E360 */
    /* Whether trigger exists and takes an item (required_item_id >= 0 or a
       type-10 action payload naming item). */
    bool TriggerRequiresItem(); /* 0x0044E380 */
    /* The prop's current animation value; -1 when it has none. */
    int GetAnimationState() const; /* 0x0044EBE0 */
    void AttachAnimationInstances(W8World* world);
    /* Raw activation byte, also persisted in prop animation state. */
    unsigned char GetActivationState();
    srModelInstance* ToggleRepAnimation(int argument);
    srModelInstance* ToggleRepAnimationDefault();
    std::optional<srBoundingBox> GetFrameBounds();
    void SetAnimationDirection(W8AnimationDirection direction);
    void SetRepresentationActive(unsigned char active, bool update_animation);
    bool CanBeUsedFrom(int path_x, int path_z, bool notify);
    void SetActivationState(unsigned char value);
    /* Queues the signed byte as a short; -1 becomes the 0xffff sentinel. */
    void SetPendingAnimationSubcycle(char value);
    void SetAnimationSpeed(float speed);
    bool IsAnimationPingPong();
    void ReverseAnimationDirection();
    Trigger* GetTrigger();
    bool IsTriggerInView(glm::vec3* position);
    Trigger* GetGDPropOwnerTrigger();
    std::optional<glm::vec3> GetCenterPosition();
    /* Whether the renderer's currently selected model instance is dispatched
       by this prop's animation - the prop half of ResolvePickedProp's test.
       `world` is accepted but never read. */
    bool IsPickedProp(W8World* world); /* 0x0044D680 */
    std::optional<srBoundingBox> GetBounds();
    void CollectModelInstances(std::vector<stModelInstance*>& instances);
    /* Run trigger when its action is one of the missile-impact kinds
       (0x3a..0x3c); the record hands Run the missile's table index. */
    void RunMissileTrigger(W8AIMissile* record);

    Trigger* trigger;   /* 0x18 */
    unsigned int flags; /* 0x1c */
    std::string name;
    /* 0x24: UpdatePropAnimation stores the animation timer's progress here, then
       reduces it by the whole-frame count - the fractional remainder. */
    float anim_frame_fraction;
    W8GameTimer timer;
    glm::vec3 animation_position;          /* 0x2c: written by ApplyAnimationFrame */
    std::unique_ptr<GDProp> m_gd_prop;
    glm::vec3 previous_animation_position; /* 0x3c */
    /* Prop::Prop writes two identity bases here as nine floats each. */
    glm::mat3 previous_animation_rotation; /* 0x48 */
    glm::mat3 animation_rotation;          /* 0x6c */
}; /* 0x90 */

W8Prop* FindPropByName(W8World* world, const char* name);
std::unique_ptr<W8Prop> CreateAndLoadProp(W8ReadLevelInfo* info);

bool ResolvePickedProp(W8World* world);
int GetSelectedPropIndex(void);
/* Run the latched selected-prop trigger, or clear the latch when the
   renderer has no pick. */
bool ActivateSelectedProp(void);
void UpdateWorldProps(W8World* world);

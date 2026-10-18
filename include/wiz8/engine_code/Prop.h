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
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/game_timer.h"

class W8PathingService;
struct W8ReadLevelInfo;
struct W8World;
class srModelInstance;
class stModelInstance;
class Trigger;
class W8Prop;
struct W8AIMissile;
namespace wiz8 {
class File;
}

extern bool g_animated_prop_present;

struct W8PropAnimationSegment {
    signed char frame;
    unsigned char tag;
};

/* Combined W8Prop::flags masks. */
enum {
    W8_PROP_COLLIDABLE = 0x01u,
    W8_PROP_ACCUMULATE_PATH_FRAMES = 0x02u,
    W8_PROP_ANIMATION_GEOMETRY_DIRTY = 0x20u,
    W8_PROP_NO_DIRECT_SUN = 0x40u,
    W8_PROP_GD_TRIGGER_BOUND = 0x80u
};

class W8Prop : public W8GrObject, public W8AnimRep {
public:
    W8Prop();
    virtual ~W8Prop() override;
    W8Prop(const W8Prop&) = delete;
    W8Prop& operator=(const W8Prop&) = delete;

    // Animation representation operations (now directly on W8Prop):
    srModelInstance* ToggleAnimation(int argument);
    unsigned char SelectAnimationSlot(unsigned char tag);
    int FindCurrentAnimationSlot();
    unsigned char AdvanceAnimationSegment();
    bool LoadProp(W8ReadLevelInfo* info, W8Prop* prop);

    std::optional<W8AnimObj> animation;
    W8AnimObj* Animation() { return animation ? &*animation : nullptr; }
    const W8AnimObj* Animation() const { return animation ? &*animation : nullptr; }
    float animation_speed = 0.0f;
    int frame_index = 0;
    bool animation_running = false;
    bool random_play = false;
    float play_chance = 0.5f;
    unsigned char saved_subcycle = 0;
    unsigned char frame_steps = 0;
    std::vector<W8PropAnimationSegment> slots;
    unsigned char footstep_surface = 0xff;
    unsigned char footstep_material = 0xff;

    void DetachAnimationInstances(W8World* world);
    void UpdatePropAnimation();
    void ApplyAnimationPaths();
    void AdvanceAnimationValue(int frames, char total);
    char NextAnimationValue();
    void ApplyAnimationFrame(W8PathingService* pathing);
    struct SavedAnimationState {
        unsigned char subcycle, first_frame, last_frame, frame_direction, animation_playing, unused;
    };
    void ApplyAnimationState(const SavedAnimationState& state, W8PathingService* pathing);
    int BuildOrRefreshPathingRepresentation(W8PathingService* pathing);
    glm::vec3 GetDelta();
    glm::vec3 GetPosition() const;
    void SetPosition(glm::vec3* position);
    bool TriggerHasActionMessage();
    bool TriggerRequiresItem();
    int GetAnimationState() const;
    void AttachAnimationInstances(W8World* world);
    unsigned char GetActivationState();
    srModelInstance* ToggleRepAnimation(int argument);
    srModelInstance* ToggleRepAnimationDefault();
    std::optional<srBoundingBox> GetFrameBounds();
    void SetAnimationDirection(W8AnimationDirection direction);
    void SetRepresentationActive(unsigned char active, bool update_animation);
    bool CanBeUsedFrom(int path_x, int path_z, bool notify);
    void SetActivationState(unsigned char value);
    void SetPendingAnimationSubcycle(char value);
    void SetAnimationSpeed(float speed);
    bool IsAnimationPingPong();
    void ReverseAnimationDirection();
    Trigger* GetTrigger();
    bool IsTriggerInView(glm::vec3* position);
    Trigger* GetGDPropOwnerTrigger();
    std::optional<glm::vec3> GetCenterPosition();
    std::optional<srBoundingBox> GetBounds();
    void CollectModelInstances(std::vector<stModelInstance*>& instances);
    void RunMissileTrigger(W8AIMissile* record);

    Trigger* trigger = nullptr;
    unsigned int flags = 0;
    std::string name;
    float anim_frame_fraction = 0.0f;
    W8GameTimer animation_timer; // W8AnimRep::timer is the frame-advance stamp
    glm::vec3 animation_position{0.0f};
    std::unique_ptr<GDProp> m_gd_prop;
    glm::vec3 previous_animation_position{0.0f};
};

W8Prop* FindPropByName(W8World* world, std::string_view name);
std::unique_ptr<W8Prop> CreateAndLoadProp(W8ReadLevelInfo* info);

bool ResolvePickedProp();
int GetSelectedPropIndex(void);
bool ActivateSelectedProp(void);
void UpdateWorldProps(W8World* world);

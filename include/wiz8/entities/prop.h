#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/geometry.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/navigation.h"

class NavigationMesh;
struct LevelLoadContext;
struct World;
struct W8WorldItem;
class MeshInstance;
class Trigger;
class Prop;
struct MissileFlight;
enum W8FootstepSurface : unsigned char;
enum W8FootstepMaterial : unsigned char;
namespace wiz8 {
class File;
}

extern bool g_animated_prop_present;

struct W8PropAnimationSegment {
    signed char frame;
    unsigned char tag;
};

/* Combined Prop::flags masks. */
enum {
    W8_PROP_COLLIDABLE = 0x01u,
    W8_PROP_ACCUMULATE_PATH_FRAMES = 0x02u,
    W8_PROP_ANIMATION_GEOMETRY_DIRTY = 0x20u,
    W8_PROP_NO_DIRECT_SUN = 0x40u,
    W8_PROP_GD_TRIGGER_BOUND = 0x80u
};

class Prop {
public:
    Prop();
    ~Prop();
    Prop(const Prop&) = delete;
    Prop& operator=(const Prop&) = delete;

    void SetLocation(const glm::vec3* new_location)
    {
        local_location = *new_location;
        location = parent_location + local_location;
    }
    glm::vec3 GetLocation() const { return location; }
    glm::vec3 GetLocalLocation() const { return local_location; }
    void SetRotation(const glm::mat3* new_rotation) { rotation = *new_rotation; }
    glm::mat3 GetRotation() const { return rotation; }
    void SetFrameMethod(W8AnimationFrameMethod method);

    // Key used only when loading the old numeric APST save format.
    int legacy_save_id;
    glm::vec3 location{0.0f};
    glm::vec3 local_location{0.0f};
    glm::vec3 parent_location{0.0f};
    glm::mat3 rotation{1.0f};
    unsigned char subcycle = 0;
    unsigned short pending_subcycle = 0xffff;
    unsigned char active = 0;
    unsigned char animation_playing = 0;
    W8AnimationDirection frame_direction = W8_ANIMATION_DIRECTION_NONE;
    W8AnimationFrameMethod frame_method = W8_ANIMATION_FRAME_METHOD_NONE;
    W8AnimationBehaviour animation_behaviour = W8_ANIMATION_BEHAVIOUR_NONE;
    W8AnimationBehaviour pending_behaviour = W8_ANIMATION_NO_PENDING_BEHAVIOUR;
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
    float bounds_extent = 0.0f;
    unsigned char first_frame = 0xff;
    unsigned char last_frame = 0xff;

    // Animation representation operations (now directly on Prop):
    // The model showing `frame`, or the first part of a path-list animation.
    MeshInstance* FrameModel(int frame);
    // The instance that draws part `index` of the animation; the prop owns one per part.
    MeshInstance& model(std::size_t index = 0);
    bool SelectAnimationSlot(unsigned char tag);
    int FindCurrentAnimationSlot();
    unsigned char AdvanceAnimationSegment();
    bool LoadProp(LevelLoadContext* info, Prop* prop);

    std::optional<Animation> animation;
    Animation* currentAnimation() { return animation ? &*animation : nullptr; }
    const Animation* currentAnimation() const { return animation ? &*animation : nullptr; }
    float animation_speed = 0.0f;
    int frame_index = 0;
    bool animation_running = false;
    bool random_play = false;
    float play_chance = 0.5f;
    unsigned char saved_subcycle = 0;
    unsigned char frame_steps = 0;
    std::vector<W8PropAnimationSegment> slots;
    W8FootstepSurface footstep_surface = static_cast<W8FootstepSurface>(0xff);
    W8FootstepMaterial footstep_material = static_cast<W8FootstepMaterial>(0xff);

    void DetachAnimationInstances(World* world);
    /* The step's prop animation service: `elapsed` is the step time since the previous
       one. */
    void UpdatePropAnimation(std::chrono::milliseconds elapsed, bool held);
    /* A frame lasts 1 / animation_speed seconds; a speed that gives no positive length
       keeps the previous one. */
    void UpdateAnimationFrameLength();
    void ApplyAnimationPaths();
    void AdvanceAnimationValue(int frames, signed char total);
    signed char NextAnimationValue() const;
    void ApplyAnimationFrame(NavigationMesh* pathing);
    struct SavedAnimationState {
        unsigned char subcycle, first_frame, last_frame;
        W8AnimationDirection frame_direction;
        unsigned char animation_playing, unused;
    };
    SavedAnimationState CaptureAnimationState() const;
    void ApplyAnimationState(const SavedAnimationState& state, NavigationMesh* pathing);
    int BuildOrRefreshPathingRepresentation(NavigationMesh* pathing);
    glm::vec3 GetDelta() const;
    glm::vec3 GetPosition() const;
    void SetPosition(glm::vec3* position);
    void activate();
    bool TriggerHasActionMessage();
    bool TriggerRequiresItem();
    int GetAnimationState() const;
    void AttachAnimationInstances(World* world);
    unsigned char GetActivationState() const;
    std::optional<BoundingBox> GetFrameBounds();
    void SetAnimationDirection(W8AnimationDirection direction);
    void SetRepresentationActive(unsigned char active, bool update_animation);
    bool CanBeUsedFrom(int path_x, int path_z, bool notify);
    void SetActivationState(bool active);
    /* Queues the signed byte as a short; -1 becomes the 0xffff sentinel. */
    void SetPendingAnimationSubcycle(signed char value);
    void SetAnimationSpeed(float speed);
    bool IsAnimationPingPong();
    void ReverseAnimationDirection();
    Trigger* GetTrigger();
    bool IsTriggerInView(glm::vec3* position);
    Trigger* boundTrigger();
    std::optional<glm::vec3> GetCenterPosition();
    std::optional<BoundingBox> GetBounds();
    void RunMissileTrigger(MissileFlight* record);

    Trigger* trigger = nullptr;
    unsigned int flags = 0;
    // Cleared for props the level marks as never pickable.
    bool pickable = true;
    // Load-time presentation of every model: the prop's name and screen-facing alignment.
    bool face_camera = false;
    std::vector<std::unique_ptr<MeshInstance>> models;
    std::string name;
    float anim_frame_fraction = 0.0f;
    /* The step time since the animation's current frame began, and how long a frame lasts
       (1 / animation_speed seconds). It stands still while the shared timers are held. */
    std::chrono::duration<float> animation_elapsed{};
    std::chrono::duration<float> animation_frame{1.0f};
    glm::vec3 animation_position{0.0f};
    bool collision_ready = false;
    bool collision_unattached = false;
    unsigned short collision_frame = 0;
    unsigned int m_path_handle = 0;
    std::vector<unsigned short> m_path_edges, m_path_waypoints;
    std::vector<W8GDSurface> m_pGDSurfaces;
    std::vector<glm::vec3> m_pVertices;
    Trigger* collision_trigger = nullptr;
    W8PathVerticalRange m_path_range{};
    glm::vec3 m_bound_min{0}, m_bound_max{0};
    W8PathGridBounds m_path_bounds{};
    std::vector<W8WorldItem*> m_supported_items;

    void initializeCollision(NavigationMesh* pathing, MeshInstance* instance,
                             std::string_view path_name, unsigned short frame,
                             W8FootstepSurface surface, W8FootstepMaterial material);
    void updateCollision(NavigationMesh* pathing, MeshInstance* instance, bool attach,
                         unsigned short frame, W8FootstepSurface surface,
                         W8FootstepMaterial material);
    void PrepareGeometry(MeshInstance* instance);
    void BindTrigger(Trigger* owner, NavigationMesh* pathing);
    bool ContainsPathCoordinate(unsigned short x, unsigned short y) const;
    bool HasSupportedItems();
    BoundingBox computeBounds();
    std::span<const unsigned short> conditionalEdges() const { return m_path_edges; }
    std::span<const unsigned short> conditionalWaypoints() const { return m_path_waypoints; }
    bool RegisterPathSurface(unsigned int index, const glm::ivec2& point);
    bool RegisterPathVertex(unsigned int index, const glm::ivec2& point, const glm::ivec2& second);
    glm::vec3 previous_animation_position{0.0f};
};

Prop* FindPropByName(World* world, std::string_view name);
std::unique_ptr<Prop> CreateAndLoadProp(LevelLoadContext* info);

void UpdateWorldProps(World* world);

#ifndef WIZ8_ENGINE_CODE_ANIM_OBJ_H
#define WIZ8_ENGINE_CODE_ANIM_OBJ_H

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/spatial/bounding_box.h"

struct AnimationMesh;
#include "wiz8/entities/keyframe_track.h"
struct LevelLoadContext;
class Light;
class MeshInstance;
enum W8AnimationFrameMethod : unsigned char;
enum W8AnimationBehaviour : unsigned char;

struct W8FrameBounds {
    bool known = false;
    glm::vec3 minimum{0, 0, 0};
    glm::vec3 maximum{0, 0, 0};
};

struct Animation {
    Animation();
    Animation(const Animation& other);
    Animation& operator=(const Animation&) = delete;
    ~Animation();

    unsigned char group_count{};       // mesh/list group count, max 3
    bool animation_playing{}; // copied onto monster/prop animation_playing
    W8AnimationFrameMethod frame_method{}; // copied onto frame_method
    W8AnimationBehaviour behaviour{};     // copied onto animation_behaviour
    unsigned char cycle{};             // default cycle/emitter index
    bool path_lists{};        // 0 = mesh entries, nonzero = path lists
    float playback_scale{};
    bool random_play{}; // copied onto prop random_play
    /* Serialized as a float; 0x004A0320 copies its four-byte representation. */
    float play_chance{}; // copied onto prop play_chance
    unsigned char start_frame{};
    unsigned char end_frame{};
    unsigned char frame_count{}; // prop clamps frame_index to it
    std::unique_ptr<AnimationMesh> entries[3];
    std::optional<KeyframeTrack> path;
    // Each group pairs meshes with the paths that place them.
    std::vector<std::unique_ptr<AnimationMesh>> meshes[3];
    std::vector<std::optional<KeyframeTrack>> paths[3];
    std::vector<W8FrameBounds> frame_bounds;
};

void TransformBounds(const glm::mat3* rotation, const glm::vec3* translation,
                     const glm::vec3* scale, glm::vec3* minimum, glm::vec3* maximum);
unsigned int AnimObjValue(const Animation* animation, signed char index);
unsigned int AnimObjListCount(Animation* animation, signed char index);
KeyframeTrack* AnimObjListEntry(Animation* animation, signed char list_index,
                                signed char entry_index);
bool AnimObjReadFromFile(LevelLoadContext* info, Animation* animation,
                         std::vector<std::unique_ptr<Light>>* light_list);
MeshInstance* AnimObjDispatch(Animation* animation, signed char list_index, unsigned char value);
MeshInstance* AnimObjDispatchList(Animation* animation, signed char list_index,
                                  signed char entry_index);
AnimationMesh* AnimObjEntry(Animation* animation, signed char list_index, std::size_t entry_index);
std::optional<BoundingBox> AnimObjGetBounds(Animation* animation, signed char list_index,
                                            unsigned int frame);

#endif

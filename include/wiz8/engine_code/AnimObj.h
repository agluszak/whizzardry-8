#ifndef WIZ8_ENGINE_CODE_ANIM_OBJ_H
#define WIZ8_ENGINE_CODE_ANIM_OBJ_H

#include <memory>
#include <optional>
#include <vector>

#include "surrender/srMath.h"
#include "surrender/srBoundingBox.h"
#include "glm/ext/matrix_float3x3.hpp"

struct W8AniMesh;
struct W8PathAI;
struct W8ReadLevelInfo;
class stLight;
class srModelInstance;

struct W8FrameBounds {
    bool known = false;
    srVector3T<float> minimum{0, 0, 0};
    srVector3T<float> maximum{0, 0, 0};
};

struct W8AnimObj {
    W8AnimObj();
    W8AnimObj(const W8AnimObj& other);
    W8AnimObj& operator=(const W8AnimObj&) = delete;
    ~W8AnimObj();

    unsigned char group_count{};       // mesh/list group count, max 3
    bool animation_playing{}; // copied onto monster/prop animation_playing
    unsigned char frame_method{};      // copied onto frame_method
    unsigned char behaviour{};         // copied onto animation_behaviour
    unsigned char cycle{};             // default cycle/emitter index
    bool path_lists{};        // 0 = mesh entries, nonzero = path lists
    float playback_scale{};
    bool random_play{}; // copied onto prop random_play
    /* Serialized as a float; 0x004A0320 copies its four-byte representation. */
    float play_chance{}; // copied onto prop play_chance
    unsigned char start_frame{};
    unsigned char end_frame{};
    unsigned char frame_count{}; // prop clamps frame_index to it
    std::unique_ptr<W8AniMesh> entries[3];
    std::unique_ptr<W8PathAI> path;
    // Each group pairs meshes with the paths that place them.
    std::vector<std::unique_ptr<W8AniMesh>> meshes[3];
    std::vector<std::unique_ptr<W8PathAI>> paths[3];
    std::vector<W8FrameBounds> frame_bounds;
};

void TransformBounds(const glm::mat3* rotation, const srVector3T<float>* translation,
                     const srVector3T<float>* scale, srVector3T<float>* minimum,
                     srVector3T<float>* maximum);
unsigned int AnimObjValue(W8AnimObj* animation, signed char index);
unsigned int AnimObjListCount(W8AnimObj* animation, signed char index);
W8PathAI* AnimObjListEntry(W8AnimObj* animation, signed char list_index, signed char entry_index);
bool AnimationIsRunning(W8AnimObj* animation);
bool AnimObjReadFromFile(W8ReadLevelInfo* info, W8AnimObj* animation, int load_all,
                                  std::vector<stLight*>* light_list, int);
srModelInstance* AnimObjDispatch(W8AnimObj* animation, signed char list_index, unsigned char value);
srModelInstance* AnimObjDispatchList(W8AnimObj* animation, signed char list_index,
                                     signed char entry_index);
W8AniMesh* AnimObjEntry(W8AnimObj* animation, signed char list_index, unsigned int entry_index);
std::optional<srBoundingBox> AnimObjGetBounds(W8AnimObj* animation, signed char list_index,
                                            unsigned int frame);

#endif

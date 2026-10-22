#ifndef WIZ8_ENGINE_CODE_ANIM_OBJ_H
#define WIZ8_ENGINE_CODE_ANIM_OBJ_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/spatial/bounding_box.h"
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

struct AnimationPart {
    AnimationMesh mesh;
    std::optional<KeyframeTrack> path;
};

struct Animation {
    Animation();
    Animation(const Animation& other);
    Animation& operator=(const Animation&) = delete;
    ~Animation();

    bool animation_playing{}; // copied onto monster/prop animation_playing
    W8AnimationFrameMethod frame_method{}; // copied onto frame_method
    W8AnimationBehaviour behaviour{};     // copied onto animation_behaviour
    unsigned char cycle{};             // default cycle/emitter index
    bool positioned_parts{}; // simultaneous placed parts, rather than a frame sequence
    float playback_scale{};
    bool random_play{}; // copied onto prop random_play
    /* Serialized as a float; 0x004A0320 copies its four-byte representation. */
    float play_chance{}; // copied onto prop play_chance
    unsigned char start_frame{};
    unsigned char end_frame{};
    unsigned char frame_count{}; // prop clamps frame_index to it
    std::optional<KeyframeTrack> path;
    std::array<std::vector<AnimationPart>, 3> parts;

    AnimationMesh* mesh(signed char lod, std::size_t part = 0);
    KeyframeTrack* partPath(signed char lod, signed char part);
    AnimationFrame frame(signed char lod, unsigned char frame, std::size_t part = 0);
    unsigned int frameCount(signed char lod) const;
    std::vector<W8FrameBounds> frame_bounds;
};

void TransformBounds(const glm::mat3* rotation, const glm::vec3* translation,
                     const glm::vec3* scale, glm::vec3* minimum, glm::vec3* maximum);
bool AnimObjReadFromFile(LevelLoadContext* info, Animation* animation,
                         std::vector<std::unique_ptr<Light>>* light_list);
std::optional<BoundingBox> AnimObjGetBounds(Animation* animation, signed char list_index,
                                            unsigned int frame);

#endif
